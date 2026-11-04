/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (executor: post / post_from_isr, ring by default)
 */
#include <atomic>
#include <cassert>

#include "abstractx/executor.hpp"

namespace abstractx {

__attribute__((weak)) int hal::in_isr() noexcept { return -1; }

bool ring_post(std::coroutine_handle<> h) noexcept {
    const bool ok = Dispatcher::push(h);
    if (ok) wake();
    return ok;
}

bool ring_post_from_isr(std::coroutine_handle<> h) noexcept {
    const bool ok = Dispatcher::post(h);
    if (ok) wake_from_isr();
    return ok;
}

namespace {
// shared: host sets before events start; ISRs / threads read; atomic
std::atomic<PostFn> s_post{&ring_post};
std::atomic<PostFn> s_post_isr{&ring_post_from_isr};
} // namespace

void set_executor(const Executor& e) noexcept {
    s_post.store(e.post ? e.post : &ring_post, std::memory_order_release);
    s_post_isr.store(e.post_from_isr ? e.post_from_isr : &ring_post_from_isr, std::memory_order_release);
}

bool post(std::coroutine_handle<> h) noexcept {
#if !defined(NDEBUG) && !ABSTRACTX_HOSTED
    assert(hal::in_isr() != 1 && "post() from an interrupt: use post_from_isr()");
#endif
    return s_post.load(std::memory_order_acquire)(h);
}

bool post_from_isr(std::coroutine_handle<> h) noexcept {
#if !defined(NDEBUG) && !ABSTRACTX_HOSTED
    assert(hal::in_isr() != 0 && "post_from_isr() outside an interrupt: use post()");
#endif
    return s_post_isr.load(std::memory_order_acquire)(h);
}

} // namespace abstractx
