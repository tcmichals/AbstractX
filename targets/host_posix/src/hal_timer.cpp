/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Host POSIX HAL Timer Implementation
 */

#include "abstractx/hal/timer.hpp"
#include <chrono>
#include <thread>

namespace abstractx::hal {

class HostTimer : public ITimer {
public:
    void delay_us(uint32_t us) override {
        std::this_thread::sleep_for(std::chrono::microseconds(us));
    }

    void delay_ms(uint32_t ms) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    uint64_t get_time_us() const override {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    }

    uint32_t get_time_ms() const override {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

protected:
    void start_hardware_transfer_from_isr(const TimerRequest& req) noexcept override {
        TimerResult result{};
        result.status = TimerStatus::Ok;
        result.timestamp_us = get_time_us() + req.duration_us;
        push_completion_from_isr(req, result);
        set_hardware_idle_from_isr();
    }
};

} // namespace abstractx::hal
