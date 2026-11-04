/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The one way a coroutine becomes ready ([SPEC-ARCH-16] requirement 5). Every driver, ISR, timer and
 * primitive calls post() (task, thread or coroutine context) or post_from_isr() (interrupt context).
 * Where the handle goes is the host's choice (set_executor); the default is the AbstractX ring (the
 * Dispatcher) plus the host's wake hook. A false return means the handle did not fit; it is counted
 * (Dispatcher::dropped_resumes() for the default executor) and never silent.
 */
#ifndef ABSTRACTX_EXECUTOR_HPP
#define ABSTRACTX_EXECUTOR_HPP

#include <coroutine>

#include "abstractx/domain_dispatcher.hpp"
#include "abstractx/wake.hpp"

namespace abstractx {

using PostFn = bool (*)(std::coroutine_handle<>) noexcept;
struct Executor {
    PostFn post{nullptr};
    PostFn post_from_isr{nullptr};
};

// @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (executor; {} restores the default)
void set_executor(const Executor& e) noexcept;
bool post(std::coroutine_handle<> h) noexcept;
bool post_from_isr(std::coroutine_handle<> h) noexcept;

// The default executor's two functions (ring + wake), so a host can reuse them.
bool ring_post(std::coroutine_handle<> h) noexcept;
bool ring_post_from_isr(std::coroutine_handle<> h) noexcept;

namespace hal {
// 1 in interrupt context, 0 not, -1 unknown (target does not say). Debug builds check post() /
// post_from_isr() against it. Core default (weak): -1.
int in_isr() noexcept;
}

} // namespace abstractx

#endif // ABSTRACTX_EXECUTOR_HPP
