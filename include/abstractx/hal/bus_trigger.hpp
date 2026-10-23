/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: triggered reads ([SPEC-HAL-13]). A pin edge or a timer starts a prepared read
 * in the driver's interrupt; the hardware reads straight into a ring slot; a coroutine pops it.
 * One producer (the driver's interrupt), one consumer coroutine.
 */

#ifndef ABSTRACTX_HAL_BUS_TRIGGER_HPP
#define ABSTRACTX_HAL_BUS_TRIGGER_HPP

#include <array>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <span>

#include "abstractx/domain_dispatcher.hpp"
#include "io_status.hpp"

namespace abstractx::hal {

// @impl [SPEC-HAL-13] docs/DESIGN_SPECIFICATION.md
enum class TriggerKind : uint8_t { PinRising = 0, PinFalling = 1, PinBoth = 2, Periodic = 3 };

constexpr uint8_t trigger_kind_bit(TriggerKind k) noexcept {
    return static_cast<uint8_t>(1U << static_cast<uint8_t>(k));
}

struct TriggerSource {
    TriggerKind kind{TriggerKind::PinRising};
    uint16_t    pin{0};          // target pin number (pin kinds)
    uint32_t    period_us{0};    // Periodic only
};

// One completed read: a span into the ring slot (valid until release()).
struct TriggerSample {
    IoStatus                 status{IoStatus::Ok};
    std::span<const uint8_t> data{};
    uint64_t                 timestamp_us{0};
    uint32_t                 dropped{0};   // events skipped (ring full) since the previous sample
};

// Per-slot outcome, written by the producer before the slot is published.
struct TriggerSlotMeta {
    IoStatus status{IoStatus::Ok};
    uint32_t bytes{0};
    uint64_t timestamp_us{0};
    uint32_t dropped_at{0};   // dropped_ counter when this slot was committed
};

class TriggerRingBase {
public:
    TriggerRingBase(const TriggerRingBase&) = delete;
    TriggerRingBase& operator=(const TriggerRingBase&) = delete;

    size_t slot_bytes() const noexcept { return slot_bytes_; }
    size_t slots() const noexcept { return slots_; }

    // ---- producer (driver interrupt) --------------------------------------------------------

    // A free slot for the next read, or empty when every slot is full (event counted as dropped;
    // the driver then skips the bus transfer).
    std::span<uint8_t> acquire_from_isr() noexcept {
        const uint32_t w = wr_.load(std::memory_order_relaxed);
        if (w - rd_.load(std::memory_order_acquire) >= slots_) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return {};
        }
        return std::span<uint8_t>(storage_.data() + (w % slots_) * slot_bytes_, slot_bytes_);
    }

    // Publish the slot from acquire_from_isr() with its outcome and the event time.
    void commit_from_isr(IoStatus status, size_t bytes, uint64_t timestamp_us) noexcept {
        const uint32_t w = wr_.load(std::memory_order_relaxed);
        TriggerSlotMeta& m = meta_[w % slots_];
        m.status = status;
        m.bytes = static_cast<uint32_t>(bytes < slot_bytes_ ? bytes : slot_bytes_);
        m.timestamp_us = timestamp_us;
        m.dropped_at = dropped_.load(std::memory_order_relaxed);
        wr_.store(w + 1U, std::memory_order_release);
        wake_from_isr();
    }

    // ---- consumer (coroutine) ---------------------------------------------------------------

    struct PopAwaiter {
        TriggerRingBase* ring{nullptr};
        bool             busy{false};

        bool await_ready() noexcept {
            if (ring->waiter_.load(std::memory_order_acquire) != nullptr || ring->holding_) {
                busy = true;                  // one consumer, one sample held at a time
                return true;
            }
            if (ring->cancel_req_.load(std::memory_order_acquire)) {
                ring->reason_.store(kReasonCancel, std::memory_order_relaxed);
                return true;                  // a cancel that found no waiter is delivered here
            }
            ring->reason_.store(kReasonData, std::memory_order_relaxed);
            return ring->available();
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept {
            ring->reason_.store(kReasonNone, std::memory_order_relaxed);
            ring->waiter_.store(h.address(), std::memory_order_release);
            if (ring->available()) {          // re-check after publishing: never miss a commit
                if (ring->waiter_.exchange(nullptr, std::memory_order_acq_rel) != nullptr) {
                    ring->reason_.store(kReasonData, std::memory_order_relaxed);
                    return false;
                }
            }
            return true;
        }

        TriggerSample await_resume() noexcept {
            if (busy) {
                return TriggerSample{IoStatus::QueueFull};
            }
            if (ring->reason_.load(std::memory_order_relaxed) == kReasonCancel) {
                ring->cancel_req_.store(false, std::memory_order_relaxed);
                return TriggerSample{IoStatus::Cancelled};
            }
            return ring->take();
        }
    };

    PopAwaiter pop() noexcept { return PopAwaiter{this}; }

    // The consumer is done with the sample from pop(); its slot is free again.
    void release() noexcept {
        if (holding_) {
            holding_ = false;
            rd_.store(rd_.load(std::memory_order_relaxed) + 1U, std::memory_order_release);
        }
    }

    // Complete a waiting pop() with Cancelled (posted, never resumed inline). If the producer is
    // holding the waiter at this moment, it sees the request and delivers the cancel itself.
    void cancel() noexcept {
        cancel_req_.store(true, std::memory_order_release);
        void* h = waiter_.exchange(nullptr, std::memory_order_acq_rel);
        if (h != nullptr) {
            reason_.store(kReasonCancel, std::memory_order_relaxed);
            (void)Dispatcher::push(std::coroutine_handle<>::from_address(h));
        }
    }

    uint32_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    size_t depth() const noexcept {
        return wr_.load(std::memory_order_acquire) - rd_.load(std::memory_order_acquire);
    }

protected:
    TriggerRingBase(std::span<uint8_t> storage, std::span<TriggerSlotMeta> meta, size_t slot_bytes) noexcept
        : storage_(storage), meta_(meta), slot_bytes_(slot_bytes), slots_(static_cast<uint32_t>(meta.size())) {}

private:
    static constexpr uint8_t kReasonNone = 0;
    static constexpr uint8_t kReasonData = 1;
    static constexpr uint8_t kReasonCancel = 2;

    bool available() const noexcept {
        return wr_.load(std::memory_order_acquire) != rd_.load(std::memory_order_acquire);
    }

    TriggerSample take() noexcept {
        const uint32_t r = rd_.load(std::memory_order_relaxed);
        const TriggerSlotMeta& m = meta_[r % slots_];
        TriggerSample s{m.status,
                        std::span<const uint8_t>(storage_.data() + (r % slots_) * slot_bytes_, m.bytes),
                        m.timestamp_us, m.dropped_at - dropped_seen_};
        dropped_seen_ = m.dropped_at;
        holding_ = true;
        return s;
    }

    // A commit's wake can arrive after the consumer already took that slot without waiting and
    // went back to waiting: then there is nothing to read, so the waiter is put back instead of
    // resumed (a resume would read a slot never written). Only the single producer does this,
    // so no data wake is lost; a cancel racing with it is carried by cancel_req_.
    void wake_from_isr() noexcept {
        void* h = waiter_.exchange(nullptr, std::memory_order_acq_rel);
        if (h == nullptr) {
            return;
        }
        if (!available() && !cancel_req_.load(std::memory_order_acquire)) {
            waiter_.store(h, std::memory_order_release);
            if (!cancel_req_.load(std::memory_order_acquire)) {
                return;
            }
            h = waiter_.exchange(nullptr, std::memory_order_acq_rel);
            if (h == nullptr) {
                return;       // cancel() took it
            }
        }
        reason_.store(available() ? kReasonData : kReasonCancel, std::memory_order_relaxed);
        (void)Dispatcher::post(std::coroutine_handle<>::from_address(h));
    }

    std::span<uint8_t>         storage_;
    std::span<TriggerSlotMeta> meta_;
    size_t                slot_bytes_;
    uint32_t              slots_;
    std::atomic<uint32_t> wr_{0};        // slots committed (producer-owned, monotonic)
    std::atomic<uint32_t> rd_{0};        // slots released (consumer-owned, monotonic)
    std::atomic<uint32_t> dropped_{0};   // producer-owned
    std::atomic<void*>    waiter_{nullptr};
    std::atomic<uint8_t>  reason_{kReasonNone};
    std::atomic<bool>     cancel_req_{false};
    uint32_t              dropped_seen_{0};   // consumer-owned
    bool                  holding_{false};    // consumer-owned
};

template <size_t SlotBytes, size_t Slots>
struct TriggerRingStorage {
    alignas(8) std::array<uint8_t, SlotBytes * Slots> trig_mem{};
    std::array<TriggerSlotMeta, Slots>                trig_meta{};
};

// N slots of SlotBytes each, owned by the application (static) and handed to arm_triggered_read.
template <size_t SlotBytes, size_t Slots = 4>
class TriggerRing : private TriggerRingStorage<SlotBytes, Slots>, public TriggerRingBase {
    static_assert(SlotBytes > 0 && Slots >= 2, "[SPEC-HAL-13]: at least two slots");

public:
    TriggerRing() noexcept
        : TriggerRingStorage<SlotBytes, Slots>(),
          TriggerRingBase(std::span<uint8_t>(this->trig_mem), std::span<TriggerSlotMeta>(this->trig_meta), SlotBytes) {}
};

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_BUS_TRIGGER_HPP
