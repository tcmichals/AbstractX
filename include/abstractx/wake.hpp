/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The one wake route ([SPEC-ARCH-15]): AbstractX never waits; it calls the host's hook so the
 * host (Boost.Asio, a FreeRTOS task, a bare-metal loop) calls step() again.
 */
#ifndef ABSTRACTX_WAKE_HPP
#define ABSTRACTX_WAKE_HPP

namespace abstractx {

using WakeFn = void (*)() noexcept;
struct WakeHooks {
    WakeFn task{nullptr};  // from a task or thread
    WakeFn isr{nullptr};   // from interrupt context
};

// @impl [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md (one wake route)
void set_wake(const WakeHooks& hooks) noexcept;
void wake() noexcept;
void wake_from_isr() noexcept;

} // namespace abstractx

#endif // ABSTRACTX_WAKE_HPP
