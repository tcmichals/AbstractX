/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX ESP32-P4 HAL Timer Implementation
 */

#include "abstractx/hal/timer.hpp"

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "rom/ets_sys.h"
#endif

namespace abstractx::hal {

// @impl [SPEC-ESP32P4-HAL-00] targets/esp32p4/SPECIFICATION.md (no alarm driver yet: sleeps fail)
class Esp32p4Timer : public ITimer {
public:
    void delay_us(uint32_t us) override {
#ifdef ESP_PLATFORM
        ets_delay_us(us);
#else
        (void)us;
#endif
    }

    void delay_ms(uint32_t ms) override {
#ifdef ESP_PLATFORM
        ets_delay_us(ms * 1000);
#else
        (void)ms;
#endif
    }

    uint64_t get_time_us() const override {
#ifdef ESP_PLATFORM
        return esp_timer_get_time();
#else
        return 0;
#endif
    }

    uint32_t get_time_ms() const override {
#ifdef ESP_PLATFORM
        return static_cast<uint32_t>(esp_timer_get_time() / 1000);
#else
        return 0;
#endif
    }

protected:
    void start_hardware_transfer_from_isr(const TimerRequest& req) noexcept override {
        TimerResult result{};
        result.status = TimerStatus::Error;
        push_completion_from_isr(req, result);
        set_hardware_idle_from_isr();
    }
};

} // namespace abstractx::hal
