/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: UART receive ring ([SPEC-HAL-10]).
 * Driver-owned storage filled by a circular DMA chain (or an RX interrupt); the reader awaits
 * "bytes available" and looks at them in place, then releases them. One producer (ISR / DMA
 * completion), one reader coroutine.
 */

#ifndef ABSTRACTX_HAL_UART_RX_HPP
#define ABSTRACTX_HAL_UART_RX_HPP

#include <array>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <span>

#include "abstractx/domain_dispatcher.hpp"
#include "uart_status.hpp"

namespace abstractx::hal {

// Line-error bits reported by the UART line-status interrupt.
namespace UartLineError {
inline constexpr uint8_t Overrun = 0x01;
inline constexpr uint8_t Framing = 0x02;
inline constexpr uint8_t Parity  = 0x04;
inline constexpr uint8_t Break   = 0x08;
} // namespace UartLineError

struct UartRxStats {
    uint32_t dropped_bytes{0};
    uint32_t overrun{0};
    uint32_t framing{0};
    uint32_t parity{0};
    uint32_t breaks{0};
};

// What a read returns: spans into the ring (second is the part after the wrap point).
struct UartRxView {
    UartStatus               status{UartStatus::Ok};
    std::span<const uint8_t> first{};
    std::span<const uint8_t> second{};
    uint32_t                 dropped{0};      // bytes lost since the previous view
    uint8_t                  line_errors{0};  // UartLineError bits seen since the previous view
    size_t size() const noexcept { return first.size() + second.size(); }
};

// @impl [SPEC-HAL-10] docs/DESIGN_SPECIFICATION.md
class UartRxRingBase {
public:
    // One-shot timer the driver provides for read deadlines; it later calls deadline_from_isr(token).
    using DeadlineFn = void (*)(void* ctx, uint32_t timeout_us, uint32_t token);

    UartRxRingBase(const UartRxRingBase&) = delete;
    UartRxRingBase& operator=(const UartRxRingBase&) = delete;

    void set_deadline_hook(DeadlineFn fn, void* ctx) noexcept {
        deadline_fn_ = fn;
        deadline_ctx_ = ctx;
    }

    // [SPEC-HAL-10] Byte handler: called by the producer, at interrupt level, with the bytes just
    // written (second span after the wrap point), before the reader is woken. Bounded, never
    // resumes; the bytes stay in the ring. Set it before the producer starts.
    // @impl [SPEC-HAL-10] docs/DESIGN_SPECIFICATION.md
    using DataFn = void (*)(void* ctx, std::span<const uint8_t> first, std::span<const uint8_t> second) noexcept;

    void set_data_hook(DataFn fn, void* ctx) noexcept {
        data_fn_ = fn;
        data_ctx_ = ctx;
    }

    // The contiguous buffers the DMA descriptor chain points at (buffer i = bytes [i*B, (i+1)*B)).
    std::span<uint8_t> dma_region() noexcept { return storage_; }
    uint32_t buffer_bytes() const noexcept { return buf_; }
    uint32_t capacity() const noexcept { return cap_; }
    // Largest min_bytes a read may ask for: the ring minus the buffer the DMA is filling.
    uint32_t max_read_bytes() const noexcept { return cap_ - buf_; }

    // ---- producer (ISR / DMA completion) ----------------------------------------------------

    // The hardware wrote `n` more bytes after the previous call; end_of_frame = RX timeout.
    void produced_from_isr(size_t n, bool end_of_frame) noexcept {
        const uint32_t w0 = wr_.load(std::memory_order_relaxed);
        if (data_fn_ != nullptr && n != 0U) {
            const uint32_t pos = w0 % cap_;
            const uint32_t len = static_cast<uint32_t>(n < cap_ ? n : cap_);
            const uint32_t n1 = len < cap_ - pos ? len : cap_ - pos;
            data_fn_(data_ctx_, std::span<const uint8_t>(storage_.data() + pos, n1),
                     std::span<const uint8_t>(storage_.data(), len - n1));
        }
        const uint32_t w = w0 + static_cast<uint32_t>(n);
        wr_.store(w, std::memory_order_release);
        if (end_of_frame) {
            eof_wr_.store(w, std::memory_order_release);
        }
        if (waiter_.load(std::memory_order_acquire) != nullptr && satisfied()) {
            wake(kReasonData, /*from_isr=*/true);
        }
    }

    // Line-status interrupt: count each error and flag it for the next view.
    void line_error_from_isr(uint8_t errs) noexcept {
        if ((errs & UartLineError::Overrun) != 0) overrun_.fetch_add(1, std::memory_order_relaxed);
        if ((errs & UartLineError::Framing) != 0) framing_.fetch_add(1, std::memory_order_relaxed);
        if ((errs & UartLineError::Parity) != 0) parity_.fetch_add(1, std::memory_order_relaxed);
        if ((errs & UartLineError::Break) != 0) breaks_.fetch_add(1, std::memory_order_relaxed);
        pending_errs_.fetch_or(errs, std::memory_order_relaxed);
    }

    // The driver's timer fired for the read identified by `token`; stale tokens are ignored.
    void deadline_from_isr(uint32_t token) noexcept {
        if (token == gen_.load(std::memory_order_acquire)) {
            wake(kReasonTimeout, /*from_isr=*/true);
        }
    }

    // ---- reader (coroutine) ------------------------------------------------------------------

    struct ReadAwaiter {
        UartRxRingBase* ring{nullptr};
        size_t          min_bytes{1};
        uint32_t        timeout_us{0};
        UartStatus      refused{UartStatus::Ok};

        bool await_ready() noexcept {
            if (ring == nullptr) {
                refused = UartStatus::NotSupported;          // driver has no receive ring
            } else if (min_bytes == 0 || min_bytes > ring->max_read_bytes()) {
                refused = UartStatus::InvalidArgument;
            } else if (timeout_us != 0 && ring->deadline_fn_ == nullptr) {
                refused = UartStatus::NotSupported;          // no timer: deadline refused, not faked
            } else if (ring->waiter_.load(std::memory_order_acquire) != nullptr) {
                refused = UartStatus::QueueFull;             // one reader per port
            } else {
                ring->cancel_req_.store(false, std::memory_order_relaxed);
                ring->want_.store(static_cast<uint32_t>(min_bytes), std::memory_order_relaxed);
                if (!ring->satisfied()) {
                    return false;
                }
                ring->reason_.store(kReasonData, std::memory_order_relaxed);
            }
            return true;
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept {
            ring->reason_.store(kReasonNone, std::memory_order_relaxed);
            const uint32_t token = ring->gen_.fetch_add(1, std::memory_order_acq_rel) + 1U;
            ring->waiter_.store(h.address(), std::memory_order_release);
            // Re-check after publishing: data or cancel that arrived in between must not be missed.
            if (ring->satisfied() || ring->cancel_req_.load(std::memory_order_acquire)) {
                if (ring->waiter_.exchange(nullptr, std::memory_order_acq_rel) != nullptr) {
                    const bool cancelled = ring->cancel_req_.load(std::memory_order_relaxed) && !ring->satisfied();
                    ring->reason_.store(cancelled ? kReasonCancel : kReasonData, std::memory_order_relaxed);
                    return false;   // resume now; never resume inline
                }
                return true;        // the producer or cancel_read() took it and posted the resume
            }
            if (timeout_us != 0) {
                ring->deadline_fn_(ring->deadline_ctx_, timeout_us, token);
            }
            return true;
        }

        UartRxView await_resume() noexcept {
            if (refused != UartStatus::Ok) {
                return UartRxView{refused};
            }
            const uint8_t reason = ring->reason_.load(std::memory_order_relaxed);
            if (reason == kReasonCancel) {
                return UartRxView{UartStatus::Cancelled};
            }
            return ring->make_view(reason == kReasonTimeout ? UartStatus::Timeout : UartStatus::Ok);
        }
    };

    // Resume when >= min_bytes are buffered, an end of frame delivered >= 1 byte, timeout_us
    // elapsed (0 = no deadline) or cancel_read() was called.
    ReadAwaiter read_async(size_t min_bytes, uint32_t timeout_us) noexcept {
        return ReadAwaiter{this, min_bytes, timeout_us};
    }

    // The reader parsed n bytes of the last view. OverrunError if the DMA overwrote any of them
    // while they were held (the reader must discard what it parsed from them).
    UartStatus release(size_t n) noexcept {
        if (n > held_) {
            return UartStatus::InvalidArgument;
        }
        const uint32_t rd = rd_.load(std::memory_order_relaxed);
        const uint32_t floor = valid_floor(wr_.load(std::memory_order_acquire));
        UartStatus st = UartStatus::Ok;
        if (static_cast<int32_t>(floor - rd) > 0) {
            const uint32_t over = floor - rd;
            const uint32_t lost = over < n ? over : static_cast<uint32_t>(n);
            if (lost != 0U) {
                dropped_total_ += lost;
                st = UartStatus::OverrunError;
            }
        }
        rd_.store(rd + static_cast<uint32_t>(n), std::memory_order_release);
        held_ = 0;
        return st;
    }

    // Complete a pending read with Cancelled (posted, not resumed inline).
    void cancel_read() noexcept {
        cancel_req_.store(true, std::memory_order_release);
        wake(kReasonCancel, /*from_isr=*/false);
    }

    bool reader_waiting() const noexcept { return waiter_.load(std::memory_order_acquire) != nullptr; }

    // Counters since start; bytes already overwritten but not yet seen by the reader included.
    UartRxStats stats() const noexcept {
        UartRxStats s{};
        const uint32_t rd = rd_.load(std::memory_order_relaxed);
        const uint32_t floor = valid_floor(wr_.load(std::memory_order_acquire));
        const uint32_t pending = static_cast<int32_t>(floor - rd) > 0 ? floor - rd : 0U;
        s.dropped_bytes = dropped_total_ + pending;
        s.overrun = overrun_.load(std::memory_order_relaxed);
        s.framing = framing_.load(std::memory_order_relaxed);
        s.parity = parity_.load(std::memory_order_relaxed);
        s.breaks = breaks_.load(std::memory_order_relaxed);
        return s;
    }

    // Copy up to max_len available bytes from the ring's spans and release them ([SPEC-E906-HAL-02]).
    // @impl [SPEC-E906-HAL-02] targets/allwinner_e906/SPECIFICATION.md
    size_t copy_out(uint8_t* dst, size_t max_len) noexcept {
        if (dst == nullptr || max_len == 0) return 0;
        const UartRxView v = make_view(UartStatus::Ok);
        size_t copied = 0;
        for (uint8_t b : v.first) {
            if (copied >= max_len) break;
            dst[copied++] = b;
        }
        if (copied < max_len) {
            for (uint8_t b : v.second) {
                if (copied >= max_len) break;
                dst[copied++] = b;
            }
        }
        (void)release(copied);
        return copied;
    }

    void reset() noexcept {
        wr_.store(0, std::memory_order_relaxed);
        eof_wr_.store(0, std::memory_order_relaxed);
        rd_.store(0, std::memory_order_relaxed);
        want_.store(1, std::memory_order_relaxed);
        waiter_.store(nullptr, std::memory_order_relaxed);
        reason_.store(kReasonNone, std::memory_order_relaxed);
        cancel_req_.store(false, std::memory_order_relaxed);
        pending_errs_.store(0, std::memory_order_relaxed);
        held_ = 0;
        dropped_total_ = 0;
        dropped_since_view_ = 0;
    }

    UartRxRingBase(std::span<uint8_t> storage, uint32_t buffer_bytes) noexcept
        : storage_(storage), buf_(buffer_bytes), cap_(static_cast<uint32_t>(storage.size())) {}

private:
    static constexpr uint8_t kReasonNone = 0;
    static constexpr uint8_t kReasonData = 1;
    static constexpr uint8_t kReasonTimeout = 2;
    static constexpr uint8_t kReasonCancel = 3;

    // First byte not being overwritten: the buffer the DMA is filling is lost from the old lap.
    uint32_t valid_floor(uint32_t w) const noexcept {
        const uint32_t dma_end = (w / buf_) * buf_ + buf_;
        return dma_end - cap_;
    }

    uint32_t valid_start(uint32_t w) const noexcept {
        const uint32_t rd = rd_.load(std::memory_order_acquire);
        const uint32_t floor = valid_floor(w);
        return static_cast<int32_t>(floor - rd) > 0 ? floor : rd;
    }

    // Same predicate on both sides, so a wake is never lost.
    bool satisfied() const noexcept {
        const uint32_t w = wr_.load(std::memory_order_acquire);
        const uint32_t start = valid_start(w);
        const uint32_t avail = w - start;
        const uint32_t eof = eof_wr_.load(std::memory_order_acquire);
        return avail >= want_.load(std::memory_order_relaxed) ||
               (avail > 0U && static_cast<int32_t>(eof - start) > 0);
    }

    void wake(uint8_t reason, bool from_isr) noexcept {
        void* h = waiter_.exchange(nullptr, std::memory_order_acq_rel);
        if (h == nullptr) {
            return;
        }
        if (reason == kReasonData && !satisfied() && !cancel_req_.load(std::memory_order_acquire)) {
            waiter_.store(h, std::memory_order_release);
            if (!cancel_req_.load(std::memory_order_acquire)) {
                return;
            }
            h = waiter_.exchange(nullptr, std::memory_order_acq_rel);
            if (h == nullptr) {
                return;
            }
            reason = kReasonCancel;
        }
        reason_.store(reason, std::memory_order_relaxed);
        const auto handle = std::coroutine_handle<>::from_address(h);
        if (from_isr) {
            (void)Dispatcher::post(handle);
        } else {
            (void)Dispatcher::push(handle);
        }
    }

    UartRxView make_view(UartStatus status) noexcept {
        const uint32_t w = wr_.load(std::memory_order_acquire);
        uint32_t rd = rd_.load(std::memory_order_relaxed);
        const uint32_t floor = valid_floor(w);
        if (static_cast<int32_t>(floor - rd) > 0) {        // lapped before the reader looked
            dropped_total_ += floor - rd;
            dropped_since_view_ += floor - rd;
            rd = floor;
            rd_.store(rd, std::memory_order_release);
        }
        const uint32_t avail = w - rd;
        const uint32_t pos = rd % cap_;
        const uint32_t n1 = avail < cap_ - pos ? avail : cap_ - pos;
        UartRxView v{status};
        v.first = std::span<const uint8_t>(storage_.data() + pos, n1);
        v.second = std::span<const uint8_t>(storage_.data(), avail - n1);
        v.dropped = dropped_since_view_;
        dropped_since_view_ = 0;
        v.line_errors = pending_errs_.exchange(0, std::memory_order_relaxed);
        held_ = avail;
        return v;
    }

    std::span<uint8_t> storage_;
    uint32_t buf_;
    uint32_t cap_;
    DeadlineFn deadline_fn_{nullptr};
    void* deadline_ctx_{nullptr};
    DataFn data_fn_{nullptr};
    void* data_ctx_{nullptr};

    std::atomic<uint32_t> wr_{0};       // bytes produced (monotonic, producer-owned)
    std::atomic<uint32_t> eof_wr_{0};   // wr_ at the last end of frame
    std::atomic<uint32_t> rd_{0};       // bytes released (monotonic, reader-owned)
    std::atomic<uint32_t> want_{1};
    std::atomic<uint32_t> gen_{0};      // read generation, for stale deadlines
    std::atomic<void*>    waiter_{nullptr};
    std::atomic<uint8_t>  reason_{kReasonNone};
    std::atomic<bool>     cancel_req_{false};
    std::atomic<uint8_t>  pending_errs_{0};
    std::atomic<uint32_t> overrun_{0};
    std::atomic<uint32_t> framing_{0};
    std::atomic<uint32_t> parity_{0};
    std::atomic<uint32_t> breaks_{0};
    uint32_t held_{0};                  // reader-owned
    uint32_t dropped_total_{0};         // reader-owned
    uint32_t dropped_since_view_{0};    // reader-owned
};

template <size_t Bytes>
struct UartRxStorage {
    alignas(4) std::array<uint8_t, Bytes> rx_mem{};
};

// Storage for NumBufs x BufBytes; total must be a power of two (monotonic counters wrap cleanly).
// The storage base is constructed before UartRxRingBase, which keeps a span of it.
template <size_t BufBytes = 64, size_t NumBufs = 4>
class UartRxRing : private UartRxStorage<BufBytes * NumBufs>, public UartRxRingBase {
    static_assert(BufBytes > 0 && NumBufs >= 2, "[SPEC-HAL-10]: at least two buffers");
    static_assert(((BufBytes * NumBufs) & (BufBytes * NumBufs - 1)) == 0,
                  "[SPEC-HAL-10]: ring size must be a power of two");

public:
    UartRxRing() noexcept
        : UartRxStorage<BufBytes * NumBufs>(),
          UartRxRingBase(std::span<uint8_t>(this->rx_mem), static_cast<uint32_t>(BufBytes)) {}
};

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_UART_RX_HPP
