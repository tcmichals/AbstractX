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
#include <condition_variable>
#include <mutex>

namespace abstractx::hal {

class LinuxTimerDriver : public ITimer {
public:
    LinuxTimerDriver() = default;
    ~LinuxTimerDriver() override {
        {
            std::lock_guard<std::mutex> l(m_);
            stop_ = true;
        }
        cv_.notify_one();
        if (thread_.joinable()) thread_.join();
    }

    void delay_us(uint32_t us) override {
        struct timespec req{};
        req.tv_sec = us / 1'000'000;
        req.tv_nsec = (us % 1'000'000) * 1'000;
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
    // @impl [SPEC-HAL-05] docs/DESIGN_SPECIFICATION.md (Linux: one timer thread for every sleep)
    // Called with the process-wide InterruptGuard held; the timer thread never takes that lock
    // while holding m_, so the order guard -> m_ cannot deadlock.
    Alarm arm_alarm(uint64_t deadline_us) noexcept override {
        std::call_once(started_, [this] { thread_ = std::thread([this] { run(); }); });
        {
            std::lock_guard<std::mutex> l(m_);
            armed_us_ = deadline_us;
        }
        cv_.notify_one();
        return Alarm::Armed;
    }

    void disarm_alarm() noexcept override {
        std::lock_guard<std::mutex> l(m_);
        armed_us_ = kNever;
    }

private:
    void run() {
        std::unique_lock<std::mutex> l(m_);
        while (!stop_) {
            if (armed_us_ == kNever) {
                cv_.wait(l);
                continue;
            }
            // CLOCK_MONOTONIC is steady_clock on Linux, so the same microseconds.
            const auto due = std::chrono::steady_clock::time_point(std::chrono::microseconds(armed_us_));
            cv_.wait_until(l, due);
            if (stop_ || armed_us_ == kNever || get_time_us() < armed_us_) continue;
            armed_us_ = kNever;  // expire_sleeps re-arms for the next deadline
            l.unlock();
            (void)expire_sleeps(get_time_us(), false);
            l.lock();
        }
    }

    std::mutex              m_;
    std::condition_variable cv_;
    uint64_t                armed_us_{kNever};  // shared: arm/disarm (any thread) and run(); m_
    bool                    stop_{false};       // shared: destructor and run(); m_
    std::once_flag          started_;
    std::thread             thread_;
};

static LinuxTimerDriver g_linux_timer;
ITimer& get_timer_driver() {
    return g_linux_timer;
}

} // namespace abstractx::hal
