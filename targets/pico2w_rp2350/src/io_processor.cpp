/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX RP2350 / Pico 2 Target: I/O Processor Implementation
 * ---------------------------------------------------------------
 * Unified IIoProcessor IP Implementation for Raspberry Pi Pico 2 W (RP2350).
 * Operates an autonomous Sensor HW Fusion Engine on Core 0:
 * - Pin 3 (GP20) DRDY rising-edge ISR triggers simultaneous SPI1 DMA read ASAP
 * - DMA completion ISR pushes 64-byte DMA_Stream TLP into completion ring
 * - Signals SIO FIFO doorbell (sio_hw->fifo_wr = 1) to awaken Core 1
 * - Enforces Bus Lockout Invariant: rejects manual SPI access with ASP_STATUS_BUS_LOCKED
 * - Idles Core 0 in low-power WFE
 */

#include "abstractx/hal/io_processor.hpp"
#include "abstractx_pico.hpp"
#include "asp_tlp_msg.hpp"
#include "spsc_tlp_ring.hpp"

#ifdef PICO_ON_DEVICE
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/sio.h"
#endif

#include <atomic>
#include <array>
#include <cstring>

namespace abstractx::target {

class PicoIoProcessor final : public hal::IIoProcessor {
public:
    static constexpr size_t MAX_CHANNELS = 4;

    PicoIoProcessor() noexcept = default;
    ~PicoIoProcessor() override { stop(); }

    bool configure(const hal::IoProcessorSetup& setup) override {
        setup_ = setup;
        num_channels_ = 0;

        for (const auto& ch : setup_.channels) {
            if (num_channels_ >= MAX_CHANNELS) break;
            channels_[num_channels_] = ch;

            // Bind physical trigger source (Pin 3 / GP20 DRDY)
            if (ch.trigger_mode == hal::TriggerMode::GpioEdge) {
#ifdef PICO_ON_DEVICE
                gpio_init(ch.trigger_pin);
                gpio_set_dir(ch.trigger_pin, GPIO_IN);
                uint32_t event_mask = 0;
                if (ch.trigger_rising) event_mask |= GPIO_IRQ_EDGE_RISE;
                else event_mask |= GPIO_IRQ_EDGE_FALL;

                gpio_set_irq_enabled_with_callback(
                    ch.trigger_pin,
                    event_mask,
                    true,
                    &PicoIoProcessor::gpio_irq_forwarder
                );
#endif
            }

            // Bind physical SPI peripheral
            if (ch.bus_type == hal::BusType::Spi) {
                hal::SpiConfig cfg{};
                cfg.frequency_hz = ch.bus_speed_hz ? ch.bus_speed_hz : 10'000'000;
                cfg.mode = hal::SpiMode::Mode3;
                spi_.init(cfg);
            }

            // Bind physical I2C peripheral
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
#ifdef PICO_ON_DEVICE
            __asm__ volatile("wfe");
#endif
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

    void handle_gpio_trigger_from_isr(uint gpio, uint32_t events) noexcept {
        (void)events;
        for (size_t i = 0; i < num_channels_; ++i) {
            auto& ch = channels_[i];
            if (ch.trigger_pin == gpio) {
                if (!ch.auto_mode) return; // Auto mode disabled: do not poll ASAP

#ifdef PICO_ON_DEVICE
                uint64_t ts_ns = time_us_64() * 1000ULL;
#else
                uint64_t ts_ns = 0;
#endif
                last_trigger_ts_ns_ = ts_ns;
                active_auto_ch_ = ch;

                if (ch.bus_type == hal::BusType::Spi) {
                    size_t tx_n = (ch.tx_len > 8) ? 8 : ch.tx_len;
                    std::memcpy(auto_tx_buf_, ch.tx_cmd, tx_n);
                    size_t total_xfer = tx_n + ch.rx_len;
                    if (total_xfer > sizeof(auto_rx_buf_)) total_xfer = sizeof(auto_rx_buf_);

                    // Trigger instant DMA burst read directly from ISR!
                    spi_.start_dma_transfer_raw(auto_tx_buf_, auto_rx_buf_, total_xfer);
                }
            }
        }
    }

    void on_auto_spi_dma_done() noexcept {
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

#ifdef PICO_ON_DEVICE
            // Ring SIO FIFO doorbell to awaken Core 1
            sio_hw->fifo_wr = 0x01;
#endif
            if (setup_.on_rx_pushed.is_valid()) {
                setup_.on_rx_pushed();
            }
        }
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
#ifdef PICO_ON_DEVICE
                    sio_hw->fifo_wr = 0x01;
#endif
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
#ifdef PICO_ON_DEVICE
                        sio_hw->fifo_wr = 0x01;
#endif
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

                spi_.start_dma_transfer_raw(tx.data(), bridge_rx_buf_, xfer_len);

                // Wait for DMA completion interrupt in low-power WFE
                while (spi_.is_hardware_busy()) {
#ifdef PICO_ON_DEVICE
                    __asm__ volatile("wfe");
#endif
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
#ifdef PICO_ON_DEVICE
                    sio_hw->fifo_wr = 0x01;
#endif
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
#ifdef PICO_ON_DEVICE
                        sio_hw->fifo_wr = 0x01;
#endif
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
#ifdef PICO_ON_DEVICE
                    sio_hw->fifo_wr = 0x01;
#endif
                    if (setup_.on_rx_pushed.is_valid()) setup_.on_rx_pushed();
                }
            } else if (ch == ASP_CHANNEL_GPIO_BRIDGE || ch == 0x08) {
                TlpGpioView gpio_view(tlp);
                const auto* req = gpio_view.req_header();
                if (req) {
                    switch (req->cmd) {
                    case ASP_GPIO_CMD_CONFIG:
                        hal::get_gpio().configure_pin(req->pin, static_cast<hal::PinMode>(req->mode), static_cast<hal::PinPull>(req->pull_edge));
                        break;
                    case ASP_GPIO_CMD_SET:
#ifdef PICO_ON_DEVICE
                        sio_hw->gpio_set = req->mask ? req->mask : (1U << req->pin);
#endif
                        break;
                    case ASP_GPIO_CMD_CLR:
#ifdef PICO_ON_DEVICE
                        sio_hw->gpio_clr = req->mask ? req->mask : (1U << req->pin);
#endif
                        break;
                    case ASP_GPIO_CMD_XOR:
#ifdef PICO_ON_DEVICE
                        sio_hw->gpio_togl = req->mask ? req->mask : (1U << req->pin);
#endif
                        break;
                    case ASP_GPIO_CMD_WRITE:
                        hal::get_gpio().write_pin(req->pin, req->value != 0);
                        break;
                    case ASP_GPIO_CMD_READ: {
                        bool lvl = hal::get_gpio().read_pin(req->pin);
                        if (setup_.ingress_rx_ring) {
                            Tlp64 cpl = Tlp64::make_gpio_cpl(tlp.tag(), 0, req->pin, lvl ? 1 : 0);
                            setup_.ingress_rx_ring->push(cpl);
#ifdef PICO_ON_DEVICE
                            sio_hw->fifo_wr = 0x01;
#endif
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

#ifdef PICO_ON_DEVICE
    static void gpio_irq_forwarder(uint gpio, uint32_t events) {
        if (active_instance_) {
            active_instance_->handle_gpio_trigger_from_isr(gpio, events);
        }
    }
#endif

    hal::IoProcessorSetup setup_{};
    bool configured_{false};
    std::atomic<bool> running_{false};

    hal::PicoSpi spi_{};
    hal::PicoI2c i2c_{};
    size_t num_channels_{0};
    std::array<hal::AutoChannelConfig, MAX_CHANNELS> channels_{};

    hal::AutoChannelConfig active_auto_ch_{};
    uint64_t last_trigger_ts_ns_{0};
    alignas(4) uint8_t auto_tx_buf_[32]{};
    alignas(4) uint8_t auto_rx_buf_[64]{};
    alignas(4) uint8_t bridge_rx_buf_[64]{};

    static inline PicoIoProcessor* active_instance_{nullptr};
};

static PicoIoProcessor g_pico_io_processor;

PicoIoProcessor& get_pico_io_processor() noexcept {
    return g_pico_io_processor;
}

} // namespace abstractx::target

namespace abstractx::hal {

IIoProcessor& get_target_io_processor() noexcept {
    return target::get_pico_io_processor();
}

} // namespace abstractx::hal
