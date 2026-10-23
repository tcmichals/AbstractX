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
#include "uart_status.hpp"
#include "uart_rx.hpp"

namespace abstractx::hal {

// Line format, flow control and inter-byte gap ([SPEC-HAL-06]).
// @impl [SPEC-HAL-06] docs/DESIGN_SPECIFICATION.md
enum class UartParity : uint8_t { None = 0, Even = 1, Odd = 2 };
enum class UartFlow : uint8_t { None = 0, RtsCts = 1 };

struct UartConfig {
    uint32_t   baud{115200};
    uint8_t    data_bits{8};
    uint8_t    stop_bits{1};
    UartParity parity{UartParity::None};
    UartFlow   flow{UartFlow::None};
    uint32_t   tx_gap_us{0};  // idle time after every byte's stop bit (hardware gap only)
};

// What the hardware behind a driver can do; a field is set only when verified for that chip.
struct UartCapabilities {
    uint32_t max_baud{0};               // 0 = unknown: any baud the driver accepts
    uint16_t fifo_bytes{0};
    uint8_t  min_data_bits{8};
    uint8_t  max_data_bits{8};
    uint8_t  max_stop_bits{1};
    bool     parity{false};
    bool     rts_cts{false};
    bool     rx_dma{false};
    bool     tx_dma{false};
    uint32_t max_tx_gap_us{0};          // 0 = no hardware inter-byte gap
    uint32_t tx_gap_step_ns{0};         // gap resolution
    bool     tx_done_at_line_idle{false};  // write completes when the last stop bit has left
};

// Pure check of a request against capabilities ([SPEC-HAL-06]); Ok or NotSupported.
constexpr UartStatus check_uart_config(const UartCapabilities& caps, const UartConfig& cfg) noexcept {
    if (cfg.baud == 0 || (caps.max_baud != 0 && cfg.baud > caps.max_baud)) return UartStatus::NotSupported;
    if (cfg.data_bits < caps.min_data_bits || cfg.data_bits > caps.max_data_bits) return UartStatus::NotSupported;
    if (cfg.stop_bits < 1 || cfg.stop_bits > caps.max_stop_bits) return UartStatus::NotSupported;
    if (cfg.parity != UartParity::None && !caps.parity) return UartStatus::NotSupported;
    if (cfg.flow != UartFlow::None && !caps.rts_cts) return UartStatus::NotSupported;
    if (cfg.tx_gap_us > caps.max_tx_gap_us) return UartStatus::NotSupported;
    return UartStatus::Ok;
}

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
// @impl [SPEC-HAL-11] docs/DESIGN_SPECIFICATION.md
// Deprecated [SPEC-HAL-11]: never used by a driver; receive is read_async() ([SPEC-HAL-10]).
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
    // read_byte()/read(): polling, pre-scheduler bring-up only; coroutines use read_async ([SPEC-HAL-11]).
    virtual bool read_byte(uint8_t& ch) = 0;
    virtual size_t read(std::span<uint8_t> buffer) = 0;
    virtual void flush() = 0;
    virtual bool set_baud_rate(uint32_t baudrate) { (void)baudrate; return true; }

    // [SPEC-HAL-06] What this driver's hardware supports; the default is plain 8N1, no gap.
    virtual UartCapabilities capabilities() const noexcept { return {}; }

    // [SPEC-HAL-10] The driver's receive ring; null (the default) = read_async is NotSupported.
    // @impl [SPEC-HAL-10] docs/DESIGN_SPECIFICATION.md
    virtual UartRxRingBase* rx_ring() noexcept { return nullptr; }

    // Resume when >= min_bytes are buffered, an end of frame delivered data, timeout_us elapsed
    // (0 = no deadline) or cancel_read(); the view's spans point into the ring (no copy).
    UartRxRingBase::ReadAwaiter read_async(size_t min_bytes = 1, uint32_t timeout_us = 0) noexcept {
        return UartRxRingBase::ReadAwaiter{rx_ring(), min_bytes, timeout_us};
    }

    // Give back n parsed bytes of the last view; OverrunError if they were overwritten meanwhile.
    UartStatus release_rx(size_t n) noexcept {
        UartRxRingBase* r = rx_ring();
        return r != nullptr ? r->release(n) : UartStatus::NotSupported;
    }

    void cancel_read() noexcept {
        if (UartRxRingBase* r = rx_ring()) r->cancel_read();
    }

    UartRxStats rx_stats() noexcept {
        UartRxRingBase* r = rx_ring();
        return r != nullptr ? r->stats() : UartRxStats{};
    }

    // [SPEC-HAL-06] Check against capabilities(), then apply (configuration, not I/O: no waiting).
    UartStatus configure(const UartConfig& cfg) noexcept {
        const UartStatus st = check_uart_config(capabilities(), cfg);
        return st == UartStatus::Ok ? apply_config(cfg) : st;
    }

protected:
    // Called only with a config that passed check_uart_config(); the default sets the baud.
    virtual UartStatus apply_config(const UartConfig& cfg) noexcept {
        return set_baud_rate(cfg.baud) ? UartStatus::Ok : UartStatus::NotSupported;
    }

public:

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

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            request.coro_handle = handle;
            request.result_slot = &result;
            if (!driver.submit_request(request)) {
                result.status = UartStatus::QueueFull;
                return false; // resume immediately; never resume inline
            }
            return true;
        }

        UartResult await_resume() noexcept {
            return result;
        }
    };

    AsyncWriteAwaiter write_async(std::span<const uint8_t> buffer) noexcept {
        return AsyncWriteAwaiter(*this, buffer);
    }

    /*
     * C++20 Coroutine Async Byte Write Awaiter (Zero Heap, Frame-Embedded Byte)
     */
    struct AsyncByteWriteAwaiter {
        AsyncUartDriver& driver;
        uint8_t          byte_val;
        UartTxRequest    request{};
        UartResult       result{};

        AsyncByteWriteAwaiter(AsyncUartDriver& drv, uint8_t b)
            : driver(drv), byte_val(b) {}

        bool await_ready() const noexcept { return false; }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            request.data = std::span<const uint8_t>(&byte_val, 1);
            request.coro_handle = handle;
            request.result_slot = &result;
            if (!driver.submit_request(request)) {
                result.status = UartStatus::QueueFull;
                return false; // resume immediately; never resume inline
            }
            return true;
        }

        UartResult await_resume() noexcept {
            return result;
        }
    };

    AsyncByteWriteAwaiter write_byte_async(uint8_t b) noexcept {
        return AsyncByteWriteAwaiter(*this, b);
    }
};

using IUart = AsyncUartDriver<32>;

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_UART_HPP
