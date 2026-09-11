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
};

} // namespace abstractx::hal
