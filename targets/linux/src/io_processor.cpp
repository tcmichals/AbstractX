/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux Target: I/O Processor Epoll Reactor Implementation
 * ------------------------------------------------------------------
 * Concrete IIoProcessor implementation for Linux.
 * Operates an autonomous Auto-DMA engine: when an ISR/trigger fires, the bus
 * is clocked ASAP and the data is forwarded as a 64-byte TLP into the completion ring.
 */

#include "abstractx/hal/io_processor.hpp"
#include "epoll_reactor.hpp"
#include "eventfd.hpp"
#include "spi_worker.hpp"
#include "i2c_worker.hpp"
#include "gpio_gpiod.hpp"
#include "asp_tlp_msg.hpp"
#include "spsc_tlp_ring.hpp"

#include <atomic>
#include <array>
#include <iostream>
#include <thread>
#include <cstring>

namespace abstractx::target {

class LinuxIoProcessor final : public hal::IIoProcessor {
public:
    static constexpr size_t MAX_EVENTS = 16;
    static constexpr size_t MAX_CHANNELS = 4;

    LinuxIoProcessor() noexcept
        : tx_doorbell_(true), rx_doorbell_(true) {}

    ~LinuxIoProcessor() override {
        stop();
    }

    bool configure(const hal::IoProcessorSetup& setup) override {
        setup_ = setup;

        if (!reactor_.is_valid()) {
            return false;
        }

        // Register Egress TX Doorbell (Application Coroutine Domain -> ioProcessor)
        reactor_.add_fd_u64(tx_doorbell_.fd(), EPOLLIN, FD_TAG_TX_DOORBELL);

        // Register Ingress RX Doorbell
        reactor_.add_fd_u64(rx_doorbell_.fd(), EPOLLIN, FD_TAG_RX_DOORBELL);

        // Configure hardware channels
        num_channels_ = 0;
        for (const auto& ch : setup_.channels) {
            if (num_channels_ >= MAX_CHANNELS) break;
            channels_[num_channels_] = ch;

            // Bind physical trigger source
            if (ch.trigger_mode == hal::TriggerMode::GpioEdge) {
                gpiod_monitors_[num_channels_].open_pin_interrupt(
                    "/dev/gpiochip0",
                    ch.trigger_pin,
                    ch.trigger_rising,
                    !ch.trigger_rising
                );
                int gpiod_fd = gpiod_monitors_[num_channels_].get_fd();
                if (gpiod_fd >= 0) {
                    reactor_.add_fd_u64(gpiod_fd, EPOLLIN | EPOLLET, FD_TAG_TRIGGER_BASE + num_channels_);
                }
            }

            // Bind physical bus peripheral
            if (ch.bus_type == hal::BusType::Spi) {
                spi_worker_.start("/dev/spidev0.0", ch.bus_speed_hz, 3);
            } else if (ch.bus_type == hal::BusType::I2c) {
                i2c_worker_.start("/dev/i2c-1");
            }

            num_channels_++;
        }

        configured_ = true;
        return true;
    }

    bool start() override {
        if (!configured_) return false;
        running_.store(true, std::memory_order_release);
        return true;
    }

    void stop() override {
        if (running_.exchange(false, std::memory_order_acq_rel)) {
            tx_doorbell_.notify(1);
            if (worker_thread_.joinable()) {
                worker_thread_.join();
            }
            spi_worker_.stop();
            i2c_worker_.stop();
            for (size_t i = 0; i < num_channels_; ++i) {
                gpiod_monitors_[i].close();
            }
        }
    }

    int step(int timeout_ms = 0) override {
        drain_egress_requests();
        return process_events(timeout_ms);
    }


    void run() override {
        running_.store(true, std::memory_order_release);
        while (running_.load(std::memory_order_acquire)) {
            process_events(100);
        }
    }

    coro::Task<void> run_coroutine() override {
        running_.store(true, std::memory_order_release);
        while (running_.load(std::memory_order_acquire)) {
            drain_egress_requests();
            process_events(0);
            co_await yield_to_dispatcher();
        }
    }

    bool is_running() const noexcept override {
        return running_.load(std::memory_order_acquire);
    }

    bool set_auto_mode(uint8_t channel_id, bool enable) override {
        for (size_t i = 0; i < num_channels_; ++i) {
            if (channels_[i].channel_id == channel_id) {
                channels_[i].auto_mode = enable;
                return true;
            }
        }
        return false;
    }

    EventFd& tx_doorbell() noexcept { return tx_doorbell_; }
    EventFd& rx_doorbell() noexcept { return rx_doorbell_; }
    EpollReactor& reactor() noexcept { return reactor_; }
    SpiWorker& spi_worker() noexcept { return spi_worker_; }

private:
    int process_events(int timeout_ms) noexcept {
        std::array<struct epoll_event, MAX_EVENTS> events{};
        int count = reactor_.wait(events, timeout_ms);
        if (count <= 0) return count;

        for (int i = 0; i < count; ++i) {
            uint64_t tag = events[i].data.u64;
            if (tag == FD_TAG_TX_DOORBELL) {
                tx_doorbell_.drain();
                drain_egress_requests();
            } else if (tag == FD_TAG_RX_DOORBELL) {
                rx_doorbell_.drain();
            } else if (tag >= FD_TAG_TRIGGER_BASE && tag < FD_TAG_TRIGGER_BASE + MAX_CHANNELS) {
                size_t ch_idx = tag - FD_TAG_TRIGGER_BASE;
                handle_hardware_trigger(ch_idx);
            }
        }
        return count;
    }

    void handle_hardware_trigger(size_t ch_idx) noexcept {
        if (ch_idx >= num_channels_) return;
        auto& ch = channels_[ch_idx];

        gpiod_monitors_[ch_idx].process_events([this, &ch](unsigned int /*line*/, uint64_t ts_ns) {
            if (!ch.auto_mode) return; // Auto mode disabled: do not poll ASAP
            last_trigger_ts_ns_ = ts_ns;
            active_auto_ch_ = ch;

            if (ch.bus_type == hal::BusType::Spi) {
                // Prepare command and receive buffer
                size_t tx_n = (ch.tx_len > 8) ? 8 : ch.tx_len;
                std::memcpy(auto_tx_buf_, ch.tx_cmd, tx_n);

                size_t total_xfer = tx_n + ch.rx_len;
                if (total_xfer > sizeof(auto_rx_buf_)) total_xfer = sizeof(auto_rx_buf_);

                hal::SpiRequest req{};
                req.tx_data = std::span<const uint8_t>(auto_tx_buf_, total_xfer);
                req.rx_data = std::span<uint8_t>(auto_rx_buf_, total_xfer);
                req.on_rx_complete = etl::delegate<void(const hal::SpiResult&)>::create<LinuxIoProcessor, &LinuxIoProcessor::on_auto_spi_done>(*this);

                // Read ASAP on hardware worker
                spi_worker_.submit(req);
            }
        });
    }

    void on_auto_spi_done(const hal::SpiResult& res) noexcept {
        if (res.status == hal::SpiStatus::Ok && setup_.ingress_rx_ring) {
            size_t header_skip = active_auto_ch_.tx_len;
            size_t payload_len = active_auto_ch_.rx_len;

            // Format 64B DMA_Stream TLP and forward data immediately
            Tlp64 stream_tlp = Tlp64::make_stream_tx(
                static_cast<Channel>(active_auto_ch_.tlp_channel),
                0,
                std::span<const uint8_t>(auto_rx_buf_ + header_skip, payload_len),
                active_auto_ch_.tlp_tag
            );
            stream_tlp.wire.timestamp_ns = last_trigger_ts_ns_;
            setup_.ingress_rx_ring->push(stream_tlp);
            rx_doorbell_.notify(1);
            if (setup_.on_rx_pushed.is_valid()) {
                setup_.on_rx_pushed();
            }
        }
    }

    bool is_bus_locked(hal::BusType bus) const noexcept {
        for (size_t i = 0; i < num_channels_; ++i) {
            if (channels_[i].bus_type == bus && channels_[i].auto_mode) {
                return true; // Auto/DMA mode active on this bus -> manual R/W blocked!
            }
        }
        return false;
    }

    void drain_egress_requests() noexcept {
        if (!setup_.egress_tx_ring) return;

        Tlp64 tlp{};
        while (setup_.egress_tx_ring->pop(tlp)) {
            // Check for HW Sensor Fusion Configuration / Control TLP (DMA_CFG)
            if (tlp.wire.type == ASP_TLP_TYPE_DMA_CFG) {
                const auto* cfg = reinterpret_cast<const asp_tlp_auto_dma_cfg_t*>(tlp.wire.payload);
                uint8_t status = ASP_STATUS_OK;
                if (cfg->cmd == ASP_DMA_CMD_ENABLE) {
                    set_auto_mode(cfg->channel_id, true);
                } else if (cfg->cmd == ASP_DMA_CMD_DISABLE) {
                    set_auto_mode(cfg->channel_id, false);
                } else if (cfg->cmd == ASP_DMA_CMD_SETUP) {
                    if (cfg->channel_id < num_channels_) {
                        auto& ch = channels_[cfg->channel_id];
                        ch.trigger_pin = cfg->trigger_pin;
                        ch.trigger_rising = (cfg->trigger_edge == 1 || cfg->trigger_edge == 3);
                        ch.bus_type = static_cast<hal::BusType>(cfg->bus_type);
                        ch.bus_index = cfg->bus_id;
                        ch.tx_len = cfg->tx_len;
                        ch.rx_len = cfg->rx_len;
                        std::memcpy(ch.tx_cmd, cfg->tx_cmd, 8);
                        ch.tlp_channel = cfg->tlp_channel;
                        ch.tlp_tag = cfg->tlp_tag;
                    }
                }

                // Return HW Sensor Fusion completion status
                if (setup_.ingress_rx_ring) {
                    Tlp64 cpl = Tlp64::make_hw_fusion_cpl(tlp.tag(), status, cfg->cmd, cfg->channel_id);
                    setup_.ingress_rx_ring->push(cpl);
                    rx_doorbell_.notify(1);
                    if (setup_.on_rx_pushed.is_valid()) setup_.on_rx_pushed();
                }
                continue;
            }

            uint8_t ch = static_cast<uint8_t>(tlp.channel());
            uint32_t addr = tlp.target_address();
            if (ch == ASP_CHANNEL_SPI_BRIDGE || ch == 0x01 || (addr & 0xFFFFFF00) == ASP_ADDR_SPI_BASE) {

                // HARDWARE INVARIANT: Once in Auto/DMA mode, manual reads and writes CANNOT happen!
                // To perform manual R/W, app must first turn off auto/DMA mode.
                if (is_bus_locked(hal::BusType::Spi)) {
                    if (setup_.ingress_rx_ring) {
                        Tlp64 err_cpl = Tlp64::make_spi_cpl(
                            tlp.tag(),
                            ASP_STATUS_BUS_LOCKED, // Bus locked exclusively by HW Sensor Fusion
                            {},
                            0,
                            0,
                            tlp.channel()
                        );
                        setup_.ingress_rx_ring->push(err_cpl);
                        rx_doorbell_.notify(1);
                        if (setup_.on_rx_pushed.is_valid()) setup_.on_rx_pushed();
                    }
                    continue;
                }

                TlpSpiView spi_view(tlp);
                auto tx = spi_view.tx_data();
                const auto* hdr = spi_view.req_header();
                size_t rx_len = hdr ? hdr->rx_len : 0;

                pending_spi_tag_ = tlp.tag();
                pending_spi_ch_  = tlp.channel();

                hal::SpiRequest req{};
                req.tx_data = tx;
                req.rx_data = std::span<uint8_t>(bridge_rx_buf_, rx_len);
                req.on_rx_complete = etl::delegate<void(const hal::SpiResult&)>::create<LinuxIoProcessor, &LinuxIoProcessor::on_bridge_spi_done>(*this);

                spi_worker_.submit(req);
            }
        }
    }

    void on_bridge_spi_done(const hal::SpiResult& res) noexcept {
        if (setup_.ingress_rx_ring) {
            Tlp64 cpl = Tlp64::make_spi_cpl(
                pending_spi_tag_,
                (res.status == hal::SpiStatus::Ok) ? 0 : 1,
                std::span<const uint8_t>(bridge_rx_buf_, res.transferred_bytes),
                res.bus_duration_us,
                res.timestamp_us * 1000ULL,
                pending_spi_ch_
            );
            setup_.ingress_rx_ring->push(cpl);
            rx_doorbell_.notify(1);
            if (setup_.on_rx_pushed.is_valid()) {
                setup_.on_rx_pushed();
            }
        }
    }

private:
    static constexpr uint64_t FD_TAG_TX_DOORBELL   = 1;
    static constexpr uint64_t FD_TAG_RX_DOORBELL   = 2;
    static constexpr uint64_t FD_TAG_TRIGGER_BASE  = 10;

    hal::IoProcessorSetup setup_{};
    bool configured_{false};
    std::atomic<bool> running_{false};
    std::thread worker_thread_{};

    EpollReactor reactor_{};
    EventFd      tx_doorbell_;
    EventFd      rx_doorbell_;

    // Peripherals
    SpiWorker      spi_worker_{};
    I2cWorker      i2c_worker_{};

    size_t num_channels_{0};
    std::array<hal::AutoChannelConfig, MAX_CHANNELS> channels_{};
    std::array<GpiodV2Monitor, MAX_CHANNELS> gpiod_monitors_{};

    hal::AutoChannelConfig active_auto_ch_{};
    uint64_t last_trigger_ts_ns_{0};
    alignas(4) uint8_t auto_tx_buf_[32]{};
    alignas(4) uint8_t auto_rx_buf_[64]{};

    uint8_t pending_spi_tag_{0};
    Channel pending_spi_ch_{Channel::Control};
    alignas(4) uint8_t bridge_rx_buf_[64]{};
};

static LinuxIoProcessor g_linux_io_processor;

LinuxIoProcessor& get_io_processor() noexcept {
    return g_linux_io_processor;
}

} // namespace abstractx::target

namespace abstractx::hal {

IIoProcessor& get_target_io_processor() noexcept {
    return target::get_io_processor();
}

} // namespace abstractx::hal
