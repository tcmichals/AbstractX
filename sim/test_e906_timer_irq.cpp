// @verifies [SPEC-E906-IRQ-03] targets/allwinner_e906/SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * [SPEC-E906-IRQ-03] the alarm only wakes ([SPEC-ARCH-16] 3). Compiles the real E906 timer.cpp
 * against fake mtime / mtimecmp registers. A sleep that becomes due: Timer::handle_irq() (the
 * line-7 handler) posts nothing, takes the sleep deadline out of the comparator (no level-interrupt
 * storm) and asks for a wake; Timer::service_due() (step() context) posts it and puts the next
 * deadline back. Before: the handler walked the list and posted from interrupt context.
 */
#undef NDEBUG
#include "hal/timer.hpp"
#include "abstractx/domain_dispatcher.hpp"

#include <cassert>
#include <coroutine>
#include <cstdio>

volatile uint32_t g_fake_mtime[2] = {0, 0};
volatile uint32_t g_fake_mtimecmp[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};
extern "C" volatile uint32_t g_irq_sample_only;

namespace hal::irq {
uint32_t trap_pc() noexcept { return 0; }  // the profiler sample is off in this test
}

namespace {
void set_now(uint64_t t) {
    g_fake_mtime[1] = static_cast<uint32_t>(t >> 32);
    g_fake_mtime[0] = static_cast<uint32_t>(t);
}
uint64_t compare() { return (uint64_t{g_fake_mtimecmp[1]} << 32) | g_fake_mtimecmp[0]; }

struct Coro {
    struct promise_type {
        Coro get_return_object() { return {std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
    std::coroutine_handle<promise_type> h;
};
int g_woke = 0;
Coro sleeper(uint64_t ticks) {
    co_await hal::Timer::sleep_ticks_async(ticks);
    ++g_woke;
}
} // namespace

int main() {
    set_now(1000);
    auto a = sleeper(100);   // due at 1100
    auto b = sleeper(5000);  // due at 6000
    assert(compare() == 1100 && "the comparator holds the earliest sleep");

    set_now(1200);           // the first sleep is due: line 7 fires
    hal::Timer::handle_irq();
    std::printf("[+] handler: ready queue %zu, comparator %llu, sample_only %u\n",
                abstractx::Dispatcher::queue_depth(), static_cast<unsigned long long>(compare()),
                g_irq_sample_only);
    assert(abstractx::Dispatcher::queue_depth() == 0 && "the handler posts nothing");
    assert(compare() > 1200 && "the due sleep is out of the comparator: no level-interrupt storm");
    assert(g_irq_sample_only == 0 && "the handler wakes the core so step() runs");

    assert(hal::Timer::service_due());  // step() context
    assert(abstractx::Dispatcher::queue_depth() == 1);
    abstractx::Dispatcher::process();
    assert(g_woke == 1 && compare() == 6000 && "posted from step(); the next deadline is back");

    set_now(7000);
    hal::Timer::handle_irq();
    assert(abstractx::Dispatcher::queue_depth() == 0);
    assert(hal::Timer::service_due());
    abstractx::Dispatcher::process();
    assert(g_woke == 2);
    a.h.destroy();
    b.h.destroy();
    std::puts("[SUCCESS] E906 timer: the alarm only wakes; step() posts [SPEC-E906-IRQ-03]");
    return 0;
}
