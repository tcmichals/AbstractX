/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux Target: I/O processor back end.
 * The shared IoEngine ([SPEC-ARCH-13]) decodes TLPs; this file maps its transfers onto the
 * spidev / i2c-dev worker threads, its triggers onto gpiod events in an epoll reactor, and
 * its host notification onto an eventfd.
 */

#include "abstractx/hal/io_engine.hpp"
#include "epoll_reactor.hpp"
#include "eventfd.hpp"
#include "spi_worker.hpp"
#include "i2c_worker.hpp"
#include "gpio_gpiod.hpp"
#include "linux_io.hpp"

#include <array>
#include <atomic>
#include <cstdio>

namespace abstractx::target {

// @impl [SPEC-ARCH-13] [SPEC-LINUX-SPI-01] targets/linux/SPECIFICATION.md#spec-linux-spi-01
class LinuxIoBackend final : public hal::IIoBackend {
public:
    static constexpr size_t MAX_EVENTS = 16;

    LinuxIoBackend() noexcept : rx_doorbell_(true) {
        for (auto& j : jobs_) j.owner = this;
    }

    void attach(hal::IoEngine& engine) noexcept { engine_ = &engine; }

    void set_gpiochip(const char* path) noexcept { gpiochip_ = (path && path[0]) ? path : "/dev/gpiochip0"; }

    // @impl [SPEC-LINUX-GPIO-01] targets/linux/SPECIFICATION.md
    bool init_channel(const hal::AutoChannelConfig& ch) noexcept override {
        if (!rx_registered_ && reactor_.is_valid()) {
            reactor_.add_fd_u64(rx_doorbell_.fd(), EPOLLIN, FD_TAG_RX_DOORBELL);
            rx_registered_ = true;
        }
        if (ch.trigger_mode == hal::TriggerMode::GpioEdge) {
            if (num_triggers_ >= hal::IoEngine::kMaxChannels) return false;
            auto& t = triggers_[num_triggers_];
            t.pin = ch.trigger_pin;
            if (!t.monitor.open_pin_interrupt(gpiochip_, ch.trigger_pin, ch.trigger_rising, !ch.trigger_rising)) {
                return false;   // a real chip refused the line: never fall back to simulation
            }
            const int fd = t.monitor.get_fd();
            if (fd >= 0) reactor_.add_fd_u64(fd, EPOLLIN | EPOLLET, FD_TAG_TRIGGER_BASE + num_triggers_);
            ++num_triggers_;
        }
        if (ch.bus_type == hal::BusType::Spi) {
            if (!spi_.is_running()) start_spi(ch.bus_index, static_cast<uint8_t>(ch.cs_pin), ch.bus_speed_hz);
        } else if (ch.bus_type == hal::BusType::I2c) {
            if (!i2c_.is_running()) i2c_.start("/dev/i2c-1");
        }
        return true;
    }

    bool trigger_simulated(uint32_t pin) const noexcept {
        for (size_t i = 0; i < num_triggers_; ++i) {
            if (triggers_[i].pin == pin) return triggers_[i].monitor.is_simulated();
        }
        return false;
    }

    bool simulate_edge(uint32_t pin) noexcept {
        for (size_t i = 0; i < num_triggers_; ++i) {
            if (triggers_[i].pin == pin && triggers_[i].monitor.is_simulated()) {
                triggers_[i].monitor.trigger_simulated_edge();
                return true;
            }
        }
        return false;
    }

    // Bridge transfers may arrive before any channel uses the bus: start workers on demand.
    // The job slot keeps the done delegate; the worker calls back into it on its thread.
    bool start_transfer(const hal::IoTransfer& x) noexcept override {
        Job* j = alloc_job();
        if (!j) return false;
        j->done = x.done;
        bool ok = false;
        if (x.bus == hal::BusType::Spi) {
            if (!spi_.is_running()) start_spi(x.bus_index, x.device, 10'000'000);
            hal::SpiRequest req{};
            req.tx_data = x.tx;
            req.rx_data = x.rx;
            req.on_rx_complete = etl::delegate<void(const hal::SpiResult&)>::create<Job, &Job::spi_done>(*j);
            ok = spi_.submit(req);
        } else if (x.bus == hal::BusType::I2c) {
            if (!i2c_.is_running()) i2c_.start("/dev/i2c-1");
            hal::I2cRequest req{};
            req.slave_addr = x.device;
            req.tx_data = x.tx;
            req.rx_data = x.rx;
            req.repeated_start = !x.tx.empty() && !x.rx.empty();
            req.on_rx_complete = etl::delegate<void(const hal::I2cResult&)>::create<Job, &Job::i2c_done>(*j);
            ok = i2c_.submit(req);
        }
        if (!ok) j->in_use.store(false, std::memory_order_release);
        return ok;
    }

    void notify_host() noexcept override { rx_doorbell_.notify(1); }

    int poll_events(int timeout_ms) noexcept override {
        if (!reactor_.is_valid()) return 0;
        std::array<struct epoll_event, MAX_EVENTS> events{};
        const int count = reactor_.wait(events, timeout_ms);
        for (int i = 0; i < count; ++i) {
            const uint64_t tag = events[static_cast<size_t>(i)].data.u64;
            if (tag == FD_TAG_RX_DOORBELL) {
                rx_doorbell_.drain();
            } else if (tag >= FD_TAG_TRIGGER_BASE && tag < FD_TAG_TRIGGER_BASE + num_triggers_) {
                auto& t = triggers_[tag - FD_TAG_TRIGGER_BASE];
                t.monitor.process_events([this, &t](unsigned int, uint64_t ts_ns) {
                    if (engine_) engine_->on_trigger(t.pin, ts_ns);
                });
            }
        }
        return count > 0 ? count : 0;
    }

    void idle_wait() noexcept override { (void)poll_events(100); }

    void on_stop() noexcept override {
        spi_.stop();
        i2c_.stop();
        for (size_t i = 0; i < num_triggers_; ++i) triggers_[i].monitor.close();
        num_triggers_ = 0;
    }

    EventFd& rx_doorbell() noexcept { return rx_doorbell_; }

private:
    // /dev/spidev<bus>.<cs> ([SPEC-LINUX-GPIO-01]); spidev_path_ outlives the worker's open().
    void start_spi(uint8_t bus, uint8_t cs, uint32_t hz) noexcept {
        std::snprintf(spidev_path_, sizeof(spidev_path_), "/dev/spidev%u.%u",
                      static_cast<unsigned>(bus), static_cast<unsigned>(cs));
        spi_.start(spidev_path_, hz ? hz : 10'000'000, 3);
    }

    struct Job {
        LinuxIoBackend* owner{nullptr};
        std::atomic<bool> in_use{false};
        etl::delegate<void(const hal::IoResult&)> done{};

        void spi_done(const hal::SpiResult& r) noexcept {
            hal::IoResult out{};
            out.ok = r.status == hal::SpiStatus::Ok;
            out.transferred = r.transferred_bytes;
            out.duration_us = static_cast<uint32_t>(r.bus_duration_us);
            out.timestamp_ns = r.timestamp_us * 1000ULL;
            finish(out);
        }
        void i2c_done(const hal::I2cResult& r) noexcept {
            hal::IoResult out{};
            out.ok = r.status == hal::I2cStatus::Ok;
            out.transferred = r.transferred_bytes;
            finish(out);
        }
        void finish(const hal::IoResult& out) noexcept {
            auto cb = done;
            in_use.store(false, std::memory_order_release);
            if (cb.is_valid()) cb(out);
        }
    };

    Job* alloc_job() noexcept {
        for (auto& j : jobs_) {
            bool expected = false;
            if (j.in_use.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return &j;
        }
        return nullptr;
    }

    struct Trigger {
        uint32_t pin{0};
        GpiodV2Monitor monitor{};
    };

    static constexpr uint64_t FD_TAG_RX_DOORBELL  = 2;
    static constexpr uint64_t FD_TAG_TRIGGER_BASE = 10;

    hal::IoEngine* engine_{nullptr};
    const char* gpiochip_{"/dev/gpiochip0"};
    char spidev_path_[32]{};
    EpollReactor reactor_{};
    EventFd rx_doorbell_;
    bool rx_registered_{false};
    SpiWorker spi_{};
    I2cWorker i2c_{};
    std::array<Trigger, hal::IoEngine::kMaxChannels> triggers_{};
    size_t num_triggers_{0};
    std::array<Job, hal::IoEngine::kMaxChannels + hal::IoEngine::kBridgeSlots> jobs_{};
};

struct LinuxIoProcessor {
    LinuxIoBackend backend{};
    hal::IoEngine engine{backend};
    LinuxIoProcessor() noexcept { backend.attach(engine); }
};

static LinuxIoProcessor g_linux_io;

void set_linux_gpiochip(const char* path) noexcept { g_linux_io.backend.set_gpiochip(path); }
bool linux_trigger_simulated(uint32_t pin) noexcept { return g_linux_io.backend.trigger_simulated(pin); }
bool linux_simulate_edge(uint32_t pin) noexcept { return g_linux_io.backend.simulate_edge(pin); }

} // namespace abstractx::target

namespace abstractx::hal {

IIoProcessor& get_target_io_processor() noexcept {
    return target::g_linux_io.engine;
}

} // namespace abstractx::hal
