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

#include <concepts>
#include <coroutine>
#include <cstddef>

#include <etl/memory_model.h>
#include <etl/queue_spsc_isr.h>

#include "abstractx/interrupt_lock.hpp"

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

// Backward-compatible name for the default ISR-safe queue policy.
using IsrSafeSpscQueue = IsrSafeCoroutineQueue<32>;

// Generic domain bridge: coroutine domain <-> I/O or ISR domain.
template <typename QueuePolicy>
    requires DomainQueuePolicy<QueuePolicy>
class DomainDispatcher {
public:
    using value_type = typename QueuePolicy::value_type;
    static constexpr size_t QUEUE_CAPACITY = static_cast<size_t>(QueuePolicy::MAX_SIZE);

    // I/O domain producer: called from an ISR, where interrupts are already masked.
    static inline bool post(value_type handle) noexcept {
        return is_resumable(handle) && queue_.push_from_isr(handle);
    }

    static inline bool isr_post_resume(value_type handle) noexcept {
        return post(handle);
    }

    // Coroutine domain producer: masks interrupts for the duration of the enqueue.
    static inline bool push(value_type handle) noexcept {
        return is_resumable(handle) && queue_.push(handle);
    }

    // Coroutine domain consumer: each pop is taken with interrupts masked, and the
    // handle is resumed outside of the critical section.
    static inline void process_ready_coroutines() noexcept {
        size_t count = queue_.size();
        value_type handle{};
        while (count-- > 0 && queue_.pop(handle)) {
            if (is_resumable(handle) && !handle.done()) {
                handle.resume();
            }
        }
    }


    static inline void process() noexcept {
        process_ready_coroutines();
    }

    static inline bool has_pending_resumes() noexcept {
        return !queue_.empty_from_isr();
    }

    static inline void reset() noexcept {
        queue_.clear();
    }

private:
    static inline bool is_resumable(value_type handle) noexcept {
        return static_cast<bool>(handle) && handle.address() != nullptr;
    }

    static inline QueuePolicy queue_{};
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
