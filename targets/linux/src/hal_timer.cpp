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
#include <atomic>
#include <algorithm>

namespace abstractx::hal {

class LinuxTimerDriver : public ITimer {
public:
    LinuxTimerDriver() = default;
    ~LinuxTimerDriver() override {
        shutdown_.store(true, std::memory_order_release);
        while (active_threads_.load(std::memory_order_acquire) > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    void delay_us(uint32_t us) override {
        struct timespec req{};
        req.tv_sec = us / 1'000'000;
        req.tv_nsec = (us % 1'000'000) * 1'000;
        ::nanosleep(&req, nullptr);
    }

    void delay_ms(uint32_t ms) override {
        delay_us(ms * 1'000);
    }

    uint64_t get_time_us() const override {
        struct timespec ts{};
        ::clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000ULL + (ts.tv_nsec / 1'000);
    }

    uint32_t get_time_ms() const override {
        return static_cast<uint32_t>(get_time_us() / 1'000);
    }

protected:
    void start_hardware_transfer_from_isr(const TimerRequest& req) noexcept override {
        active_threads_.fetch_add(1, std::memory_order_acq_rel);
        std::thread([this, req]() {
            const auto target_us = std::chrono::microseconds(req.duration_us);
            const auto start = std::chrono::steady_clock::now();
            while (!shutdown_.load(std::memory_order_acquire)) {
                auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start);
                if (elapsed >= target_us) {
                    break;
                }
                auto remaining = target_us - elapsed;
                auto chunk = std::min(remaining, std::chrono::microseconds(5000));
                std::this_thread::sleep_for(chunk);
            }
            if (!shutdown_.load(std::memory_order_acquire)) {
                TimerResult result{};
                result.status = TimerStatus::Ok;
                result.timestamp_us = get_time_us();
                push_completion_from_isr(req, result);
                set_hardware_idle_from_isr();
            }
            active_threads_.fetch_sub(1, std::memory_order_acq_rel);
        }).detach();
    }

private:
    std::atomic<bool> shutdown_{false};
    std::atomic<int>  active_threads_{0};
};

static LinuxTimerDriver g_linux_timer;
ITimer& get_timer_driver() {
    return g_linux_timer;
}

} // namespace abstractx::hal
