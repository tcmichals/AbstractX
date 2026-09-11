/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E907 I2C / TWI Driver
 * --------------------------------------------------
 * 100% ISR-driven hardware FSM driver for Sunxi TWI0 (0x02502000).
 * ZERO POLLING / ZERO BUSY-WAIT LOOPS. Idle core sleeps in WFI.
 */

#include "hal/i2c.hpp"
#include "hal/ccu.hpp"
#include "hal/timer.hpp"
#include "memory_map.h"
#include <atomic>

namespace abstractx::hal {

/* Sunxi TWI0 Hardware Register Offsets */
#define TWI0_ADDR        (*(volatile uint32_t *)(TWI0_BASE + 0x00))
#define TWI0_XADDR       (*(volatile uint32_t *)(TWI0_BASE + 0x04))
#define TWI0_DATA        (*(volatile uint32_t *)(TWI0_BASE + 0x08))
#define TWI0_CNTR        (*(volatile uint32_t *)(TWI0_BASE + 0x0C))
#define TWI0_STAT        (*(volatile uint32_t *)(TWI0_BASE + 0x10))
#define TWI0_CCR         (*(volatile uint32_t *)(TWI0_BASE + 0x14))
#define TWI0_SRST        (*(volatile uint32_t *)(TWI0_BASE + 0x18))

/* Sunxi TWI Control Register Bits */
#define TWI_CNTR_INTEN   (1U << 7)
#define TWI_CNTR_BUSEN   (1U << 6)
#define TWI_CNTR_M_STA   (1U << 5)
#define TWI_CNTR_M_STP   (1U << 4)
#define TWI_CNTR_INT_FLG (1U << 3)
#define TWI_CNTR_A_ACK   (1U << 2)

static E907I2c g_e907_i2c;

E907I2c& get_e907_i2c() noexcept {
    return g_e907_i2c;
}

II2c& get_i2c_driver() noexcept {
    return g_e907_i2c;
}

II2c& get_i2c() noexcept {
    return g_e907_i2c;
}

bool E907I2c::init(const I2cConfig& config) {
    config_ = config;

    // 1. Enable Bus Clock Gating and deassert reset in CCU
    ::hal::Ccu::enable_module(::hal::Ccu::BusModule::Twi0);

    // 2. Soft Reset TWI Controller
    TWI0_SRST = 1;
    for (int i = 0; i < 100; ++i) __asm__ volatile("nop");

    // 3. Set Frequency (Default Fast Mode: 400 kHz)
    set_frequency(config.frequency_hz ? config.frequency_hz : 400'000);

    // 4. Enable Bus and Interrupts
    TWI0_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_INTEN;

    // 5. Enable PLIC IRQ 26 (TWI0)
    volatile uint32_t* priority = reinterpret_cast<volatile uint32_t*>(0x10000000 + 4 * 26);
    *priority = 1;
    volatile uint32_t* enable = reinterpret_cast<volatile uint32_t*>(0x10002000 + 4 * (26 / 32));
    *enable |= (1U << (26 % 32));

    return true;
}

bool E907I2c::set_frequency(uint32_t frequency_hz) {
    config_.frequency_hz = frequency_hz;
    // APB Clock = 24 MHz
    // Fscl = 24MHz / (10 * 2^N * (M + 1))
    if (frequency_hz >= 400'000) {
        // N = 1, M = 2 -> 24M / (10 * 2 * 3) = 400 kHz
        TWI0_CCR = (2 << 3) | 1;
    } else {
        // N = 2, M = 5 -> 24M / (10 * 4 * 6) = 100 kHz
        TWI0_CCR = (5 << 3) | 2;
    }
    return true;
}

void E907I2c::start_fsm(uint8_t slave_addr, bool is_read, bool repeated_start,
                       std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept {
    slave_addr_ = slave_addr;
    is_read_ = is_read;
    repeated_start_ = repeated_start;
    tx_ = tx;
    rx_ = rx;
    tx_idx_ = 0;
    rx_idx_ = 0;
    busy_ = true;
    success_ = false;

    // Send Master START
    TWI0_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_INTEN | TWI_CNTR_M_STA;
}

bool E907I2c::write_read_sync(uint8_t slave_addr,
                             std::span<const uint8_t> tx_data,
                             std::span<uint8_t> rx_data) {
    if (tx_data.empty() && rx_data.empty()) return true;

    start_fsm(slave_addr, false, !rx_data.empty(), tx_data, rx_data);

    // Sleep in WFI until FSM completion ISR triggers
    while (busy_) {
        __asm__ volatile("wfi");
    }
    return success_;
}

bool E907I2c::write_sync(uint8_t slave_addr, std::span<const uint8_t> tx_data) {
    if (tx_data.empty()) return true;

    start_fsm(slave_addr, false, false, tx_data, {});

    while (busy_) {
        __asm__ volatile("wfi");
    }
    return success_;
}

bool E907I2c::read_sync(uint8_t slave_addr, std::span<uint8_t> rx_data) {
    if (rx_data.empty()) return true;

    start_fsm(slave_addr, true, false, {}, rx_data);

    while (busy_) {
        __asm__ volatile("wfi");
    }
    return success_;
}

void E907I2c::start_hardware_transfer_from_isr(const I2cRequest& req) noexcept {
    active_req_ = req;
    bool is_rd = req.is_read || (!req.repeated_start && req.tx_data.empty() && !req.rx_data.empty());
    start_fsm(req.slave_addr, is_rd, req.repeated_start, req.tx_data, req.rx_data);
}

void E907I2c::on_irq() noexcept {
    uint32_t status = TWI0_STAT;

    switch (status) {
    case 0x08: // START condition transmitted
        // Send SLA+W or SLA+R
        TWI0_DATA = (static_cast<uint32_t>(slave_addr_) << 1) | (is_read_ ? 1 : 0);
        TWI0_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_INTEN;
        break;

    case 0x10: // REPEATED START condition transmitted
        // Send SLA+R
        TWI0_DATA = (static_cast<uint32_t>(slave_addr_) << 1) | 1;
        TWI0_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_INTEN;
        break;

    case 0x18: // SLA+W transmitted, ACK received
    case 0x28: // Data byte in TWI_DATA transmitted, ACK received
        if (tx_idx_ < tx_.size()) {
            // Write next byte
            TWI0_DATA = tx_[tx_idx_++];
            TWI0_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_INTEN;
        } else if (repeated_start_ && !rx_.empty()) {
            // Transition to Repeated Start Read
            is_read_ = true;
            repeated_start_ = false;
            TWI0_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_INTEN | TWI_CNTR_M_STA;
        } else {
            // End of write: send STOP
            TWI0_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_M_STP;
            success_ = true;
            busy_ = false;

            if (active_req_.slave_addr != 0) {
                I2cResult res{};
                res.status = I2cStatus::Ok;
                res.transferred_bytes = tx_.size();
                res.timestamp_us = ::hal::Timer::get_time_ns() / 1000ULL;
                push_completion_from_isr(active_req_, res);
                active_req_.slave_addr = 0;
                set_hardware_idle_from_isr();
            }
        }
        break;

    case 0x40: // SLA+R transmitted, ACK received
        if (rx_.size() > 1) {
            // Assert ACK for incoming byte
            TWI0_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_INTEN | TWI_CNTR_A_ACK;
        } else {
            // Only 1 byte requested: send NACK
            TWI0_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_INTEN;
        }
        break;

    case 0x50: // Data byte received, ACK returned
        if (rx_idx_ < rx_.size()) {
            rx_[rx_idx_++] = static_cast<uint8_t>(TWI0_DATA);
        }
        if (rx_.size() - rx_idx_ > 1) {
            // Still more than 1 byte remaining: assert ACK
            TWI0_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_INTEN | TWI_CNTR_A_ACK;
        } else {
            // Last byte remaining: send NACK
            TWI0_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_INTEN;
        }
        break;

    case 0x58: // Data byte received, NACK returned
        if (rx_idx_ < rx_.size()) {
            rx_[rx_idx_++] = static_cast<uint8_t>(TWI0_DATA);
        }
        // Read burst finished: send STOP
        TWI0_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_M_STP;
        success_ = true;
        busy_ = false;

        if (active_req_.slave_addr != 0) {
            I2cResult res{};
            res.status = I2cStatus::Ok;
            res.transferred_bytes = rx_.size();
            res.timestamp_us = ::hal::Timer::get_time_ns() / 1000ULL;
            push_completion_from_isr(active_req_, res);
            active_req_.slave_addr = 0;
            set_hardware_idle_from_isr();
        }
        break;

    default: // Error states: 0x20 (SLA+W NACK), 0x30 (Data TX NACK), 0x38 (Arb Lost), 0x48 (SLA+R NACK), 0x00 (Bus error)
        TWI0_CNTR = TWI_CNTR_BUSEN | TWI_CNTR_M_STP;
        success_ = false;
        busy_ = false;

        if (active_req_.slave_addr != 0) {
            I2cResult res{};
            res.status = I2cStatus::BusError;
            res.transferred_bytes = 0;
            push_completion_from_isr(active_req_, res);
            active_req_.slave_addr = 0;
            set_hardware_idle_from_isr();
        }
        break;
    }
}

} // namespace abstractx::hal

// Connect PLIC IRQ 26 ISR to E907I2c instance
extern "C" __attribute__((section(".fastcode")))
void fc_twi0_isr() noexcept {
    abstractx::hal::get_e907_i2c().on_irq();
}
