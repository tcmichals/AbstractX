/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Point the Linux UART driver (get_uart_driver()) at a device: a tty, or any file a test needs.
 * Without it the driver writes to stdout (SITL).
 */
#ifndef ABSTRACTX_LINUX_UART_HPP
#define ABSTRACTX_LINUX_UART_HPP

#include <cstdint>

namespace abstractx::hal {
// @impl [SPEC-HAL-10] docs/DESIGN_SPECIFICATION.md (Linux UART device selection)
bool linux_uart_open(const char* device_path, uint32_t baudrate = 115200) noexcept;
void linux_uart_close() noexcept;
}

#endif // ABSTRACTX_LINUX_UART_HPP
