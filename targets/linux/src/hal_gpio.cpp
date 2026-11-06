/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: libgpiod v2 GPIO & Edge Interrupt Driver ([SPEC-LINUX-GPIO-02])
 */

#include "linux_gpio_driver.hpp"

namespace abstractx::hal {

// @impl [SPEC-LINUX-GPIO-02] targets/linux/SPECIFICATION.md
static LinuxGpioDriver g_linux_gpio;
IGpio& get_gpio_driver() noexcept {
    return g_linux_gpio;
}

} // namespace abstractx::hal
