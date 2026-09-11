/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: Asynchronous Timer & Alarm Interface
 * ---------------------------------------------------
 * Non-blocking timer driver based on the split-queue work model.
 * Coroutines yield to the work queue and are awakened when hardware alarms,
 * OS timer callbacks, or I/O processor queue completions arrive.
 */

#ifndef ABSTRACTX_HAL_TIMER_HPP
#define ABSTRACTX_HAL_TIMER_HPP

#include <cstdint>
#include <coroutine>

#include "abstractx/hal/async_driver.hpp"

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

struct TimerRequest : public AsyncTransactionMeta<TimerResult> {
    uint32_t duration_us{0};
    uint64_t target_time_us{0};
    bool     periodic{false};
};

// @impl [SPEC-HAL-05] docs/DESIGN_SPECIFICATION.md#spec-hal-05
// @status Complete
class AsyncTimerDriver : public AsyncDriverBase<TimerRequest, TimerResult, 16> {
public:
    virtual ~AsyncTimerDriver() = default;

    // High-resolution monotonic timestamp queries
    virtual uint64_t get_time_us() const = 0;
    virtual uint32_t get_time_ms() const = 0;

    // Synchronous busy-wait delays (strictly for boot-time / hardware reset only)
    virtual void delay_us(uint32_t us) = 0;
    virtual void delay_ms(uint32_t ms) = 0;

    /*
     * Coroutine Awaiter: Sleep for specified microseconds asynchronously.
     * Coroutine yields to the work queue and never blocks the CPU thread.
     */
    struct SleepAwaiter {
        AsyncTimerDriver& driver;
        uint32_t          duration_us;
        TimerResult       result{};

        bool await_ready() const noexcept { return duration_us == 0; }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            TimerRequest req{};
            req.duration_us = duration_us;
            req.target_time_us = driver.get_time_us() + duration_us;
            req.coro_handle = handle;
            req.callback = etl::delegate<void(const TimerResult&)>::create<SleepAwaiter, &SleepAwaiter::on_done>(*this);
            return driver.submit_request(req);
        }

        TimerResult await_resume() const noexcept { return result; }

    private:
        void on_done(const TimerResult& res) noexcept {
            result = res;
        }
    };

    SleepAwaiter sleep_us_async(uint32_t us) noexcept {
        return SleepAwaiter{*this, us};
    }

    SleepAwaiter sleep_ms_async(uint32_t ms) noexcept {
        return SleepAwaiter{*this, ms * 1000u};
    }
};

using ITimer = AsyncTimerDriver;

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_TIMER_HPP
