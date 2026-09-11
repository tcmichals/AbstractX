/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: Unified Platform Lifecycle & Driver Factory Interface
 * ---------------------------------------------------------------------
 * Encapsulates all silicon-specific and board-specific operations:
 * - Boot & Clocks Initialization (stdio, peripheral clocks, power)
 * - Inter-Domain Launch (Core 1 on dual-core AMP, RT threads on Linux)
 * - Idle Power Management (WFE on ARM, WFI on RISC-V, yield on POSIX)
 * - Background Polling (Wi-Fi / Network servicing)
 * - Universal HAL Driver Factories (SPI, I2C, UART, Timer, GPIO, ioProcessor)
 *
 * Guarantees that applications in apps/ contain ZERO #ifdef silicon checks.
 */

#pragma once

#include "abstractx/hal/spi.hpp"
#include "abstractx/hal/i2c.hpp"
#include "abstractx/hal/uart.hpp"
#include "abstractx/hal/timer.hpp"
#include "abstractx/hal/gpio.hpp"
#include "abstractx/hal/io_processor.hpp"

namespace abstractx::hal {

// Initialize silicon clocks, standard I/O console, and board power domains
void platform_init() noexcept;

// Launch the secondary / worker processing domain (Core 1 on dual-core AMP, worker thread on Linux)
void platform_launch_processing_domain(void (*entry)()) noexcept;

// Backward-compatible alias
inline void platform_launch_flight_domain(void (*entry)()) noexcept {
    platform_launch_processing_domain(entry);
}

// Power-saving sleep while waiting for events (__wfe, __wfi, or yield)
void platform_idle_wait() noexcept;

// Background networking / wireless stack servicing hook
void platform_poll_network() noexcept;

// Universal HAL Driver Accessors
ISpi&         get_spi_driver() noexcept;
II2c&         get_i2c_driver() noexcept;
IUart&        get_uart_driver() noexcept;
ITimer&       get_timer_driver() noexcept;
IGpio&        get_gpio_driver() noexcept;
IIoProcessor& get_target_io_processor() noexcept;

} // namespace abstractx::hal
