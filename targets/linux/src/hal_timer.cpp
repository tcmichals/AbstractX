/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: Monotonic High-Resolution Timer & timerfd Implementation
 */

#include "abstractx/hal/timer.hpp"
#include <time.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <chrono>
#include <thread>
// @impl [SPEC-HAL-05] docs/DESIGN_SPECIFICATION.md#spec-hal-05

namespace abstractx::hal {

class LinuxTimerDriver : public ITimer {
public:
    LinuxTimerDriver() = default;

    void delay_us(uint32_t us) override {
        struct timespec req{};
        req.tv_sec = us / 1'000'000;
        req.tv_nsec = static_cast<long>(us % 1'000'000) * 1'000;  // < 1e9
        ::nanosleep(&req, nullptr);  // boot-only ([SPEC-HAL-05])
    }

    void delay_ms(uint32_t ms) override {
        delay_us(ms * 1'000);
    }

    uint64_t get_time_us() const override {
        struct timespec ts{};
        ::clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000ULL +
               static_cast<uint64_t>(ts.tv_nsec) / 1'000ULL;
    }

    uint32_t get_time_ms() const override {
        return static_cast<uint32_t>(get_time_us() / 1'000);
    }

protected:
    // @impl [SPEC-HAL-05] docs/DESIGN_SPECIFICATION.md (Linux: Polled, no thread)
    // @impl [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md (AbstractX never waits)
    // No thread and no OS timer: step() expires due sleeps and returns the next deadline; the host
    // (Boost.Asio steady_timer, or the built-in run() loop) waits for it.
    Alarm arm_alarm(uint64_t) noexcept override { return Alarm::Polled; }
};

static LinuxTimerDriver g_linux_timer;
ITimer& get_timer_driver() {
    return g_linux_timer;
}

} // namespace abstractx::hal
