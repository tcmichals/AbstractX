/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E907: I/O Processor Implementation
 * --------------------------------------------------------------
 * Unified IIoProcessor IP Implementation for Allwinner E907 RISC-V Coprocessor.
 * Operates an autonomous Sensor HW Fusion Engine:
 * - Pin DRDY (PLIC IRQ 82) rising-edge ISR triggers simultaneous SPI0 DMA read ASAP
 * - DMA completion (PLIC IRQ 64) pushes 64-byte DMA_Stream TLP into completion ring
 * - Signals MSGBOX hardware doorbell (0x03003000) to awaken Cortex-A55 Linux Host
 * - Enforces Bus Lockout Invariant: rejects manual SPI access with ASP_STATUS_BUS_LOCKED
 * - Idles E907 core in low-power WFI
 */

#include "abstractx/hal/io_processor.hpp"
#include "hal/spi.hpp"
#include "hal/i2c.hpp"
#include "hal/pio.hpp"
#include "hal/dma.hpp"
#include "hal/msgbox.hpp"
#include "hal/timer.hpp"
#include "asp_tlp_msg.hpp"
#include "spsc_tlp_ring.hpp"

#include <atomic>
#include <array>
#include <cstring>

namespace abstractx::target {

class E907IoProcessor final : public hal::IIoProcessor {
public:
    static constexpr size_t MAX_CHANNELS = 4;

    E907IoProcessor() noexcept = default;
    ~E907IoProcessor() override { stop(); }

    bool configure(const hal::IoProcessorSetup& setup) override {
        setup_ = setup;
        num_channels_ = 0;

        // Initialize underlying DMA controller & MSGBOX
        ::hal::DmaController::init();
        ::hal::MsgBox::init();

        for (const auto& ch : setup_.channels) {
            if (num_channels_ >= MAX_CHANNELS) break;
            channels_[num_channels_] = ch;

            if (ch.bus_type == hal::BusType::Spi) {
                uint32_t speed = ch.bus_speed_hz ? ch.bus_speed_hz : 20'000'000;
                fc::hal::Spi0::init(speed);
            }

            if (ch.bus_type == hal::BusType::I2c) {
                hal::I2cConfig cfg{};
                cfg.frequency_hz = ch.bus_speed_hz ? ch.bus_speed_hz : 400'000;
                i2c_.init(cfg);
            }

            num_channels_++;
        }

        active_instance_ = this;
        configured_ = true;
        return true;
    }

    bool start() override {
        if (!configured_) return false;
        running_.store(true, std::memory_order_release);
        return true;
    }

    void stop() override {
        running_.store(false, std::memory_order_release);
    }

    int step(int timeout_ms = 0) override {
        (void)timeout_ms;
        drain_egress_requests();
        return 0;
    }

    void run() override {
        running_.store(true, std::memory_order_release);
        while (running_.load(std::memory_order_acquire)) {
            drain_egress_requests();
            __asm__ volatile("wfi");
        }
    }

    coro::Task<void> run_coroutine() override {
        running_.store(true, std::memory_order_release);
        while (running_.load(std::memory_order_acquire)) {
            drain_egress_requests();
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

    void handle_gpio_drdy_from_isr() noexcept {
        for (size_t i = 0; i < num_channels_; ++i) {
            auto& ch = channels_[i];
            if (ch.auto_mode && ch.trigger_mode == hal::TriggerMode::GpioEdge) {
                last_trigger_ts_ns_ = ::hal::Timer::get_time_ns();
                active_auto_ch_ = ch;

                if (ch.bus_type == hal::BusType::Spi) {
                    size_t tx_n = (ch.tx_len > 8) ? 8 : ch.tx_len;
                    std::memcpy(auto_tx_buf_, ch.tx_cmd, tx_n);
                    size_t total_xfer = tx_n + ch.rx_len;
                    if (total_xfer > sizeof(auto_rx_buf_)) total_xfer = sizeof(auto_rx_buf_);

                    // Trigger instant DMA burst read directly from ISR!
                    fc::hal::Spi0::start_dma_transfer(
                        1 /* CS1: IMU */,
                        auto_tx_buf_,
                        auto_rx_buf_,
                        total_xfer,
                        etl::delegate<void(bool)>::create<E907IoProcessor, &E907IoProcessor::on_auto_spi_dma_done>(*this)
                    );
                }
            }
        }
    }

    void on_auto_spi_dma_done(bool success) noexcept {
        (void)success;
        if (setup_.ingress_rx_ring) {
            size_t header_skip = active_auto_ch_.tx_len;
            size_t payload_len = active_auto_ch_.rx_len;

            Tlp64 stream_tlp = Tlp64::make_stream_tx(
                static_cast<Channel>(active_auto_ch_.tlp_channel),
                0,
                std::span<const uint8_t>(auto_rx_buf_ + header_skip, payload_len),
                active_auto_ch_.tlp_tag
            );
            stream_tlp.wire.timestamp_ns = last_trigger_ts_ns_;
            setup_.ingress_rx_ring->push(stream_tlp);

            // Ring MSGBOX doorbell to awaken Linux Host
            ::hal::MsgBox::send(::hal::MsgBox::Channel::Channel0, 0x01);

            if (setup_.on_rx_pushed.is_valid()) {
                setup_.on_rx_pushed();
            }
        }
    }

    static inline E907IoProcessor* instance() noexcept {
        return active_instance_;
    }

private:
    bool is_bus_locked(hal::BusType bus) const noexcept {
        for (size_t i = 0; i < num_channels_; ++i) {
            if (channels_[i].bus_type == bus && channels_[i].auto_mode) {
                return true;
            }
        }
        return false;
    }

    void drain_egress_requests() noexcept {
        if (!setup_.egress_tx_ring) return;

        Tlp64 tlp{};
        while (setup_.egress_tx_ring->pop(tlp)) {
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

                if (setup_.ingress_rx_ring) {
                    Tlp64 cpl = Tlp64::make_hw_fusion_cpl(tlp.tag(), status, cfg->cmd, cfg->channel_id);
                    setup_.ingress_rx_ring->push(cpl);
                    ::hal::MsgBox::send(::hal::MsgBox::Channel::Channel0, 0x01);
                    if (setup_.on_rx_pushed.is_valid()) setup_.on_rx_pushed();
                }
                continue;
            }

            uint8_t ch = static_cast<uint8_t>(tlp.channel());
            if (ch == ASP_CHANNEL_SPI_BRIDGE || ch == 0x01) {
                // HARDWARE INVARIANT: Once in Auto/DMA mode, manual reads and writes CANNOT happen!
                if (is_bus_locked(hal::BusType::Spi)) {
                    if (setup_.ingress_rx_ring) {
                        Tlp64 err_cpl = Tlp64::make_spi_cpl(
                            tlp.tag(),
                            ASP_STATUS_BUS_LOCKED,
                            {},
                            0,
                            0,
                            tlp.channel()
                        );
                        setup_.ingress_rx_ring->push(err_cpl);
                        ::hal::MsgBox::send(::hal::MsgBox::Channel::Channel0, 0x01);
                        if (setup_.on_rx_pushed.is_valid()) setup_.on_rx_pushed();
                    }
                    continue;
                }

                // Execute manual transfer via DMA
                TlpSpiView spi_view(tlp);
                auto tx = spi_view.tx_data();
                const auto* hdr = spi_view.req_header();
                size_t rx_len = hdr ? hdr->rx_len : 0;
                size_t xfer_len = tx.empty() ? rx_len : tx.size();

                fc::hal::Spi0::start_dma_transfer(
                    1 /* CS1 */,
                    tx.data(),
                    bridge_rx_buf_,
                    xfer_len,
                    {}
                );

                // Wait for DMA completion in low-power WFI
                while (fc::hal::Spi0::is_busy()) {
                    __asm__ volatile("wfi");
                }

                if (setup_.ingress_rx_ring) {
                    Tlp64 cpl = Tlp64::make_spi_cpl(
                        tlp.tag(),
                        0,
                        std::span<const uint8_t>(bridge_rx_buf_, rx_len),
                        0,
                        0,
                        tlp.channel()
                    );
                    setup_.ingress_rx_ring->push(cpl);
                    ::hal::MsgBox::send(::hal::MsgBox::Channel::Channel0, 0x01);
                    if (setup_.on_rx_pushed.is_valid()) setup_.on_rx_pushed();
                }
            } else if (ch == ASP_CHANNEL_I2C_BRIDGE || ch == 0x07) {
                if (is_bus_locked(hal::BusType::I2c)) {
                    if (setup_.ingress_rx_ring) {
                        Tlp64 err_cpl = Tlp64::make_i2c_cpl(
                            tlp.tag(),
                            ASP_STATUS_BUS_LOCKED,
                            {},
                            0,
                            0,
                            tlp.channel()
                        );
                        setup_.ingress_rx_ring->push(err_cpl);
                        ::hal::MsgBox::send(::hal::MsgBox::Channel::Channel0, 0x01);
                        if (setup_.on_rx_pushed.is_valid()) setup_.on_rx_pushed();
                    }
                    continue;
                }

                TlpI2cView i2c_view(tlp);
                const auto* hdr = i2c_view.req_header();
                uint8_t slave_addr = hdr ? hdr->slave_addr : 0;
                uint8_t rx_len = hdr ? hdr->rx_len : 0;
                if (rx_len > sizeof(bridge_rx_buf_)) rx_len = sizeof(bridge_rx_buf_);

                bool success = false;
                uint8_t reg_offset = hdr ? hdr->reg_offset : 0;
                bool use_reg = (hdr && (hdr->sub_flags & ASP_I2C_FLAG_USE_REG));
                auto tx = i2c_view.tx_data();

                if (use_reg && rx_len > 0) {
                    success = i2c_.write_read_sync(slave_addr, std::span<const uint8_t>(&reg_offset, 1), std::span<uint8_t>(bridge_rx_buf_, rx_len));
                } else if (!tx.empty() && rx_len > 0) {
                    success = i2c_.write_read_sync(slave_addr, tx, std::span<uint8_t>(bridge_rx_buf_, rx_len));
                } else if (rx_len > 0) {
                    success = i2c_.read_sync(slave_addr, std::span<uint8_t>(bridge_rx_buf_, rx_len));
                } else if (!tx.empty()) {
                    success = i2c_.write_sync(slave_addr, tx);
                } else {
                    success = true;
                }

                if (setup_.ingress_rx_ring) {
                    Tlp64 cpl = Tlp64::make_i2c_cpl(
                        tlp.tag(),
                        success ? 0 : 1,
                        std::span<const uint8_t>(bridge_rx_buf_, rx_len),
                        0,
                        0,
                        tlp.channel()
                    );
                    setup_.ingress_rx_ring->push(cpl);
                    ::hal::MsgBox::send(::hal::MsgBox::Channel::Channel0, 0x01);
                    if (setup_.on_rx_pushed.is_valid()) setup_.on_rx_pushed();
                }
            } else if (ch == ASP_CHANNEL_GPIO_BRIDGE || ch == 0x08) {
                TlpGpioView gpio_view(tlp);
                const auto* req = gpio_view.req_header();
                if (req) {
                    switch (req->cmd) {
                    case ASP_GPIO_CMD_SET:
                        PC_DATA_REG |= (req->mask ? req->mask : (1U << req->pin));
                        break;
                    case ASP_GPIO_CMD_CLR:
                        PC_DATA_REG &= ~(req->mask ? req->mask : (1U << req->pin));
                        break;
                    case ASP_GPIO_CMD_XOR:
                        PC_DATA_REG ^= (req->mask ? req->mask : (1U << req->pin));
                        break;
                    case ASP_GPIO_CMD_WRITE:
                        if (req->value) {
                            PC_DATA_REG |= (1U << req->pin);
                        } else {
                            PC_DATA_REG &= ~(1U << req->pin);
                        }
                        break;
                    case ASP_GPIO_CMD_READ: {
                        bool lvl = (PC_DATA_REG & (1U << req->pin)) != 0;
                        if (setup_.ingress_rx_ring) {
                            Tlp64 cpl = Tlp64::make_gpio_cpl(tlp.tag(), 0, req->pin, lvl ? 1 : 0);
                            setup_.ingress_rx_ring->push(cpl);
                            ::hal::MsgBox::send(::hal::MsgBox::Channel::Channel0, 0x01);
                            if (setup_.on_rx_pushed.is_valid()) setup_.on_rx_pushed();
                        }
                        break;
                    }
                    default:
                        break;
                    }
                }
            }
        }
    }

    hal::IoProcessorSetup setup_{};
    bool configured_{false};
    std::atomic<bool> running_{false};

    hal::E907I2c i2c_{};
    size_t num_channels_{0};
    std::array<hal::AutoChannelConfig, MAX_CHANNELS> channels_{};

    hal::AutoChannelConfig active_auto_ch_{};
    uint64_t last_trigger_ts_ns_{0};
    alignas(4) uint8_t auto_tx_buf_[32]{};
    alignas(4) uint8_t auto_rx_buf_[64]{};
    alignas(4) uint8_t bridge_rx_buf_[64]{};

    static inline E907IoProcessor* active_instance_{nullptr};
};

static E907IoProcessor g_e907_io_processor;

E907IoProcessor& get_e907_io_processor() noexcept {
    return g_e907_io_processor;
}

} // namespace abstractx::target

namespace abstractx::hal {

IIoProcessor& get_target_io_processor() noexcept {
    return target::get_e907_io_processor();
}

} // namespace abstractx::hal

// Hook GPIO DRDY ISR into E907IoProcessor
extern "C" __attribute__((section(".fastcode")))
void fc_gpio_drdy_isr() noexcept {
    auto* proc = abstractx::target::E907IoProcessor::instance();
    if (proc) {
        proc->handle_gpio_drdy_from_isr();
    }
}
