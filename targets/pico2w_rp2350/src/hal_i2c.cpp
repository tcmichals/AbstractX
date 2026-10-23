/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Pico 2 / Pico 2 W (RP2350) I2C HAL Implementation
 * -----------------------------------------------------------
 * Hardware I2C0 driver on GP4 (SDA) and GP5 (SCL).
 * Fast-mode 400 kHz with repeated-start burst support.
 */

#include "abstractx_pico.hpp"

#ifdef PICO_ON_DEVICE
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/time.h"
#endif

namespace abstractx::hal {

bool PicoI2c::init(const I2cConfig& config) {
    config_ = config;

#ifdef PICO_ON_DEVICE
    uint32_t freq = config.frequency_hz ? config.frequency_hz : 400'000;
    i2c_init(i2c0, freq);

    // GP4 = SDA, GP5 = SCL
    gpio_set_function(4, GPIO_FUNC_I2C);
    gpio_set_function(5, GPIO_FUNC_I2C);
    gpio_pull_up(4);
    gpio_pull_up(5);

    // @impl [SPEC-PICO-HAL-01] targets/pico2w_rp2350/SPECIFICATION.md
    // Async transfers run from I2C0_IRQ; install it once here, not per request.
    i2c_get_hw(i2c0)->intr_mask = 0;
    irq_set_exclusive_handler(I2C0_IRQ, i2c0_irq_handler);
    irq_set_enabled(I2C0_IRQ, true);
#endif

    return true;
}

bool PicoI2c::set_frequency(uint32_t frequency_hz) {
    config_.frequency_hz = frequency_hz;
#ifdef PICO_ON_DEVICE
    i2c_set_baudrate(i2c0, frequency_hz);
#endif
    return true;
}

bool PicoI2c::write_read_sync(uint8_t slave_addr,
                             std::span<const uint8_t> tx_data,
                             std::span<uint8_t> rx_data) {
#ifdef PICO_ON_DEVICE
    if (!tx_data.empty()) {
        // Repeated-start: nostop = true
        int res = i2c_write_blocking(i2c0, slave_addr, tx_data.data(), tx_data.size(), true);
        if (res < 0) return false;
    }
    if (!rx_data.empty()) {
        // Emit STOP at end of read: nostop = false
        int res = i2c_read_blocking(i2c0, slave_addr, rx_data.data(), rx_data.size(), false);
        if (res < 0) return false;
    }
    return true;
#else
    (void)slave_addr;
    (void)tx_data;
    for (size_t i = 0; i < rx_data.size(); ++i) {
        rx_data[i] = static_cast<uint8_t>(0x30 + i);
    }
    return true;
#endif
}

bool PicoI2c::write_sync(uint8_t slave_addr, std::span<const uint8_t> tx_data) {
    if (tx_data.empty()) return true;
#ifdef PICO_ON_DEVICE
    int res = i2c_write_blocking(i2c0, slave_addr, tx_data.data(), tx_data.size(), false);
    return res >= 0;
#else
    (void)slave_addr;
    return true;
#endif
}

bool PicoI2c::read_sync(uint8_t slave_addr, std::span<uint8_t> rx_data) {
    if (rx_data.empty()) return true;
#ifdef PICO_ON_DEVICE
    int res = i2c_read_blocking(i2c0, slave_addr, rx_data.data(), rx_data.size(), false);
    return res >= 0;
#else
    (void)slave_addr;
    for (size_t i = 0; i < rx_data.size(); ++i) {
        rx_data[i] = static_cast<uint8_t>(0x40 + i);
    }
    return true;
#endif
}

// @impl [SPEC-PICO-HAL-01] targets/pico2w_rp2350/SPECIFICATION.md
void PicoI2c::start_hardware_transfer_from_isr(const I2cRequest& req) noexcept {
    active_req_ = req;
    busy_ = true;
    cmd_idx_ = 0;
    rx_idx_ = 0;

#ifdef PICO_ON_DEVICE
    if (req.tx_data.empty() && req.rx_data.empty()) {
        busy_ = false;
        I2cResult result{};
        result.timestamp_us = time_us_64();
        push_completion_from_isr(active_req_, result);
        set_hardware_idle_from_isr();
        return;
    }
    // Same register sequence as the SDK's i2c_write_blocking_internal: disable, TAR, enable.
    i2c_hw_t* hw = i2c_get_hw(i2c0);
    hw->intr_mask = 0;
    hw->enable = 0;
    hw->tar = req.slave_addr;
    hw->enable = 1;
    (void)hw->clr_intr;  // drop a stale STOP_DET / TX_ABRT from an earlier transfer
    // IC_TX_TL = IC_RX_TL = 0 (i2c_init): TX_EMPTY fires now and the handler queues the words.
    hw->intr_mask = I2C_IC_INTR_MASK_M_TX_EMPTY_BITS |
                    I2C_IC_INTR_MASK_M_RX_FULL_BITS |
                    I2C_IC_INTR_MASK_M_STOP_DET_BITS |
                    I2C_IC_INTR_MASK_M_TX_ABRT_BITS;
#else
    // SITL / Desktop simulation immediately completes
    I2cResult result{};
    result.status = I2cStatus::Ok;
    result.transferred_bytes = req.is_read ? req.rx_data.size() : req.tx_data.size();
    result.timestamp_us = 0;

    push_completion_from_isr(req, result);
    set_hardware_idle_from_isr();
#endif
}

#ifdef PICO_ON_DEVICE
void PicoI2c::i2c0_irq_handler() {
    get_pico_i2c().on_irq_from_isr();
}

// @impl [SPEC-PICO-HAL-01] targets/pico2w_rp2350/SPECIFICATION.md
void PicoI2c::on_irq_from_isr() noexcept {
    constexpr size_t kFifoDepth = 16;  // IC_TX_BUFFER_DEPTH / IC_RX_BUFFER_DEPTH
    i2c_hw_t* hw = i2c_get_hw(i2c0);
    const uint32_t stat = hw->intr_stat;
    const size_t tx_len = active_req_.tx_data.size();
    const size_t rx_len = active_req_.rx_data.size();
    const size_t total = tx_len + rx_len;

    I2cResult result{};
    bool complete = false;

    if (stat & I2C_IC_INTR_STAT_R_TX_ABRT_BITS) {
        // The controller flushed the FIFOs and sends STOP itself.
        const uint32_t reason = hw->tx_abrt_source;
        (void)hw->clr_tx_abrt;
        if (reason & I2C_IC_TX_ABRT_SOURCE_ABRT_7B_ADDR_NOACK_BITS) {
            result.status = I2cStatus::NackAddress;
        } else if (reason & I2C_IC_TX_ABRT_SOURCE_ABRT_TXDATA_NOACK_BITS) {
            result.status = I2cStatus::NackData;
        } else {
            result.status = I2cStatus::BusError;
        }
        result.transferred_bytes = 0;
        complete = true;
    } else {
        // Drain received bytes.
        while (rx_idx_ < rx_len && hw->rxflr > 0) {
            active_req_.rx_data[rx_idx_] = static_cast<uint8_t>(hw->data_cmd);
            rx_idx_ = rx_idx_ + 1;
        }
        // Queue write bytes, then read commands; never more reads in flight than the RX FIFO holds.
        while (cmd_idx_ < total && hw->txflr < kFifoDepth) {
            const size_t idx = cmd_idx_;
            uint32_t word;
            if (idx < tx_len) {
                word = active_req_.tx_data[idx];
            } else {
                if ((idx - tx_len) - rx_idx_ >= kFifoDepth) {
                    break;
                }
                word = I2C_IC_DATA_CMD_CMD_BITS;
                if (idx == tx_len && tx_len != 0) {
                    word |= 1u << I2C_IC_DATA_CMD_RESTART_LSB;  // write -> read: repeated start
                }
            }
            if (idx + 1 == total) {
                word |= 1u << I2C_IC_DATA_CMD_STOP_LSB;
            }
            hw->data_cmd = word;
            cmd_idx_ = idx + 1;
        }
        if (cmd_idx_ >= total) {
            hw->intr_mask = hw->intr_mask & ~I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
        }
        if (stat & I2C_IC_INTR_STAT_R_STOP_DET_BITS) {
            (void)hw->clr_stop_det;
            if (cmd_idx_ >= total && rx_idx_ >= rx_len) {
                result.status = I2cStatus::Ok;
                result.transferred_bytes = rx_len ? rx_len : tx_len;
            } else {
                result.status = I2cStatus::BusError;  // STOP before every word went out
                result.transferred_bytes = 0;
            }
            complete = true;
        }
    }

    if (complete) {
        hw->intr_mask = 0;
        (void)hw->clr_intr;
        busy_ = false;
        result.timestamp_us = time_us_64();
        // Complete the finished request, then start the next one exactly once:
        // set_hardware_idle_from_isr() pops and starts it (and overwrites active_req_).
        push_completion_from_isr(active_req_, result);
        set_hardware_idle_from_isr();
    }
}
#endif

static PicoI2c g_pico_i2c;

PicoI2c& get_pico_i2c() noexcept {
    return g_pico_i2c;
}

II2c& get_i2c_driver() noexcept {
    return g_pico_i2c;
}

II2c& get_i2c() noexcept {
    return g_pico_i2c;
}

} // namespace abstractx::hal
