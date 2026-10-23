/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: I2C Asynchronous Split-Queue & Packet Transfer Interface
 * ------------------------------------------------------------------------
 * Aligned with INAV bus_i2c and Linux i2c_rdwr_ioctl_data standards.
 */

#ifndef ABSTRACTX_HAL_I2C_HPP
#define ABSTRACTX_HAL_I2C_HPP

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>
#include <coroutine>

#include "async_driver.hpp"
#include "io_status.hpp"

namespace abstractx::hal {

enum class I2cSpeed : uint8_t {
    Standard_100k = 0,
    Fast_400k     = 1,
    FastPlus_1M   = 2
};

enum class I2cStatus : uint8_t {
    Ok = 0,
    NackAddress = 1,
    NackData = 2,
    BusError = 3,
    Timeout = 4,
    QueueFull = 5,
    ArbitrationLost = 6,   // [SPEC-HAL-09]
    NotSupported = 7,      // [SPEC-HAL-09]: outside I2cCapabilities, refused before the hardware
    InvalidArgument = 8,   // [SPEC-HAL-09]: malformed transfer
    Cancelled = 9          // [SPEC-HAL-07]
};

// @impl [SPEC-HAL-07] docs/DESIGN_SPECIFICATION.md
constexpr IoStatus to_io_status(I2cStatus s) noexcept {
    switch (s) {
    case I2cStatus::Ok: return IoStatus::Ok;
    case I2cStatus::NackAddress: return IoStatus::NackAddress;
    case I2cStatus::NackData: return IoStatus::NackData;
    case I2cStatus::BusError: return IoStatus::BusError;
    case I2cStatus::Timeout: return IoStatus::Timeout;
    case I2cStatus::QueueFull: return IoStatus::QueueFull;
    case I2cStatus::ArbitrationLost: return IoStatus::ArbitrationLost;
    case I2cStatus::NotSupported: return IoStatus::NotSupported;
    case I2cStatus::InvalidArgument: return IoStatus::InvalidArgument;
    case I2cStatus::Cancelled: return IoStatus::Cancelled;
    }
    return IoStatus::BusError;
}

// ---------------------------------------------------------------------------
// [SPEC-HAL-09] Message transfers (Linux / Zephyr i2c_msg)
// @impl [SPEC-HAL-09] docs/DESIGN_SPECIFICATION.md
// ---------------------------------------------------------------------------
namespace I2cFlag {
inline constexpr uint8_t Read    = 0x01;  // read message (rx span)
inline constexpr uint8_t NoStart = 0x02;  // no repeated START: continue the previous message
inline constexpr uint8_t Stop    = 0x04;  // STOP after this (non-last) message
} // namespace I2cFlag

inline constexpr uint16_t kI2cTenBit = 0x8000;           // address flag: 10-bit address
inline constexpr uint32_t kI2cDefaultTimeoutUs = 20'000;
inline constexpr size_t   kI2cRegWriteMax = 32;          // reg + data bytes when NoStart is missing

constexpr uint16_t i2c_addr10(uint16_t addr) noexcept { return static_cast<uint16_t>(addr | kI2cTenBit); }

struct I2cMsg {
    std::span<const uint8_t> tx{};
    std::span<uint8_t>       rx{};
    uint8_t                  flags{0};

    static constexpr I2cMsg write(std::span<const uint8_t> d, uint8_t f = 0) noexcept {
        return I2cMsg{d, {}, static_cast<uint8_t>(f & ~I2cFlag::Read)};
    }
    static constexpr I2cMsg read(std::span<uint8_t> d, uint8_t f = 0) noexcept {
        return I2cMsg{{}, d, static_cast<uint8_t>(f | I2cFlag::Read)};
    }
    constexpr bool is_read() const noexcept { return (flags & I2cFlag::Read) != 0; }
    constexpr size_t size() const noexcept { return is_read() ? rx.size() : tx.size(); }
};

// What the controller supports; a field is set only when verified for that chip.
struct I2cCapabilities {
    uint32_t max_hz{0};               // 0 = unknown
    uint16_t fifo_bytes{0};           // informative
    uint16_t max_msg_bytes{0};        // 0 = no limit
    uint8_t  max_msgs{0};             // 0 = message transfers not implemented: all refused
    bool     ten_bit{false};
    bool     no_start{false};
    bool     stop_between{false};     // I2cFlag::Stop on a non-last message
    bool     zero_length_write{false};// address-only probe
    bool     bus_recovery{false};
    bool     dma{false};              // informative
};

// Pure check of a message transfer ([SPEC-HAL-09]): Ok, InvalidArgument or NotSupported.
constexpr I2cStatus check_i2c_transfer(const I2cCapabilities& caps, uint16_t addr,
                                       std::span<const I2cMsg> msgs, uint32_t timeout_us) noexcept {
    const bool ten = (addr & kI2cTenBit) != 0;
    const uint16_t a = static_cast<uint16_t>(addr & ~kI2cTenBit);
    if (msgs.empty() || timeout_us == 0) return I2cStatus::InvalidArgument;
    if (ten ? a > 0x3FFU : a > 0x7FU) return I2cStatus::InvalidArgument;
    for (size_t i = 0; i < msgs.size(); ++i) {
        const I2cMsg& m = msgs[i];
        if (m.is_read() ? (!m.tx.empty() || m.rx.empty()) : !m.rx.empty()) {
            return I2cStatus::InvalidArgument;
        }
        if ((m.flags & I2cFlag::NoStart) != 0 && (i == 0 || msgs[i - 1].is_read() != m.is_read())) {
            return I2cStatus::InvalidArgument;
        }
    }
    if (caps.max_msgs == 0 || msgs.size() > caps.max_msgs) return I2cStatus::NotSupported;
    if (ten && !caps.ten_bit) return I2cStatus::NotSupported;
    for (size_t i = 0; i < msgs.size(); ++i) {
        const I2cMsg& m = msgs[i];
        if (m.size() == 0 && !caps.zero_length_write) return I2cStatus::NotSupported;
        if (caps.max_msg_bytes != 0 && m.size() > caps.max_msg_bytes) return I2cStatus::NotSupported;
        if ((m.flags & I2cFlag::NoStart) != 0 && !caps.no_start) return I2cStatus::NotSupported;
        if ((m.flags & I2cFlag::Stop) != 0 && i + 1 < msgs.size() && !caps.stop_between) {
            return I2cStatus::NotSupported;
        }
    }
    return I2cStatus::Ok;
}

struct I2cConfig {
    uint32_t frequency_hz{400'000};
    I2cSpeed speed{I2cSpeed::Fast_400k};
    bool     ten_bit_addr{false};  // deprecated [SPEC-HAL-11]: i2c_addr10() per transfer
    bool     use_dma{false};
};

struct I2cResult {
    I2cStatus status{I2cStatus::Ok};
    size_t    transferred_bytes{0};
    uint64_t  timestamp_us{0};
    uint8_t   msgs_done{0};        // [SPEC-HAL-09]: messages fully completed
};

struct I2cRequest : public AsyncTransactionMeta<I2cResult> {
    uint8_t                  slave_addr{0};        // deprecated [SPEC-HAL-11]: `address`
    bool                     is_read{false};
    bool                     repeated_start{false};
    uint8_t                  register_offset{0};   // deprecated [SPEC-HAL-11]
    bool                     use_register{false};  // deprecated [SPEC-HAL-11]
    // [SPEC-HAL-09] Message transfer (msgs non-empty) or bus recovery (recover_bus); the driver
    // ignores the legacy fields then. msgs and their buffers live in the awaiting frame.
    uint16_t                 address{0};
    std::span<const I2cMsg>  msgs{};
    uint32_t                 timeout_us{0};
    bool                     recover_bus{false};
    std::span<const uint8_t> tx_data{};
    std::span<uint8_t>       rx_data{};

    // Dedicated RX/TX callbacks when hardware operation finishes
    etl::delegate<void(const I2cResult&)> on_tx_complete{};
    etl::delegate<void(const I2cResult&)> on_rx_complete{};

    void notify_tx_completion(const I2cResult& result) const noexcept {
        if (on_tx_complete.is_valid()) {
            on_tx_complete(result);
        }
    }

    void notify_rx_completion(const I2cResult& result) const noexcept {
        if (on_rx_complete.is_valid()) {
            on_rx_complete(result);
        }
    }
};

template <size_t QueueDepth = 16>
class AsyncI2cDriver : public AsyncDriverBase<I2cRequest, I2cResult, QueueDepth> {
public:
    virtual ~AsyncI2cDriver() = default;

    virtual bool init(const I2cConfig& config) = 0;
    virtual bool set_frequency(uint32_t frequency_hz) { (void)frequency_hz; return true; }

    // [SPEC-HAL-09] What this controller supports; the default refuses every message transfer.
    virtual I2cCapabilities capabilities() const noexcept { return {}; }

    /*
     * [SPEC-HAL-09] Message-transfer awaiter: checked in await_ready (a refusal never suspends or
     * reaches the hardware), submitted in await_suspend. Helpers keep their messages and the
     * register byte in the awaiter (bound to the request in await_suspend).
     */
    // @impl [SPEC-HAL-09] docs/DESIGN_SPECIFICATION.md
    struct AsyncMsgAwaiter {
        AsyncI2cDriver&          driver;
        uint16_t                 address{0};
        std::span<const I2cMsg>  caller_msgs{};
        uint32_t                 timeout_us{kI2cDefaultTimeoutUs};
        uint8_t                  helper{0};   // 0 = caller msgs, 1 = read regs, 2 = write regs, 3 = recover
        uint8_t                  reg{0};
        std::span<uint8_t>       out{};
        std::span<const uint8_t> in{};
        std::array<I2cMsg, 2>    own_msgs{};
        uint8_t                  own_count{0};
        std::array<uint8_t, kI2cRegWriteMax> combined{};
        I2cRequest               request{};
        I2cResult                result{};

        std::span<const I2cMsg> msgs() const noexcept {
            return helper == 0 ? caller_msgs : std::span<const I2cMsg>(own_msgs.data(), own_count);
        }

        // Runs on the awaiter's final address, so spans into reg/combined stay valid.
        bool await_ready() noexcept {
            const I2cCapabilities caps = driver.capabilities();
            if (helper == 3) {
                result.status = caps.bus_recovery ? I2cStatus::Ok : I2cStatus::NotSupported;
                return result.status != I2cStatus::Ok;
            }
            if (helper == 1) {
                own_msgs[0] = I2cMsg::write(std::span<const uint8_t>(&reg, 1));
                own_msgs[1] = I2cMsg::read(out);
                own_count = 2;
            } else if (helper == 2) {
                if (caps.no_start) {
                    own_msgs[0] = I2cMsg::write(std::span<const uint8_t>(&reg, 1));
                    own_msgs[1] = I2cMsg::write(in, I2cFlag::NoStart);
                    own_count = 2;
                } else if (in.size() + 1U <= combined.size()) {
                    combined[0] = reg;   // control path only ([SPEC-HAL-09], [SPEC-ARCH-08])
                    for (size_t i = 0; i < in.size(); ++i) combined[i + 1U] = in[i];
                    own_msgs[0] = I2cMsg::write(std::span<const uint8_t>(combined.data(), in.size() + 1U));
                    own_count = 1;
                } else {
                    result.status = I2cStatus::NotSupported;
                    return true;
                }
            }
            result.status = check_i2c_transfer(caps, address, msgs(), timeout_us);
            return result.status != I2cStatus::Ok;
        }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            request.address = address;
            request.msgs = msgs();
            request.timeout_us = timeout_us;
            request.recover_bus = (helper == 3);
            request.coro_handle = handle;
            request.result_slot = &result;
            if (!driver.submit_request(request)) {
                result.status = I2cStatus::QueueFull;
                return false; // resume immediately; never resume inline
            }
            return true;
        }

        I2cResult await_resume() noexcept { return result; }
    };

    // One bus transaction of messages to `address` (7-bit, or i2c_addr10(a)).
    AsyncMsgAwaiter transfer_async(uint16_t address, std::span<const I2cMsg> msgs,
                                   uint32_t timeout_us = kI2cDefaultTimeoutUs) noexcept {
        return AsyncMsgAwaiter{*this, address, msgs, timeout_us};
    }

    // write {reg}, repeated START, read out.size() bytes.
    AsyncMsgAwaiter read_regs_async(uint16_t address, uint8_t reg, std::span<uint8_t> out,
                                    uint32_t timeout_us = kI2cDefaultTimeoutUs) noexcept {
        AsyncMsgAwaiter a{*this, address, {}, timeout_us};
        a.helper = 1;
        a.reg = reg;
        a.out = out;
        return a;
    }

    // write {reg, in...} (NoStart when supported, else built in the awaiter, <= kI2cRegWriteMax).
    AsyncMsgAwaiter write_regs_async(uint16_t address, uint8_t reg, std::span<const uint8_t> in,
                                     uint32_t timeout_us = kI2cDefaultTimeoutUs) noexcept {
        AsyncMsgAwaiter a{*this, address, {}, timeout_us};
        a.helper = 2;
        a.reg = reg;
        a.in = in;
        return a;
    }

    // Up to 9 SCL pulses until SDA is released, then STOP ([SPEC-HAL-09]).
    AsyncMsgAwaiter recover_bus_async(uint32_t timeout_us = kI2cDefaultTimeoutUs) noexcept {
        AsyncMsgAwaiter a{*this, 0, {}, timeout_us};
        a.helper = 3;
        return a;
    }

    // ------------------------------------------------------------------------
    // Isolated Synchronous Fallbacks (Pre-scheduler bare-metal setup only)
    // NOTE: MUST NOT be called inside coroutines or the reactive event loop!
    // See docs/architecture/DESIGN_RULES.md Section 12.
    // ------------------------------------------------------------------------
    virtual bool write_read_sync(uint8_t slave_addr,
                                 std::span<const uint8_t> tx_data,
                                 std::span<uint8_t> rx_data) = 0;

    // Synchronous write burst
    virtual bool write_sync(uint8_t slave_addr, std::span<const uint8_t> tx_data) = 0;

    // Synchronous read burst
    virtual bool read_sync(uint8_t slave_addr, std::span<uint8_t> rx_data) = 0;

    /*
     * C++20 Coroutine Async Awaiter
     */
    struct AsyncI2cAwaiter {
        AsyncI2cDriver& driver;
        I2cRequest      request;
        I2cResult       result{};

        AsyncI2cAwaiter(AsyncI2cDriver& drv, uint8_t slave_addr,
                        std::span<const uint8_t> tx, std::span<uint8_t> rx)
            : driver(drv) {
            request.slave_addr = slave_addr;
            request.tx_data = tx;
            request.rx_data = rx;
            request.is_read = !rx.empty();
            request.repeated_start = (!tx.empty() && !rx.empty());
        }

        bool await_ready() const noexcept {
            return request.tx_data.empty() && request.rx_data.empty();
        }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            request.coro_handle = handle;
            request.result_slot = &result;
            if (!driver.submit_request(request)) {
                result.status = I2cStatus::QueueFull;
                return false; // resume immediately; never resume inline
            }
            return true;
        }

        I2cResult await_resume() noexcept {
            return result;
        }
    };

    // @impl [SPEC-HAL-11] docs/DESIGN_SPECIFICATION.md
    // Deprecated [SPEC-HAL-11]: 8-bit address, one write + one read, no timeout.
    // Use transfer_async(uint16_t, msgs, timeout_us).
    AsyncI2cAwaiter transfer_async(uint8_t slave_addr,
                                   std::span<const uint8_t> tx,
                                   std::span<uint8_t> rx) noexcept {
        return AsyncI2cAwaiter(*this, slave_addr, tx, rx);
    }

    /*
     * C++20 Coroutine Async Register Read Awaiter (Zero-Heap, Frame-Embedded Single-Byte Buffers)
     */
    struct AsyncI2cRegReadAwaiter {
        AsyncI2cDriver& driver;
        uint8_t         reg_byte;
        uint8_t         rx_byte{0};
        I2cRequest      request{};
        I2cResult       result{};

        AsyncI2cRegReadAwaiter(AsyncI2cDriver& drv, uint8_t slave_addr, uint8_t reg)
            : driver(drv), reg_byte(reg) {
            request.slave_addr = slave_addr;
            request.use_register = true;
            request.register_offset = reg;
            request.is_read = true;
            request.repeated_start = true;
        }

        bool await_ready() const noexcept { return false; }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            request.tx_data = std::span<const uint8_t>(&reg_byte, 1);
            request.rx_data = std::span<uint8_t>(&rx_byte, 1);
            request.coro_handle = handle;
            request.result_slot = &result;
            if (!driver.submit_request(request)) {
                result.status = I2cStatus::QueueFull;
                return false; // resume immediately; never resume inline
            }
            return true;
        }

        uint8_t await_resume() noexcept {
            return rx_byte;
        }
    };

    /*
     * C++20 Coroutine Async Register Write Awaiter (Zero-Heap, Frame-Embedded Buffer)
     */
    struct AsyncI2cRegWriteAwaiter {
        AsyncI2cDriver&        driver;
        std::array<uint8_t, 2> tx_buf;
        I2cRequest             request{};
        I2cResult              result{};

        AsyncI2cRegWriteAwaiter(AsyncI2cDriver& drv, uint8_t slave_addr, uint8_t reg, uint8_t val)
            : driver(drv), tx_buf{reg, val} {
            request.slave_addr = slave_addr;
            request.use_register = true;
            request.register_offset = reg;
            request.is_read = false;
            request.repeated_start = false;
        }

        bool await_ready() const noexcept { return false; }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            request.tx_data = tx_buf;
            request.rx_data = {};
            request.coro_handle = handle;
            request.result_slot = &result;
            if (!driver.submit_request(request)) {
                result.status = I2cStatus::QueueFull;
                return false; // resume immediately; never resume inline
            }
            return true;
        }

        I2cResult await_resume() noexcept {
            return result;
        }
    };

    // Deprecated [SPEC-HAL-11]: returns only the byte (status lost); use read_regs_async.
    AsyncI2cRegReadAwaiter read_reg_async(uint8_t slave_addr, uint8_t reg) noexcept {
        return AsyncI2cRegReadAwaiter(*this, slave_addr, reg);
    }

    // Deprecated [SPEC-HAL-11]: one byte only; use write_regs_async.
    AsyncI2cRegWriteAwaiter write_reg_async(uint8_t slave_addr, uint8_t reg, uint8_t val) noexcept {
        return AsyncI2cRegWriteAwaiter(*this, slave_addr, reg, val);
    }
};

using II2c = AsyncI2cDriver<16>;

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_I2C_HPP
