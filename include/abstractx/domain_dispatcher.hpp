/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Domain Bridge / Coroutine-I/O Dispatcher
 * -------------------------------------------------
 * Canonical bridge between the cooperative coroutine domain and the I/O domain
 * (ISR / driver context). The transport is an ETL SPSC queue whose critical
 * sections are protected by an interrupt save/restore access policy:
 *
 *   coroutine domain -> queue : locked calls   (push / pop)
 *   I/O domain (ISR)  -> queue : unlocked calls (push_from_isr / pop_from_isr)
 *
 * The queue policy is a template parameter, so a platform may substitute a
 * thread-aware or core-to-core queue without changing the dispatcher API.
 */

#ifndef ABSTRACTX_DOMAIN_DISPATCHER_HPP
#define ABSTRACTX_DOMAIN_DISPATCHER_HPP

#include <atomic>
#include <concepts>
#include <coroutine>
#include <cstddef>

#include <etl/memory_model.h>
#include <etl/queue_spsc_isr.h>

#include "abstractx/interrupt_lock.hpp"
#include "abstractx/trace/runtime_stats.hpp"

namespace abstractx {

// Queue policy contract for the domain bridge: every queue must expose both the
// coroutine-domain (locked) and the I/O-domain (already masked) entry points.
template <typename QueuePolicy>
concept DomainQueuePolicy = requires(QueuePolicy queue,
                                     typename QueuePolicy::value_type handle,
                                     typename QueuePolicy::value_type& out_handle) {
    { QueuePolicy::MAX_SIZE } -> std::convertible_to<size_t>;
    { queue.push(handle) } -> std::same_as<bool>;
    { queue.push_from_isr(handle) } -> std::same_as<bool>;
    { queue.pop(out_handle) } -> std::same_as<bool>;
    { queue.pop_from_isr(out_handle) } -> std::same_as<bool>;
    { queue.empty_from_isr() } -> std::same_as<bool>;
    { queue.clear() };
};

// ISR-safe coroutine handoff queue: ETL SPSC queue + interrupt save/restore lock.
template <size_t Capacity = 32>
using IsrSafeCoroutineQueue = etl::queue_spsc_isr<std::coroutine_handle<>,
                                                  Capacity,
                                                  InterruptLock,
                                                  etl::memory_model::MEMORY_MODEL_SMALL>;

// @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (5a: ready-queue size per application)
#ifndef ABSTRACTX_READY_QUEUE_SIZE
#if ABSTRACTX_HOSTED
#define ABSTRACTX_READY_QUEUE_SIZE 255
#else
#define ABSTRACTX_READY_QUEUE_SIZE 64
#endif
#endif
static_assert(ABSTRACTX_READY_QUEUE_SIZE >= 8 && ABSTRACTX_READY_QUEUE_SIZE <= 255,
              "ABSTRACTX_READY_QUEUE_SIZE: 8..255 (ETL small memory model)");

// The default executor's ready ring.
using IsrSafeSpscQueue = IsrSafeCoroutineQueue<ABSTRACTX_READY_QUEUE_SIZE>;

// Generic domain bridge: coroutine domain <-> I/O or ISR domain.
template <typename QueuePolicy>
    requires DomainQueuePolicy<QueuePolicy>
class DomainDispatcher {
public:
    using value_type = typename QueuePolicy::value_type;
    static constexpr size_t QUEUE_CAPACITY = static_cast<size_t>(QueuePolicy::MAX_SIZE);

    // I/O domain producer: safe from both ISR and non-ISR contexts because queue_.push()
    // uses InterruptLock (saves/restores interrupt state on single-core bare metal, takes
    // spinlock/mutex on SMP and hosted builds).
    // A false return means the coroutine will never be resumed; it is counted in
    // dropped_resumes() so a hang caused by an undersized queue is diagnosable.
    // @impl [SPEC-ARCH-01] docs/DESIGN_SPECIFICATION.md#spec-arch-01
    static inline bool post(value_type handle) noexcept {
        if (!is_resumable(handle)) {
            return false;
        }
        return record(queue_.push(handle));
    }

    static inline bool isr_post_resume(value_type handle) noexcept {
        return post(handle);
    }

    // Coroutine domain producer: masks interrupts for the duration of the enqueue.
    static inline bool push(value_type handle) noexcept {
        if (!is_resumable(handle)) {
            return false;
        }
        return record(queue_.push(handle));
    }

    // Resumes lost because the queue was full. Must stay zero in a healthy system.
    static inline uint32_t dropped_resumes() noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

    // Coroutine domain consumer: each pop is taken with interrupts masked, and the
    // handle is resumed outside of the critical section.
    static inline void process_ready_coroutines() noexcept {
        size_t count = queue_.size();
        value_type handle{};
        while (count-- > 0 && queue_.pop(handle)) {
            if (is_resumable(handle) && !handle.done()) {
                // @impl [SPEC-TRACE-08] docs/DESIGN_SPECIFICATION.md
                stats::ResumeScope charge(handle.address());
                // resume: the Dispatcher is the one place that wakes coroutines ([SPEC-ARCH-10])
                handle.resume();
            }
        }
    }


    static inline void process() noexcept {
        process_ready_coroutines();
    }

    static inline size_t queue_depth() noexcept {
        return queue_.size();
    }

    // @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (8: a destroyed task is never resumed)
    // Remove every copy of `handle` from the ring (its frame is being destroyed). Returns how many.
    static inline size_t purge(value_type handle) noexcept {
        if (!handle) return 0;
        InterruptGuard guard;
        size_t removed = 0;
        const size_t n = queue_.size();
        value_type h{};
        for (size_t i = 0; i < n && queue_.pop_from_isr(h); ++i) {
            if (h.address() == handle.address()) { ++removed; continue; }
            (void)queue_.push_from_isr(h);  // re-queue in order; the ring had room for it
        }
        return removed;
    }

    // Deepest the ring has been since start (or the last reset).
    static inline uint32_t high_water(bool reset = false) noexcept {
        return reset ? high_water_.exchange(0, std::memory_order_relaxed)
                     : high_water_.load(std::memory_order_relaxed);
    }

    static inline bool has_pending_resumes() noexcept {
        return !queue_.empty();
    }

    static inline void reset() noexcept {
        queue_.clear();
        dropped_.store(0, std::memory_order_relaxed);
        high_water_.store(0, std::memory_order_relaxed);
    }

private:
    static inline bool record(bool pushed) noexcept {
        if (!pushed) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        } else {
            // @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (5a: high-water mark)
            const uint32_t depth = static_cast<uint32_t>(queue_.size());
            uint32_t hw = high_water_.load(std::memory_order_relaxed);
            while (depth > hw && !high_water_.compare_exchange_weak(hw, depth, std::memory_order_relaxed)) {}
        }
        return pushed;
    }

    static inline bool is_resumable(value_type handle) noexcept {
        return static_cast<bool>(handle) && handle.address() != nullptr;
    }

    static inline QueuePolicy queue_{};
    static inline std::atomic<uint32_t> dropped_{0};
    static inline std::atomic<uint32_t> high_water_{0};  // shared: any producer (CAS max); readers
};

// Canonical public API: the dispatcher is domain-oriented, not ISR-only.
using Dispatcher = DomainDispatcher<IsrSafeSpscQueue>;

// Backward-compatible alias for the current RISC-V ISR-safe implementation.
using IsrDispatcher = Dispatcher;

// Dispatcher-aware yield awaiter: yields control and reschedules the coroutine in Dispatcher.
struct DispatcherYieldAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        Dispatcher::push(h);
    }
    void await_resume() const noexcept {}
};

inline DispatcherYieldAwaiter yield_to_dispatcher() noexcept {
    return {};
}

} // namespace abstractx

#endif // ABSTRACTX_DOMAIN_DISPATCHER_HPP
