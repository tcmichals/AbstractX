/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: i2c-dev Driver with Dedicated Worker Thread
 */

#include "linux_i2c_driver.hpp"
#include "abstractx/hal/platform.hpp"
#include "hal/ports.hpp"

namespace abstractx::hal {

// @impl [SPEC-LINUX-HAL-01] targets/linux/SPECIFICATION.md
static LinuxI2cDriver g_linux_i2c{::hal::ports::kI2c1Dev};
II2c& get_i2c_driver() noexcept {
    return g_linux_i2c;
}

// Port list ([SPEC-LINUX-HAL-02]): 0 and 1 are /dev/i2c-1; else null.
// @impl [SPEC-LINUX-HAL-02] targets/linux/SPECIFICATION.md
II2c* get_i2c_port(uint8_t index) noexcept { return (index == 0 || index == 1) ? &g_linux_i2c : nullptr; }

} // namespace abstractx::hal
