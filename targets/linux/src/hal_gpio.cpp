/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: libgpiod v2 GPIO & Edge Interrupt Driver
 */

#include "abstractx/hal/gpio.hpp"
#include "gpio_gpiod.hpp"
#include <array>
#include <iostream>

namespace abstractx::hal {

class LinuxGpioDriver : public IGpio {
public:
    LinuxGpioDriver() = default;

    void configure_pin(uint32_t pin, PinMode mode, PinPull pull = PinPull::None) override {
        (void)pull;
        if (pin < pin_modes_.size()) {
            pin_modes_[pin] = mode;
        }
    }

    void write_pin(uint32_t pin, bool level) override {
        if (pin < pin_levels_.size()) {
            pin_levels_[pin] = level;
        }
    }

    void toggle_pin(uint32_t pin) override {
        if (pin < pin_levels_.size()) {
            pin_levels_[pin] = !pin_levels_[pin];
        }
    }

    bool read_pin(uint32_t pin) override {
        if (pin < pin_levels_.size()) {
            return pin_levels_[pin];
        }
        return false;
    }

    bool configure_interrupt(uint32_t pin, EdgeTrigger trigger, GpioInterruptHandler handler, void* context) override {
        interrupt_handler_ = handler;
        interrupt_context_ = context;
        interrupt_pin_ = pin;

        bool rising = (trigger == EdgeTrigger::Rising || trigger == EdgeTrigger::Both);
        bool falling = (trigger == EdgeTrigger::Falling || trigger == EdgeTrigger::Both);

        return monitor_.open_pin_interrupt("/dev/gpiochip0", pin, rising, falling);
    }

    void enable_interrupt(uint32_t pin, bool enable) override {
        (void)pin;
        interrupt_enabled_ = enable;
    }

    target::GpiodV2Monitor& monitor() noexcept {
        return monitor_;
    }

    void dispatch_interrupt_events() noexcept {
        if (!interrupt_enabled_ || !interrupt_handler_) return;

        monitor_.process_events([this](unsigned int line, uint64_t ts_ns) {
            (void)ts_ns;
            if (interrupt_handler_) {
                interrupt_handler_(line, interrupt_context_);
            }
        });
    }

private:
    std::array<PinMode, 64> pin_modes_{};
    std::array<bool, 64>    pin_levels_{};
    target::GpiodV2Monitor  monitor_{};
    GpioInterruptHandler    interrupt_handler_{nullptr};
    void*                   interrupt_context_{nullptr};
    uint32_t                interrupt_pin_{0};
    bool                    interrupt_enabled_{false};
};

static LinuxGpioDriver g_linux_gpio;
IGpio& get_gpio_driver() {
    return g_linux_gpio;
}

} // namespace abstractx::hal
