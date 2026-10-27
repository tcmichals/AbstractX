/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E906 I2C / TWI HAL Header
 */

#pragma once

#include "abstractx/hal/i2c.hpp"
#include "hal/ports.hpp"
#include <cstdint>
#include <span>

namespace abstractx::hal {

// Interrupt-driven TWI master: one INT_FLG interrupt per bus step.
// @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md
class E906I2c : public II2c {
public:
    static constexpr uint32_t kTimeoutMs = 20;

    explicit constexpr E906I2c(const ::hal::ports::TwiPort& port) noexcept : port_(port) {}

    bool init(const I2cConfig& config) override;
    bool set_frequency(uint32_t frequency_hz) override;
    I2cCapabilities capabilities() const noexcept override;

    bool write_read_sync(uint8_t slave_addr,
                         std::span<const uint8_t> tx_data,
                         std::span<uint8_t> rx_data) override;
    bool write_sync(uint8_t slave_addr, std::span<const uint8_t> tx_data) override;
    bool read_sync(uint8_t slave_addr, std::span<uint8_t> rx_data) override;

    // Write `tx`, then (repeated START) read `rx`; either may be empty, both empty = address probe.
    I2cStatus transfer_sync(uint8_t addr, std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept;

    bool is_busy() const noexcept { return state_ == State::Busy; }
    size_t bytes_read() const noexcept { return rx_idx_; }
    uint32_t timeouts() const noexcept { return timeouts_; }

    void on_irq() noexcept;

    // Loop context, every step(): end an async transfer past its deadline with Timeout.
    void poll_timeout() noexcept;

protected:
    void start_hardware_transfer_from_isr(const I2cRequest& req) noexcept override;

private:
    enum class State : uint8_t { Idle, Busy, Done, Error };

    volatile uint32_t& reg(uint32_t off) const noexcept {
        return *reinterpret_cast<volatile uint32_t*>(port_.base + off);
    }
    void cmd(uint32_t bits) noexcept;
    void start(uint8_t addr, std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept;
    void finish(I2cStatus status) noexcept;
    void complete_async(I2cStatus status, size_t bytes) noexcept;
    void after_tx() noexcept;

    const ::hal::ports::TwiPort& port_;
    bool attached_{false};
    volatile State state_{State::Idle};
    I2cStatus status_{I2cStatus::Ok};
    bool async_{false};
    uint64_t deadline_{0};  // async transfer, mtime ticks
    I2cRequest active_req_{};

    uint8_t addr_{0};
    bool read_phase_{false};
    std::span<const uint8_t> tx_{};
    std::span<uint8_t> rx_{};
    size_t tx_idx_{0};
    size_t rx_idx_{0};
    volatile uint32_t timeouts_{0};
};

E906I2c& get_e906_i2c() noexcept;   // TWI0
E906I2c& get_e906_twi1() noexcept;  // TWI1, 40-pin header PB4/PB5
void poll_i2c_timeouts() noexcept;   // both TWI ports; called by platform_poll_network()
II2c& get_i2c_driver() noexcept;
II2c& get_i2c() noexcept;

} // namespace abstractx::hal
