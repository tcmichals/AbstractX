/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdint>

// @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md (one wait hook for all drivers)
namespace hal {

// Sleep until an interrupt or `deadline_ticks` (mtime); never resumes a coroutine.
using DriverWaitHook = void (*)(uint64_t deadline_ticks) noexcept;

// nullptr restores the default (arm a timer wake, platform_idle_wait()).
void set_driver_wait_hook(DriverWaitHook hook) noexcept;
void driver_wait(uint64_t deadline_ticks) noexcept;

} // namespace hal
