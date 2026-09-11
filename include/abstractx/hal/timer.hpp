/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: Timer & Delay Interface
 * --------------------------------------
 * Cross-platform hardware timer interface with zero-allocation C++20 coroutine awaiters.
 */

#ifndef ABSTRACTX_HAL_TIMER_HPP
#define ABSTRACTX_HAL_TIMER_HPP

#include <cstdint>
#include <coroutine>

namespace abstractx::hal {

class ITimer {
public:
    virtual ~ITimer() = default;

    // Synchronous busy-wait delays
    virtual void delay_us(uint32_t us) = 0;
    virtual void delay_ms(uint32_t ms) = 0;

    // High-resolution monotonic timestamp (microseconds since boot)
    virtual uint64_t get_time_us() const = 0;

    // Millisecond monotonic tick count
    virtual uint32_t get_time_ms() const = 0;
};

/*
 * Coroutine sleep awaiter for non-blocking yields.
 * Yields coroutine execution until hardware or software timer expires.
 */
struct SleepAwaiter {
    uint32_t duration_ms;
    explicit SleepAwaiter(uint32_t ms) noexcept : duration_ms(ms) {}

    bool await_ready() const noexcept { return duration_ms == 0; }
    void await_suspend(std::coroutine_handle<> handle) noexcept;
    void await_resume() noexcept {}
};

inline SleepAwaiter sleep_ms(uint32_t ms) noexcept {
    return SleepAwaiter(ms);
}

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_TIMER_HPP
