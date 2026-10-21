/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: wake primitives never resume inline [SPEC-ARCH-10]
 * A producer that pushes into an AsyncQueue (or sets an Event, releases a Semaphore)
 * must return before the waiting coroutine runs; the waiter runs on the next
 * Dispatcher::process(). Steady state over 1000 rounds: pool high water and
 * dropped resumes do not grow.
 */
// @verifies [SPEC-ARCH-10] docs/DESIGN_SPECIFICATION.md
#undef NDEBUG  // the checks below must run in Release builds too

#include "asp_coro.hpp"
#include "abstractx/domain_dispatcher.hpp"

#include <cassert>
#include <iostream>

using namespace abstractx::coro;
using abstractx::Dispatcher;

namespace {

AsyncQueue<int, 8> g_queue;
Event g_event;
Semaphore g_sem;
int g_received = 0;
int g_last = -1;
bool g_in_producer = false;
bool g_ran_inside_producer = false;

Task<void> consumer(int rounds) {
    for (int i = 0; i < rounds; ++i) {
        int v = co_await g_queue.pop();
        if (g_in_producer) g_ran_inside_producer = true;
        g_last = v;
        ++g_received;
    }
}

Task<void> event_waiter(bool& woke) {
    co_await g_event;
    if (g_in_producer) g_ran_inside_producer = true;
    woke = true;
}

Task<void> sem_waiter(int& got) {
    co_await g_sem.acquire();
    if (g_in_producer) g_ran_inside_producer = true;
    ++got;
}

void pump() {
    while (Dispatcher::has_pending_resumes()) {
        Dispatcher::process();
    }
}

} // namespace

int main() {
    // 1. AsyncQueue: the consumer is suspended on pop(); try_push must not run it.
    constexpr int kRounds = 1000;
    auto task = consumer(kRounds);
    task.resume();  // start once; it suspends on the empty queue
    pump();
    const size_t hw0 = coro_pool_high_water();
    const uint32_t dropped0 = Dispatcher::dropped_resumes();
    for (int i = 0; i < kRounds; ++i) {
        g_in_producer = true;
        assert(g_queue.try_push(i));
        g_in_producer = false;
        assert(g_received == i && "consumer ran inside try_push");
        pump();
        assert(g_received == i + 1 && g_last == i);
    }
    assert(task.done());
    assert(!g_ran_inside_producer);
    assert(coro_pool_high_water() == hw0);
    assert(Dispatcher::dropped_resumes() == dropped0);

    // 2. Event::set and Semaphore::release defer the same way.
    bool woke = false;
    auto ev = event_waiter(woke);
    ev.resume();
    g_in_producer = true;
    g_event.set();
    g_in_producer = false;
    assert(!woke && "Event::set resumed inline");
    pump();
    assert(woke && ev.done());

    int got = 0;
    auto sw = sem_waiter(got);
    sw.resume();
    g_in_producer = true;
    g_sem.release();
    g_in_producer = false;
    assert(got == 0 && "Semaphore::release resumed inline");
    pump();
    assert(got == 1 && sw.done() && g_sem.count() == 0);
    assert(!g_ran_inside_producer);

    std::cout << "[PASS] wake primitives defer resume to the Dispatcher (" << kRounds << " rounds)\n";
    return 0;
}
