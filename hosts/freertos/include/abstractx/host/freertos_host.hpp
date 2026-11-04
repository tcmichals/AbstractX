/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * FreeRTOS host for AbstractX: one task calls step() and waits until the deadline it
 * returns or a wake. Shared by every FreeRTOS target (E906, ESP32-P4).
 * @impl [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md (FreeRTOS host, one copy)
 *
 * Build option ABX_FREERTOS_TIMER_MODE: 0 WAIT (default), 1 XTIMER, 2 TICK_HOOK.
 */
#ifndef ABSTRACTX_HOST_FREERTOS_HOST_HPP
#define ABSTRACTX_HOST_FREERTOS_HOST_HPP

#include <cstdint>

namespace abstractx::freertos_host {

// Call from the one task that runs AbstractX; registers the wake hooks for it. Never returns.
[[noreturn]] void run() noexcept;

// TICK_HOOK mode only: the application's vApplicationTickHook calls this. It compares the tick
// count with the stored deadline and wakes the task; it walks no timer list.
void tick_from_isr() noexcept;

// XTIMER mode: timer commands that could not be queued (must stay 0).
uint32_t timer_cmd_failures() noexcept;

} // namespace abstractx::freertos_host

#endif // ABSTRACTX_HOST_FREERTOS_HOST_HPP
