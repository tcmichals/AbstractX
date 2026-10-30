/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux I/O processor back end: device selection and the SITL edge hook.
 * @impl [SPEC-LINUX-GPIO-01] targets/linux/SPECIFICATION.md
 */
#pragma once

#include <cstdint>

namespace abstractx::target {

// GPIO chip for GpioEdge channels; call before configure(). The string must outlive the backend.
void set_linux_gpiochip(const char* path) noexcept;

// True when the trigger of `pin` runs without hardware (no libgpiod, or no such chip).
bool linux_trigger_simulated(uint32_t pin) noexcept;

// SITL: deliver one edge on a simulated trigger. false: no simulated trigger on `pin`.
bool linux_simulate_edge(uint32_t pin) noexcept;

} // namespace abstractx::target
