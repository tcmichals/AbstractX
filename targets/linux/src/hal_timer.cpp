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

namespace abstractx::hal {

class LinuxTimerDriver : public ITimer {
public:
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
        std::thread([this, req]() {
            std::this_thread::sleep_for(std::chrono::microseconds(req.duration_us));
            TimerResult result{};
            result.status = TimerStatus::Ok;
            result.timestamp_us = get_time_us();
            push_completion_from_isr(req, result);
            set_hardware_idle_from_isr();
        }).detach();
    }
};

static LinuxTimerDriver g_linux_timer;
ITimer& get_timer_driver() {
    return g_linux_timer;
}

} // namespace abstractx::hal
