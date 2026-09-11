/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: SPI & Dual-SPI DMA Split-Queue Interface
 * -------------------------------------------------------
 * Hardware interface for single and dual SPI high-speed TLP streaming
 * with DMA/ISR request/completion queues and coroutine awaiters.
 */

#ifndef ABSTRACTX_HAL_SPI_HPP
#define ABSTRACTX_HAL_SPI_HPP

#include <cstdint>
#include <cstddef>
#include <span>
#include <coroutine>

#include "async_driver.hpp"

namespace abstractx::hal {

enum class SpiMode : uint8_t {
    Mode0 = 0, // CPOL=0, CPHA=0
    Mode1 = 1, // CPOL=0, CPHA=1
    Mode2 = 2, // CPOL=1, CPHA=0
    Mode3 = 3  // CPOL=1, CPHA=1
};

enum class SpiStatus : uint8_t {
    Ok = 0,
    DmaError = 1,
    Timeout = 2,
    CrcError = 3,
    QueueFull = 4
};

struct SpiConfig {
    uint32_t frequency_hz{10'000'000};
    SpiMode  mode{SpiMode::Mode0};
    bool     lsb_first{false};
    bool     use_dma{true};
    bool     dual_spi{false};
};

/*
 * SPI Transfer Result Descriptor (Output Queue element)
 */
struct SpiResult {
    SpiStatus status{SpiStatus::Ok};
    size_t    transferred_bytes{0};
    uint64_t  timestamp_us{0};
    uint32_t  queue_latency_us{0};
    uint32_t  bus_duration_us{0};
};

/*
 * SPI Transfer Request Descriptor (Input Queue element)
 */
struct SpiRequest : public AsyncTransactionMeta<SpiResult> {
    std::span<const uint8_t> tx_data{};
    std::span<uint8_t>       rx_data{};
    std::span<const uint8_t> tx_data_link_b{}; // For Dual-SPI
    std::span<uint8_t>       rx_data_link_b{}; // For Dual-SPI
    uint32_t                 cs_pin{0};
    bool                     auto_cs{true};
    bool                     is_dual_spi{false};
};

// @impl [SPEC-HAL-02] docs/DESIGN_SPECIFICATION.md#spec-hal-02
// @status Complete
template <size_t QueueDepth = 16>
class AsyncSpiDriver : public AsyncDriverBase<SpiRequest, SpiResult, QueueDepth> {
public:
    virtual bool init(const SpiConfig& config) = 0;
    virtual void select(bool active) = 0;

    // Synchronous fallback
    virtual uint8_t transfer_byte(uint8_t tx) = 0;
    virtual bool transfer_sync(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data) = 0;

    /*
     * C++20 Coroutine Async Transfer Awaiter
     */
    struct AsyncTransferAwaiter {
        AsyncSpiDriver& driver;
        SpiRequest      request;
        SpiResult       result{};

        AsyncTransferAwaiter(AsyncSpiDriver& drv,
                             std::span<const uint8_t> tx,
                             std::span<uint8_t> rx,
                             bool dual = false)
            : driver(drv) {
            request.tx_data = tx;
            request.rx_data = rx;
            request.is_dual_spi = dual;
        }

        bool await_ready() const noexcept {
            return request.tx_data.empty() && request.rx_data.empty();
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            request.coro_handle = handle;
            if (!driver.submit_request(request)) {
                result.status = SpiStatus::QueueFull;
                if (handle && !handle.done()) {
                    handle.resume();
                }
            }
        }

        SpiResult await_resume() noexcept {
            driver.pop_completion(result);
            return result;
        }
    };

    AsyncTransferAwaiter transfer_async(std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept {
        return AsyncTransferAwaiter(*this, tx, rx, false);
    }

    AsyncTransferAwaiter transfer_dual_async(std::span<const uint8_t> tx_a,
                                             std::span<const uint8_t> tx_b,
                                             std::span<uint8_t> rx_a,
                                             std::span<uint8_t> rx_b) noexcept {
        AsyncTransferAwaiter awaiter(*this, tx_a, rx_a, true);
        awaiter.request.tx_data_link_b = tx_b;
        awaiter.request.rx_data_link_b = rx_b;
        return awaiter;
    }
};

using ISpi = AsyncSpiDriver<16>;

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_SPI_HPP
