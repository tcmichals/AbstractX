/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hand-clocked protothread helpers for the examples ([SPEC-ARCH-10], 2026-10-28).
 * These awaiters keep no handle: only a loop that resumes the task by hand can continue it,
 * which production code and tests must not do (AGENTS.md section 10.6). Firmware waits with
 * co_await hal::get_timer_driver().sleep_ms_async(ms); these stay for the protothread
 * comparisons, where the example drives its own simulated clock.
 */
// @impl [SPEC-ARCH-10] docs/DESIGN_SPECIFICATION.md (hand-clocked helpers are example code)

#ifndef ABSTRACTX_EXAMPLES_HAND_CLOCKED_HPP
#define ABSTRACTX_EXAMPLES_HAND_CLOCKED_HPP

#include "asp_coro.hpp"

#include <coroutine>
#include <cstdint>

namespace abstractx::coro {

// Delay awaiter for a hand-advanced clock: writes the comparator, keeps no handle.
struct AsyncDelayAwaiter {
    uint64_t resume_at_us_{0};
    uint64_t current_time_us_{0};
    uint64_t* timer_comparator_reg_{nullptr};

    constexpr AsyncDelayAwaiter(uint64_t resume_at_us, uint64_t current_time_us, uint64_t* timer_reg = nullptr) noexcept
        : resume_at_us_(resume_at_us), current_time_us_(current_time_us), timer_comparator_reg_(timer_reg) {}

    bool await_ready() const noexcept { return current_time_us_ >= resume_at_us_; }

    // waker: the example's own loop, which resumes the task by hand when its clock passes resume_at
    void await_suspend(std::coroutine_handle<>) noexcept {
        if (timer_comparator_reg_) {
            *timer_comparator_reg_ = resume_at_us_;
        }
    }

    void await_resume() noexcept {}
};

// Aliases for embedded flexibility
using AsyncSleepAwaiter = AsyncDelayAwaiter;

// Standard Asynchronous Delay Helpers
inline AsyncDelayAwaiter delay_us(uint64_t delta_us, uint64_t current_time_us, uint64_t* timer_reg = nullptr) noexcept {
    return AsyncDelayAwaiter{current_time_us + delta_us, current_time_us, timer_reg};
}

inline AsyncDelayAwaiter delay_ms(uint64_t delta_ms, uint64_t current_time_ms, uint64_t* timer_reg = nullptr) noexcept {
    return AsyncDelayAwaiter{current_time_ms + delta_ms, current_time_ms, timer_reg};
}

inline AsyncDelayAwaiter delay_until(uint64_t target_time, uint64_t current_time, uint64_t* timer_reg = nullptr) noexcept {
    return AsyncDelayAwaiter{target_time, current_time, timer_reg};
}

inline AsyncDelayAwaiter sleep_until(uint64_t target_time_us, uint64_t current_time_us, uint64_t* timer_comparator = nullptr) noexcept {
    return AsyncDelayAwaiter{target_time_us, current_time_us, timer_comparator};
}

inline AsyncDelayAwaiter sleep_for(uint64_t delta_us, uint64_t current_time_us, uint64_t* timer_comparator = nullptr) noexcept {
    return AsyncDelayAwaiter{current_time_us + delta_us, current_time_us, timer_comparator};
}

// Type-Safe Condition Awaiter (replaces PT_WAIT_UNTIL)
template <typename Predicate>
struct WaitUntilAwaiter {
    Predicate pred_;
    bool await_ready() const noexcept { return pred_(); }
    // waker: the example's own loop, which resumes the task by hand after the predicate changes
    void await_suspend(std::coroutine_handle<>) noexcept {}
    void await_resume() noexcept {}
};

template <typename Predicate>
auto wait_until(Predicate pred) noexcept {
    return WaitUntilAwaiter<Predicate>{pred};
}

} // namespace abstractx::coro

namespace abstractx {
using coro::AsyncDelayAwaiter;
using coro::AsyncSleepAwaiter;
using coro::delay_ms;
using coro::delay_until;
using coro::delay_us;
using coro::sleep_for;
using coro::sleep_until;
using coro::wait_until;
} // namespace abstractx

#endif // ABSTRACTX_EXAMPLES_HAND_CLOCKED_HPP
