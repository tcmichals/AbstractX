/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/driver_wait.hpp"
#include "hal/timer.hpp"
#include "abstractx/hal/platform.hpp"

// @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md (one wait hook for all drivers)
namespace hal {

namespace {

void default_wait(uint64_t deadline_ticks) noexcept {
    Timer::arm_wake(deadline_ticks);
    abstractx::hal::platform_idle_wait();
}

DriverWaitHook s_hook = default_wait;

} // namespace

void set_driver_wait_hook(DriverWaitHook hook) noexcept { s_hook = hook ? hook : default_wait; }

void driver_wait(uint64_t deadline_ticks) noexcept { s_hook(deadline_ticks); }

} // namespace hal
