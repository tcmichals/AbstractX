/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The one wake route ([SPEC-ARCH-15]). Its own file so drivers that wake the host (timers)
 * do not pull the whole runtime into programs that only use them.
 */
#include <atomic>
#include "abstractx/wake.hpp"

namespace abstractx {

// @impl [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md (one wake route)
// shared: host writes once before events start (set_wake), ISRs / threads read; atomic
static std::atomic<WakeFn> g_wake_task{nullptr};
static std::atomic<WakeFn> g_wake_isr{nullptr};

void set_wake(const WakeHooks& hooks) noexcept {
    g_wake_task.store(hooks.task, std::memory_order_release);
    g_wake_isr.store(hooks.isr, std::memory_order_release);
}

void wake() noexcept {
    if (WakeFn fn = g_wake_task.load(std::memory_order_acquire)) fn();
}

void wake_from_isr() noexcept {
    if (WakeFn fn = g_wake_isr.load(std::memory_order_acquire)) fn();
}

} // namespace abstractx
