/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: spidev High-Speed SPI Driver with Dedicated Worker Thread
 */

#include "abstractx/hal/platform.hpp"
#include "linux_spi_driver.hpp"

namespace abstractx::hal {

// @impl [SPEC-LINUX-HAL-01] targets/linux/SPECIFICATION.md
static LinuxSpiDriver g_linux_spi{"/dev/spidev1.0"};
ISpi& get_spi_driver() noexcept {
    return g_linux_spi;
}

} // namespace abstractx::hal
