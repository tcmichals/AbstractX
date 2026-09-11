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
#include <coroutine>

#include "async_driver.hpp"

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
    QueueFull = 5
};

struct I2cConfig {
    uint32_t frequency_hz{400'000};
    I2cSpeed speed{I2cSpeed::Fast_400k};
    bool     ten_bit_addr{false};
    bool     use_dma{false};
};

struct I2cResult {
    I2cStatus status{I2cStatus::Ok};
    size_t    transferred_bytes{0};
    uint64_t  timestamp_us{0};
};

struct I2cRequest : public AsyncTransactionMeta<I2cResult> {
    uint8_t                  slave_addr{0};
    bool                     is_read{false};
    bool                     repeated_start{false};
    uint8_t                  register_offset{0};
    bool                     use_register{false};
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

    // Synchronous combined write-then-read (Repeated Start)
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

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            request.coro_handle = handle;
            if (!driver.submit_request(request)) {
                result.status = I2cStatus::QueueFull;
                if (handle && !handle.done()) {
                    handle.resume();
                }
            }
        }

        I2cResult await_resume() noexcept {
            driver.pop_completion(result);
            return result;
        }
    };

    AsyncI2cAwaiter transfer_async(uint8_t slave_addr,
                                   std::span<const uint8_t> tx,
                                   std::span<uint8_t> rx) noexcept {
        return AsyncI2cAwaiter(*this, slave_addr, tx, rx);
    }
};

using II2c = AsyncI2cDriver<16>;

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_I2C_HPP
