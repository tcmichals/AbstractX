/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: Asynchronous UART DMA/ISR Split-Queue Interface
 * --------------------------------------------------------------
 * Non-blocking serial I/O contract with DMA/ISR request/completion queues,
 * packet framing awaiters, and zero-allocation coroutine yields.
 */

#ifndef ABSTRACTX_HAL_UART_HPP
#define ABSTRACTX_HAL_UART_HPP

#include <cstdint>
#include <cstddef>
#include <span>
#include <coroutine>

#include "async_driver.hpp"

namespace abstractx::hal {

enum class UartStatus : uint8_t {
    Ok = 0,
    Timeout = 1,
    FramingError = 2,
    OverrunError = 3,
    QueueFull = 4
};

/*
 * UART Transfer Result Descriptor (Output Queue element)
 */
struct UartResult {
    UartStatus status{UartStatus::Ok};
    size_t     bytes_transferred{0};
    uint64_t   timestamp_us{0};
};

/*
 * UART TX Request Descriptor (Input Queue element)
 */
struct UartTxRequest : public AsyncTransactionMeta<UartResult> {
    std::span<const uint8_t> data{};
};

/*
 * UART RX Request Descriptor (Input Queue element)
 */
struct UartRxRequest : public AsyncTransactionMeta<UartResult> {
    std::span<uint8_t> destination_buffer{};
    size_t             min_bytes{1};
    uint32_t           timeout_ms{0};
    bool               delimiter_enabled{false};
    uint8_t            delimiter_char{'\n'};
};

// @impl [SPEC-HAL-03] docs/DESIGN_SPECIFICATION.md#spec-hal-03
// @status Complete
template <size_t QueueDepth = 32>
class AsyncUartDriver : public AsyncDriverBase<UartTxRequest, UartResult, QueueDepth> {
public:
    virtual void init(uint32_t baudrate) = 0;
    virtual void write_byte(uint8_t ch) = 0;
    virtual void puts(const char* str) = 0;
    virtual size_t write(std::span<const uint8_t> buffer) = 0;
    virtual bool read_byte(uint8_t& ch) = 0;
    virtual size_t read(std::span<uint8_t> buffer) = 0;
    virtual void flush() = 0;
    virtual bool set_baud_rate(uint32_t baudrate) { (void)baudrate; return true; }

    /*
     * C++20 Coroutine Async Write Awaiter
     */
    struct AsyncWriteAwaiter {
        AsyncUartDriver& driver;
        UartTxRequest    request;
        UartResult       result{};

        AsyncWriteAwaiter(AsyncUartDriver& drv, std::span<const uint8_t> buffer)
            : driver(drv) {
            request.data = buffer;
        }

        bool await_ready() const noexcept {
            return request.data.empty();
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            request.coro_handle = handle;
            if (!driver.submit_request(request)) {
                result.status = UartStatus::QueueFull;
                if (handle && !handle.done()) {
                    handle.resume();
                }
            }
        }

        UartResult await_resume() noexcept {
            driver.pop_completion(result);
            return result;
        }
    };

    AsyncWriteAwaiter write_async(std::span<const uint8_t> buffer) noexcept {
        return AsyncWriteAwaiter(*this, buffer);
    }
};

using IUart = AsyncUartDriver<32>;

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_UART_HPP
