/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: GPIO & Edge Interrupt Interface
 * -----------------------------------------------
 * Pin configuration, toggling, and asynchronous edge event awaiters.
 */

#ifndef ABSTRACTX_HAL_GPIO_HPP
#define ABSTRACTX_HAL_GPIO_HPP

#include <cstdint>
#include <coroutine>

namespace abstractx::hal {

enum class PinMode : uint8_t {
    Input = 0,
    Output = 1,
    Alternate = 2,
    Analog = 3
};

enum class PinPull : uint8_t {
    None = 0,
    PullUp = 1,
    PullDown = 2
};

enum class EdgeTrigger : uint8_t {
    None = 0,
    Rising = 1,
    Falling = 2,
    Both = 3
};

using GpioInterruptHandler = void (*)(uint32_t pin, void* context);

class IGpio {
public:
    virtual ~IGpio() = default;

    virtual void configure_pin(uint32_t pin, PinMode mode, PinPull pull = PinPull::None) = 0;
    virtual void write_pin(uint32_t pin, bool level) = 0;
    virtual void toggle_pin(uint32_t pin) = 0;
    virtual bool read_pin(uint32_t pin) = 0;
    virtual bool configure_interrupt(uint32_t pin, EdgeTrigger trigger, GpioInterruptHandler handler, void* context) = 0;
    virtual void enable_interrupt(uint32_t pin, bool enable) = 0;
};

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_GPIO_HPP
