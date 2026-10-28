/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Raspberry Pi Pico 2 W (RP2350): I/O processor back end (Core 0).
 * The shared IoEngine ([SPEC-ARCH-13]) decodes TLPs; this file maps its transfers onto the
 * PicoSpi DMA request queue and PicoI2c, its triggers onto GPIO edge interrupts, and its host
 * notification onto the SIO FIFO doorbell to Core 1.
 */

#include "abstractx/hal/io_engine.hpp"
#include "abstractx_pico.hpp"

#ifdef PICO_ON_DEVICE
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#endif

#include <array>
#include <atomic>

namespace abstractx::target {

// @impl [SPEC-ARCH-13] docs/DESIGN_SPECIFICATION.md
class PicoIoBackend final : public hal::IIoBackend {
public:
    PicoIoBackend() noexcept {
        for (auto& j : jobs_) j.owner = this;
    }

    void attach(hal::IoEngine& engine) noexcept {
        engine_ = &engine;
        active_ = this;
    }

    bool init_channel(const hal::AutoChannelConfig& ch) noexcept override {
#ifdef PICO_ON_DEVICE
        if (ch.trigger_mode == hal::TriggerMode::GpioEdge) {
            gpio_init(ch.trigger_pin);
            gpio_set_dir(ch.trigger_pin, GPIO_IN);
            const uint32_t mask = ch.trigger_rising ? GPIO_IRQ_EDGE_RISE : GPIO_IRQ_EDGE_FALL;
            gpio_set_irq_enabled_with_callback(ch.trigger_pin, mask, true, &PicoIoBackend::gpio_irq);
        }
#endif
        if (ch.bus_type == hal::BusType::Spi) {
            hal::SpiConfig cfg{};
            cfg.frequency_hz = ch.bus_speed_hz ? ch.bus_speed_hz : 10'000'000;
            cfg.mode = hal::SpiMode::Mode3;
            spi_.init(cfg);
            spi_ready_ = true;
        } else if (ch.bus_type == hal::BusType::I2c) {
            hal::I2cConfig cfg{};
            cfg.frequency_hz = ch.bus_speed_hz ? ch.bus_speed_hz : 400'000;
            i2c_.init(cfg);
            i2c_ready_ = true;
        }
        return true;
    }

    // SPI: queued on the PicoSpi DMA driver, completed from the DMA ISR. I2C: PicoI2c only has
    // blocking calls, so it completes before returning ([SPEC-ARCH-13] 5).
    bool start_transfer(const hal::IoTransfer& x) noexcept override {
        if (x.bus == hal::BusType::Spi) {
            if (!spi_ready_) {
                hal::SpiConfig cfg{};
                cfg.frequency_hz = 10'000'000;
                cfg.mode = hal::SpiMode::Mode3;
                spi_.init(cfg);
                spi_ready_ = true;
            }
            Job* j = alloc_job();
            if (!j) return false;
            j->done = x.done;
            hal::SpiRequest req{};
            req.tx_data = x.tx;
            req.rx_data = x.rx;
            req.callback = etl::delegate<void(const hal::SpiResult&)>::create<Job, &Job::spi_done>(*j);
            if (!spi_.submit_request(req)) {
                j->in_use.store(false, std::memory_order_release);
                return false;
            }
            return true;
        }
        if (x.bus == hal::BusType::I2c) {
            if (!i2c_ready_) {
                hal::I2cConfig cfg{};
                cfg.frequency_hz = 400'000;
                i2c_.init(cfg);
                i2c_ready_ = true;
            }
            bool ok = true;
            if (!x.tx.empty() && !x.rx.empty()) ok = i2c_.write_read_sync(x.device, x.tx, x.rx);
            else if (!x.rx.empty())             ok = i2c_.read_sync(x.device, x.rx);
            else if (!x.tx.empty())             ok = i2c_.write_sync(x.device, x.tx);
            hal::IoResult r{};
            r.ok = ok;
            r.transferred = ok ? x.rx.size() : 0;
            x.done(r);
            return true;
        }
        return false;
    }

    bool gpio(const asp_tlp_gpio_req_header_t& req, uint8_t& level) noexcept override {
        const uint32_t bits = req.mask ? req.mask : (1U << req.pin);   // pin < 32 checked by the engine
        switch (req.cmd) {
        case ASP_GPIO_CMD_CONFIG:
            hal::get_gpio().configure_pin(req.pin, static_cast<hal::PinMode>(req.mode),
                                          static_cast<hal::PinPull>(req.pull_edge));
            return true;
#ifdef PICO_ON_DEVICE
        case ASP_GPIO_CMD_SET: sio_hw->gpio_set = bits; return true;
        case ASP_GPIO_CMD_CLR: sio_hw->gpio_clr = bits; return true;
        case ASP_GPIO_CMD_XOR: sio_hw->gpio_togl = bits; return true;
#else
        case ASP_GPIO_CMD_SET:
        case ASP_GPIO_CMD_CLR:
        case ASP_GPIO_CMD_XOR: (void)bits; return true;
#endif
        case ASP_GPIO_CMD_WRITE:
            hal::get_gpio().write_pin(req.pin, req.value != 0);
            return true;
        case ASP_GPIO_CMD_READ:
            level = hal::get_gpio().read_pin(req.pin) ? 1 : 0;
            return true;
        default:
            return false;
        }
    }

    // Virtual BAR: the onboard LED is GPIO 25.
    uint32_t read_reg(uint32_t addr) noexcept override {
        if (addr >= bar::LedBase && addr < bar::LedBase + 0x1000) return hal::get_gpio().read_pin(25) ? 1U : 0U;
        return 0;
    }
    void write_reg(uint32_t addr, uint32_t value) noexcept override {
        if (addr >= bar::LedBase && addr < bar::LedBase + 0x1000) hal::get_gpio().write_pin(25, value != 0);
    }

    void notify_host() noexcept override {
#ifdef PICO_ON_DEVICE
        if (sio_hw->fifo_st & SIO_FIFO_ST_RDY_BITS) sio_hw->fifo_wr = 0x01;   // Core 1 doorbell
#endif
    }

    void idle_wait() noexcept override {
#ifdef PICO_ON_DEVICE
        __asm__ volatile("wfe");
#endif
    }

    uint64_t now_ns() noexcept override {
#ifdef PICO_ON_DEVICE
        return time_us_64() * 1000ULL;
#else
        return 0;
#endif
    }

private:
    struct Job {
        PicoIoBackend* owner{nullptr};
        std::atomic<bool> in_use{false};
        etl::delegate<void(const hal::IoResult&)> done{};

        void spi_done(const hal::SpiResult& r) noexcept {
            hal::IoResult out{};
            out.ok = r.status == hal::SpiStatus::Ok;
            out.transferred = r.transferred_bytes;
            out.timestamp_ns = r.timestamp_us * 1000ULL;
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

#ifdef PICO_ON_DEVICE
    static void gpio_irq(uint gpio, uint32_t events) {
        (void)events;
        if (active_ && active_->engine_) active_->engine_->on_trigger(gpio, time_us_64() * 1000ULL);
    }
#endif

    hal::IoEngine* engine_{nullptr};
    hal::PicoSpi spi_{};
    hal::PicoI2c i2c_{};
    bool spi_ready_{false};
    bool i2c_ready_{false};
    std::array<Job, hal::IoEngine::kMaxChannels + hal::IoEngine::kBridgeSlots> jobs_{};
    static inline PicoIoBackend* active_{nullptr};
};

struct PicoIoProcessor {
    PicoIoBackend backend{};
    hal::IoEngine engine{backend};
    PicoIoProcessor() noexcept { backend.attach(engine); }
};

static PicoIoProcessor g_pico_io;

} // namespace abstractx::target

namespace abstractx::hal {

IIoProcessor& get_target_io_processor() noexcept {
    return target::g_pico_io.engine;
}

} // namespace abstractx::hal
