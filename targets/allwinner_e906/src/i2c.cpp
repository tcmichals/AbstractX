/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E906 I2C / TWI driver: one driver, one object per controller.
 */

#include "hal/i2c.hpp"
#include "hal/ccu.hpp"
#include "hal/clic.hpp"
#include "hal/timer.hpp"
#include "abstractx/hal/platform.hpp"
#include "abstractx/interrupt_lock.hpp"

// @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md
// @impl [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md (FIFO only until TWI DMA is measured)
// @impl [SPEC-E906-IRQ-02] targets/allwinner_e906/SPECIFICATION.md

namespace abstractx::hal {

namespace {

constexpr uint32_t kData = 0x08;
constexpr uint32_t kCntr = 0x0C;
constexpr uint32_t kStat = 0x10;
constexpr uint32_t kCcr  = 0x14;
constexpr uint32_t kSrst = 0x18;

constexpr uint32_t kIntEn  = 1U << 7;
constexpr uint32_t kBusEn  = 1U << 6;
constexpr uint32_t kMSta   = 1U << 5;
constexpr uint32_t kMStp   = 1U << 4;
constexpr uint32_t kIntFlg = 1U << 3;  // cleared by writing 1
constexpr uint32_t kAAck   = 1U << 2;

// CCR = (M << 3) | N, f = 24 MHz / (10 * (M + 1) * 2^N)
constexpr uint32_t kCcr400k = (2U << 3) | 1U;
constexpr uint32_t kCcr100k = (5U << 3) | 2U;

E906I2c g_twi0{::hal::ports::kTwi0};
E906I2c g_twi1{::hal::ports::kTwi1};

void default_wait(uint64_t deadline_ticks) noexcept {
    ::hal::Timer::arm_wake(deadline_ticks);
    platform_idle_wait();
}

E906I2c::WaitHook s_wait = default_wait;

} // namespace

E906I2c& get_e906_i2c() noexcept { return g_twi0; }
E906I2c& get_e906_twi1() noexcept { return g_twi1; }
// @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md (async deadline, rule 8)
void poll_i2c_timeouts() noexcept {
    g_twi0.poll_timeout();
    g_twi1.poll_timeout();
}

II2c& get_i2c_driver() noexcept { return g_twi0; }
II2c& get_i2c() noexcept { return g_twi0; }

void E906I2c::set_wait_hook(WaitHook hook) noexcept { s_wait = hook ? hook : default_wait; }

// @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md (pins of this port only, shared line)
bool E906I2c::init(const I2cConfig& config) {
    reg(kCntr) = 0;  // interrupt off; the CLIC line is shared with the other TWI, leave it enabled
    ::hal::Ccu::enable_module(port_.clock);
    if (port_.pins.cfg != 0) {
        auto& cfg = *reinterpret_cast<volatile uint32_t*>(port_.pins.cfg);
        cfg = (cfg & ~port_.pins.mask) | port_.pins.func;
    }
    reg(kSrst) = 1;  // soft reset, self-clearing
    ::hal::Timer::delay_us(20);
    set_frequency(config.frequency_hz ? config.frequency_hz : 400'000U);
    reg(kCntr) = kBusEn;
    state_ = State::Idle;

    const uint32_t line = ::hal::clic::line_for_gic(port_.gic);
    ::hal::clic::route_gic(port_.gic, true);
    if (!attached_) {
        attached_ = ::hal::irq::attach(line,
            +[](void* ctx) noexcept { static_cast<E906I2c*>(ctx)->on_irq(); }, this);
        ::hal::irq::describe(line, port_.name, &timeouts_);
    }
    return attached_;
}

bool E906I2c::set_frequency(uint32_t frequency_hz) {
    reg(kCcr) = frequency_hz >= 400'000U ? kCcr400k : kCcr100k;
    return true;
}

I2cCapabilities E906I2c::capabilities() const noexcept {
    I2cCapabilities caps{};
    caps.max_hz = 400'000;
    caps.max_msgs = 2;  // write, read, or write then read (repeated START)
    caps.zero_length_write = true;
    return caps;
}

void E906I2c::cmd(uint32_t bits) noexcept { reg(kCntr) = kBusEn | kIntEn | kIntFlg | bits; }

void E906I2c::start(uint8_t addr, std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept {
    addr_ = addr;
    tx_ = tx;
    rx_ = rx;
    tx_idx_ = 0;
    rx_idx_ = 0;
    read_phase_ = tx.empty() && !rx.empty();
    status_ = I2cStatus::Ok;
    state_ = State::Busy;
    cmd(kMSta);
}

__attribute__((noinline)) void E906I2c::finish(I2cStatus status) noexcept {
    reg(kCntr) = kBusEn | kMStp | kIntFlg;  // STOP, no further interrupt
    status_ = status;
    state_ = status == I2cStatus::Ok ? State::Done : State::Error;
    if (async_) {
        async_ = false;
        complete_async(status, tx_idx_ + rx_idx_);
    }
}

void E906I2c::complete_async(I2cStatus status, size_t bytes) noexcept {
    I2cResult res{};
    res.status = status;
    res.transferred_bytes = bytes;
    res.msgs_done = status == I2cStatus::Ok ? static_cast<uint8_t>(active_req_.msgs.size()) : 0;
    res.timestamp_us = ::hal::Timer::get_time_ns() / 1000ULL;
    push_completion_from_isr(active_req_, res);
    set_hardware_idle_from_isr();
}

void E906I2c::after_tx() noexcept {
    if (tx_idx_ < tx_.size()) {
        reg(kData) = tx_[tx_idx_++];
        cmd(0);
    } else if (!rx_.empty() && !read_phase_) {
        read_phase_ = true;
        cmd(kMSta);  // repeated START for the read
    } else {
        finish(I2cStatus::Ok);
    }
}

// @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md (INT_FLG write-1-to-clear, errors end with STOP)
void E906I2c::on_irq() noexcept {
    if (!(reg(kCntr) & kIntFlg)) {
        return;  // the other TWI on the shared line
    }
    const uint32_t stat = reg(kStat) & 0xFFU;
    if (state_ != State::Busy) {
        reg(kCntr) = kBusEn | kIntFlg;  // stray flag: clear, interrupt off
        return;
    }
    switch (stat) {
        case 0x08:  // START sent
        case 0x10:  // repeated START sent
            reg(kData) = (static_cast<uint32_t>(addr_) << 1) | (read_phase_ ? 1U : 0U);
            cmd(0);
            break;
        case 0x18:  // SLA+W ACK
        case 0x28:  // data byte ACK
            after_tx();
            break;
        case 0x40:  // SLA+R ACK
            cmd(rx_.size() > 1 ? kAAck : 0);
            break;
        case 0x50:  // data received, ACK sent
            if (rx_idx_ < rx_.size()) rx_[rx_idx_++] = static_cast<uint8_t>(reg(kData));
            cmd((rx_.size() - rx_idx_) > 1 ? kAAck : 0);
            break;
        case 0x58:  // last byte received, NACK sent
            if (rx_idx_ < rx_.size()) rx_[rx_idx_++] = static_cast<uint8_t>(reg(kData));
            finish(I2cStatus::Ok);
            break;
        case 0x20:
        case 0x48:
            finish(I2cStatus::NackAddress);
            break;
        case 0x30:
            finish(I2cStatus::NackData);
            break;
        case 0x38:
            finish(I2cStatus::ArbitrationLost);
            break;
        default:
            finish(I2cStatus::BusError);
            break;
    }
}

// @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md (bounded wait through the hook)
I2cStatus E906I2c::transfer_sync(uint8_t addr, std::span<const uint8_t> tx,
                                 std::span<uint8_t> rx) noexcept {
    if (state_ == State::Busy) {
        if (async_) return I2cStatus::QueueFull;
        finish(I2cStatus::Cancelled);  // a previous call was interrupted (Ctrl-C): abort it
    }
    if (reg(kCntr) & kMStp) {
        // previous STOP still on the bus: one short sleep; START queues behind it anyway
        s_wait(::hal::Timer::get_ticks() + 50ULL * ::hal::Timer::TICKS_PER_US);
    }
    start(addr, tx, rx);

    const uint64_t deadline = ::hal::Timer::get_ticks() + kTimeoutMs * ::hal::Timer::TICKS_PER_MS;
    while (state_ == State::Busy) {
        if (::hal::Timer::get_ticks() >= deadline) {
            finish(I2cStatus::Timeout);
            timeouts_ = timeouts_ + 1;
            return I2cStatus::Timeout;
        }
        s_wait(deadline);
    }
    return status_;
}

bool E906I2c::write_read_sync(uint8_t slave_addr, std::span<const uint8_t> tx_data,
                              std::span<uint8_t> rx_data) {
    return transfer_sync(slave_addr, tx_data, rx_data) == I2cStatus::Ok;
}

bool E906I2c::write_sync(uint8_t slave_addr, std::span<const uint8_t> tx_data) {
    return transfer_sync(slave_addr, tx_data, {}) == I2cStatus::Ok;
}

bool E906I2c::read_sync(uint8_t slave_addr, std::span<uint8_t> rx_data) {
    return transfer_sync(slave_addr, {}, rx_data) == I2cStatus::Ok;
}

void E906I2c::start_hardware_transfer_from_isr(const I2cRequest& req) noexcept {
    active_req_ = req;
    if (state_ == State::Busy) {  // a synchronous transfer owns the bus: refuse, leave it running
        complete_async(I2cStatus::QueueFull, 0);
        return;
    }
    std::span<const uint8_t> tx = req.tx_data;
    std::span<uint8_t> rx = req.rx_data;
    uint8_t addr = req.slave_addr;
    if (!req.msgs.empty() || req.recover_bus) {
        const auto& m = req.msgs;
        addr = static_cast<uint8_t>(req.address & 0x7FU);
        if (m.size() == 1 && m[0].is_read()) {
            tx = {};
            rx = m[0].rx;
        } else if (m.size() == 1) {
            tx = m[0].tx;
            rx = {};
        } else if (m.size() == 2 && !m[0].is_read() && m[1].is_read()) {
            tx = m[0].tx;
            rx = m[1].rx;
        } else {
            complete_async(I2cStatus::NotSupported, 0);
            return;
        }
    }
    async_ = true;
    const uint32_t us = req.timeout_us ? req.timeout_us : kI2cDefaultTimeoutUs;
    deadline_ = ::hal::Timer::get_ticks() + static_cast<uint64_t>(us) * ::hal::Timer::TICKS_PER_US;
    ::hal::Timer::arm_wake(deadline_);  // wake a sleeping loop to run poll_timeout()
    start(addr, tx, rx);
}

// @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md (async deadline, rule 8)
__attribute__((noinline)) void E906I2c::poll_timeout() noexcept {
    if (!async_) return;  // cheap check; re-checked under the guard
    abstractx::InterruptGuard guard;  // the completion interrupt cannot finish it at the same time
    if (async_ && state_ == State::Busy && ::hal::Timer::get_ticks() >= deadline_) {
        timeouts_ = timeouts_ + 1;
        finish(I2cStatus::Timeout);
    }
}

} // namespace abstractx::hal
