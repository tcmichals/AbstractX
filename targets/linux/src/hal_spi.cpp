/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: spidev High-Speed SPI Driver with Dedicated Worker Thread
 */

#include "abstractx/hal/platform.hpp"
#include "linux_spi_driver.hpp"
#include "hal/ports.hpp"

namespace abstractx::hal {

// @impl [SPEC-LINUX-HAL-01] targets/linux/SPECIFICATION.md
static LinuxSpiDriver g_linux_spi{::hal::ports::kSpi1Cs0Dev};
ISpi& get_spi_driver() noexcept {
    return g_linux_spi;
}

// Port list ([SPEC-LINUX-HAL-02]): 0 and 1 are SPI1 CS0; else null.
// @impl [SPEC-LINUX-HAL-02] targets/linux/SPECIFICATION.md
ISpi* get_spi_port(uint8_t index) noexcept { return (index == 0 || index == 1) ? &g_linux_spi : nullptr; }

} // namespace abstractx::hal
