/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: asynchronous timer. Every sleep is an absolute deadline in one sorted list;
 * the target provides one alarm for the earliest deadline ([SPEC-HAL-05]).
 */

#ifndef ABSTRACTX_HAL_TIMER_HPP
#define ABSTRACTX_HAL_TIMER_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <coroutine>

#include "abstractx/interrupt_lock.hpp"
#include "abstractx/domain_dispatcher.hpp"
#include "abstractx/deadline.hpp"
#include "abstractx/executor.hpp"

namespace abstractx::hal {

enum class TimerStatus : uint8_t {
    Ok = 0,
    Cancelled,
    Timeout,
    Error
};

struct TimerResult {
    TimerStatus status{TimerStatus::Ok};
    uint64_t    timestamp_us{0};
};

// @impl [SPEC-HAL-05] docs/DESIGN_SPECIFICATION.md#spec-hal-05
class AsyncTimerDriver {
public:
    // What the target's alarm does for the earliest deadline.
    enum class Alarm : uint8_t {
        Armed,        // its alarm calls expire_sleeps() at the deadline
        Polled,       // its alarm (if any) only wakes the loop; service_due_us() expires sleeps
        Unsupported,  // no alarm: a sleep completes at once with TimerStatus::Error
    };

    AsyncTimerDriver() = default;
    AsyncTimerDriver(const AsyncTimerDriver&) = delete;
    AsyncTimerDriver& operator=(const AsyncTimerDriver&) = delete;
    // Unlink every pending sleep, so a frame destroyed after the driver (static destruction at
    // exit) does not call into it.
    virtual ~AsyncTimerDriver() {
        abstractx::InterruptGuard guard;
        while (head_ != nullptr) unlink(*head_);
    }

    // High-resolution monotonic timestamp queries
    virtual uint64_t get_time_us() const = 0;
    virtual uint32_t get_time_ms() const = 0;

    // Synchronous busy-wait delays (strictly for boot-time / hardware reset only)
    virtual void delay_us(uint32_t us) = 0;
    virtual void delay_ms(uint32_t ms) = 0;

    // A sleep: the awaiter is the list node and lives in the coroutine frame.
    // @impl [SPEC-ARCH-10] docs/DESIGN_SPECIFICATION.md (timed waits: the driver posts the handle)
    struct SleepAwaiter {
        AsyncTimerDriver&       driver;
        uint32_t                duration_us;
        TimerResult             result{};
        uint64_t                deadline_us{0};
        std::coroutine_handle<> handle{};
        SleepAwaiter*           prev{nullptr};  // shared: list links, under InterruptGuard
        SleepAwaiter*           next{nullptr};
        std::atomic<bool>       linked{false};  // shared: list owner and the destructor; atomic

        SleepAwaiter(AsyncTimerDriver& d, uint32_t us) noexcept : driver(d), duration_us(us) {}
        SleepAwaiter(const SleepAwaiter&) = delete;
        SleepAwaiter& operator=(const SleepAwaiter&) = delete;
        ~SleepAwaiter() noexcept {  // destroyed task leaves the list
            if (linked.load(std::memory_order_acquire)) driver.cancel_sleep(*this);
        }

        bool await_ready() const noexcept { return duration_us == 0; }

        bool await_suspend(std::coroutine_handle<> h) noexcept {
            deadline_us = driver.get_time_us() + duration_us;
            handle = h;
            // @impl [SPEC-ARCH-10] docs/DESIGN_SPECIFICATION.md (a refused sleep is an Error, never Ok)
            if (!driver.add_sleep(*this)) {
                result.status = TimerStatus::Error;
                return false;
            }
            return true;
        }

        TimerResult await_resume() const noexcept { return result; }
    };

    SleepAwaiter sleep_us_async(uint32_t us) noexcept { return SleepAwaiter{*this, us}; }
    SleepAwaiter sleep_ms_async(uint32_t ms) noexcept { return SleepAwaiter{*this, ms * 1000u}; }

    // Post every sleep whose deadline has passed (result written first), re-arm the alarm for the
    // next deadline and wake the host. Called by the target's alarm (Armed) or service_due_us().
    // Returns the next deadline, or kNever.
    uint64_t expire_sleeps(uint64_t now_us, bool from_isr) noexcept {
        uint64_t next = kNever;
        {
            abstractx::InterruptGuard guard;
            while (head_ != nullptr && head_->deadline_us <= now_us) {
                SleepAwaiter* n = head_;
                unlink(*n);
                n->result.status = TimerStatus::Ok;
                n->result.timestamp_us = now_us;
                // [SPEC-ARCH-16] the executor posts and wakes the host; a drop is counted
                if (from_isr) (void)abstractx::post_from_isr(n->handle);
                else (void)abstractx::post(n->handle);
            }
            if (head_ != nullptr) {
                next = head_->deadline_us;
                mode_.store(arm_alarm(next), std::memory_order_relaxed);
            } else {
                disarm_alarm();
            }
        }
        return next;
    }

    // @impl [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md
    // step() calls this once per pass: a Polled driver expires its due sleeps here and reports the
    // next deadline; an Armed driver wakes the loop itself (kNever).
    virtual uint64_t service_due_us() noexcept {
        if (mode_.load(std::memory_order_relaxed) != Alarm::Polled) return kNever;
        return expire_sleeps(get_time_us(), false);
    }

    size_t pending_sleeps() const noexcept {
        abstractx::InterruptGuard guard;
        size_t n = 0;
        for (const SleepAwaiter* p = head_; p != nullptr; p = p->next) ++n;
        return n;
    }

protected:
    // Program the one alarm for deadline_us (replacing any earlier one). Called with interrupts
    // masked (InterruptGuard held); a deadline in the past must fire as soon as possible.
    virtual Alarm arm_alarm(uint64_t deadline_us) noexcept = 0;
    virtual void disarm_alarm() noexcept {}

private:
    // Insert in deadline order (FIFO among equal deadlines); arm when it becomes the earliest.
    bool add_sleep(SleepAwaiter& n) noexcept {
        abstractx::InterruptGuard guard;
        SleepAwaiter* after = nullptr;
        for (SleepAwaiter* p = head_; p != nullptr && p->deadline_us <= n.deadline_us; p = p->next) after = p;
        n.prev = after;
        n.next = after ? after->next : head_;
        if (n.next) n.next->prev = &n;
        if (after) after->next = &n; else head_ = &n;
        n.linked.store(true, std::memory_order_release);
        if (head_ == &n) {
            const Alarm a = arm_alarm(n.deadline_us);
            if (a == Alarm::Unsupported) {
                unlink(n);
                return false;
            }
            mode_.store(a, std::memory_order_relaxed);
        }
        return true;
    }

    void cancel_sleep(SleepAwaiter& n) noexcept {
        abstractx::InterruptGuard guard;
        if (n.linked.load(std::memory_order_relaxed)) unlink(n);  // the alarm may still fire; nothing due then
    }

    void unlink(SleepAwaiter& n) noexcept {
        if (n.prev) n.prev->next = n.next; else head_ = n.next;
        if (n.next) n.next->prev = n.prev;
        n.prev = n.next = nullptr;
        n.linked.store(false, std::memory_order_release);
    }

    SleepAwaiter* head_{nullptr};    // shared: coroutine context and alarm ISR / thread; InterruptGuard
    std::atomic<Alarm> mode_{Alarm::Armed};  // shared: written under InterruptGuard, read by step(); atomic
};

using ITimer = AsyncTimerDriver;

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_TIMER_HPP
