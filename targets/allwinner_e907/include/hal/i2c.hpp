/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E907 I2C / TWI HAL Header
 */

#pragma once

#include "abstractx/hal/i2c.hpp"
#include <cstdint>
#include <span>

namespace abstractx::hal {

class E907I2c : public II2c {
public:
    E907I2c() = default;

    bool init(const I2cConfig& config) override;
    bool set_frequency(uint32_t frequency_hz) override;

    bool write_read_sync(uint8_t slave_addr,
                         std::span<const uint8_t> tx_data,
                         std::span<uint8_t> rx_data) override;

    bool write_sync(uint8_t slave_addr, std::span<const uint8_t> tx_data) override;
    bool read_sync(uint8_t slave_addr, std::span<uint8_t> rx_data) override;

    bool is_busy() const noexcept { return busy_; }

    void on_irq() noexcept;

protected:
    void start_hardware_transfer_from_isr(const I2cRequest& req) noexcept override;

private:
    void start_fsm(uint8_t slave_addr, bool is_read, bool repeated_start,
                   std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept;

    I2cConfig config_{};
    I2cRequest active_req_{};
    volatile bool busy_{false};
    bool success_{false};

    uint8_t slave_addr_{0};
    bool is_read_{false};
    bool repeated_start_{false};
    std::span<const uint8_t> tx_{};
    std::span<uint8_t> rx_{};
    size_t tx_idx_{0};
    size_t rx_idx_{0};
};

E907I2c& get_e907_i2c() noexcept;
II2c& get_i2c_driver() noexcept;
II2c& get_i2c() noexcept;

} // namespace abstractx::hal
