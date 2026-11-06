/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Linux HAL: termios2 Non-Blocking Serial Driver with Low Latency
 * @impl [SPEC-HAL-03] docs/DESIGN_SPECIFICATION.md#spec-hal-03
 */

#include "linux_uart_driver.hpp"
#include "linux_uart.hpp"

namespace abstractx::hal {

static LinuxUartDriver g_linux_uart;
IUart& get_uart_driver() noexcept {
    return g_linux_uart;
}

// Port list ([SPEC-LINUX-HAL-02]): 0 is the UART the app opened with linux_uart_open(); else null.
// @impl [SPEC-LINUX-HAL-02] targets/linux/SPECIFICATION.md
IUart* get_uart_port(uint8_t index) noexcept { return index == 0 ? &g_linux_uart : nullptr; }

bool linux_uart_open(const char* device_path, uint32_t baudrate) noexcept {
    return g_linux_uart.open(device_path, baudrate);
}

void linux_uart_close() noexcept { g_linux_uart.close(); }

} // namespace abstractx::hal
