/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: i2c-dev Driver with Dedicated Worker Thread
 */

#include "linux_i2c_driver.hpp"

namespace abstractx::hal {

// @impl [SPEC-LINUX-HAL-01] targets/linux/SPECIFICATION.md
static LinuxI2cDriver g_linux_i2c{"/dev/i2c-1"};
II2c& get_i2c_driver() noexcept {
    return g_linux_i2c;
}

} // namespace abstractx::hal
