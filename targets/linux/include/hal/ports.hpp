// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// Linux / Zynq port table: a port is a device file ([SPEC-HAL-15]). The UART path is given at run time
// (linux_uart_open), so it has no entry here.
// @impl [SPEC-LINUX-HAL-02] targets/linux/SPECIFICATION.md
#pragma once

namespace hal::ports {

inline constexpr const char* kSpi1Cs0Dev = "/dev/spidev1.0";   // A5E header SPI1, CS0
inline constexpr const char* kI2c1Dev    = "/dev/i2c-1";

} // namespace hal::ports
