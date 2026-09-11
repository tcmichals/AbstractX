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

void PicoI2c::start_hardware_transfer_from_isr(const I2cRequest& req) noexcept {
    active_req_ = req;
    busy_ = true;

    bool ok = false;
    if (req.repeated_start && !req.tx_data.empty() && !req.rx_data.empty()) {
        ok = write_read_sync(req.slave_addr, req.tx_data, req.rx_data);
    } else if (req.is_read && !req.rx_data.empty()) {
        ok = read_sync(req.slave_addr, req.rx_data);
    } else if (!req.tx_data.empty()) {
        ok = write_sync(req.slave_addr, req.tx_data);
    } else {
        ok = true;
    }

    busy_ = false;

    I2cResult result{};
    result.status = ok ? I2cStatus::Ok : I2cStatus::BusError;
    result.transferred_bytes = req.is_read ? req.rx_data.size() : req.tx_data.size();
#ifdef PICO_ON_DEVICE
    result.timestamp_us = time_us_64();
#else
    result.timestamp_us = 0;
#endif

    push_completion_from_isr(req, result);
    set_hardware_idle_from_isr();
}

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
