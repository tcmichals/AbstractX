// @verifies [SPEC-HAL-05] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Sleeps never wait for each other ([SPEC-HAL-05], review 2026-11-04 T-1 / T-2), on the Linux
 * timer driver, pumped the way production pumps it (step() + platform_idle_wait()):
 * 1. five concurrent 20 ms sleeps all wake at about 20 ms (before: 20, 40, ..., 100 ms)
 * 2. a 50 Hz loop keeps its rate while another task sleeps 1 s (gps_imu_app mag vs monitor)
 * 3. steady state: 3 tasks x 1000 sleeps; nothing left in the list, no drops, no pool growth
 * 4. a destroyed sleeper leaves the list and resumes nothing
 * 5. with_timeout won by the task does not hold up later sleeps; 40 in a row all won by the task
 */
#undef NDEBUG
#include "abstractx/abstractx.hpp"

#include <cassert>
#include <cstdio>

using namespace abstractx;
using namespace abstractx::coro;

namespace {

hal::ITimer& T() { return hal::get_timer_driver(); }
uint64_t now() { return T().get_time_us(); }

template <typename Pred>
bool pump_until(Pred done, uint64_t limit_us) {
    const uint64_t t0 = now();
    while (!done()) {
        if (now() - t0 > limit_us) return false;
        step();
        if (!Dispatcher::has_pending_resumes()) hal::platform_idle_wait();
    }
    return true;
}

// --- 1 ---------------------------------------------------------------------------------------
uint64_t g_t0;
uint64_t g_wake[5];
int g_woken;
Task<void> sleeper(int i, uint32_t ms) {
    co_await T().sleep_ms_async(ms);
    g_wake[i] = now() - g_t0;
    ++g_woken;
}

void test_concurrent_sleeps() {
    g_t0 = now();
    for (int i = 0; i < 5; ++i) assert(spawn(sleeper(i, 20), "sleeper"));
    assert(pump_until([] { return g_woken == 5; }, 2'000'000));
    uint64_t lo = UINT64_MAX, hi = 0;
    for (uint64_t w : g_wake) { lo = w < lo ? w : lo; hi = w > hi ? w : hi; }
    std::printf("[+] five concurrent 20 ms sleeps woke between %llu and %llu us\n",
                (unsigned long long)lo, (unsigned long long)hi);
    assert(lo >= 20'000 && "no sleep wakes early");
    assert(hi < 60'000 && "sleeps must not wait for each other (one at a time: last at 100 ms)");
}

// --- 2 ---------------------------------------------------------------------------------------
int g_fast_ticks;
bool g_slow_done;
Task<void> slow_monitor() {
    co_await T().sleep_ms_async(1000);
    g_slow_done = true;
}
Task<void> fast_loop() {
    for (int i = 0; i < 10; ++i) {
        co_await T().sleep_ms_async(20);
        ++g_fast_ticks;
    }
}

void test_fast_loop_beside_slow_sleep() {
    assert(spawn(slow_monitor(), "monitor"));
    const uint64_t t0 = now();
    assert(spawn(fast_loop(), "fast"));
    assert(pump_until([] { return g_fast_ticks == 10; }, 3'000'000));
    const uint64_t took = now() - t0;
    std::printf("[+] 10 x 20 ms beside a 1 s sleep took %llu us\n", (unsigned long long)took);
    assert(took < 600'000 && "the 50 Hz loop waited behind the 1 s sleep");
    assert(!g_slow_done);
    assert(pump_until([] { return g_slow_done; }, 3'000'000));
}

// --- 3 ---------------------------------------------------------------------------------------
int g_steady_done;
Task<void> steady(uint32_t us) {
    for (int i = 0; i < 1000; ++i) co_await T().sleep_us_async(us);
    ++g_steady_done;
}

void test_steady_state() {
    const uint32_t dropped0 = Dispatcher::dropped_resumes();
    assert(spawn(steady(100), "s1") && spawn(steady(150), "s2") && spawn(steady(200), "s3"));
    const size_t pool0 = coro_pool_high_water();
    assert(pump_until([] { return g_steady_done == 3; }, 30'000'000));
    while (step() == kNow) {}
    assert(T().pending_sleeps() == 0);
    assert(Dispatcher::dropped_resumes() == dropped0);
    assert(coro_pool_high_water() == pool0);
    std::printf("[+] steady state: 3 x 1000 sleeps, 0 pending, 0 dropped, pool high water %zu\n", pool0);
}

// --- 4 ---------------------------------------------------------------------------------------
Event g_never;
int g_ran_past;
Task<void> victim(bool use_event) {
    if (use_event) co_await g_never;
    else co_await T().sleep_ms_async(10);
    ++g_ran_past;
}

void test_destroyed_sleeper() {
    {
        Task<void> a = victim(false);
        a.resume();  // resume: starts the never-run task; it suspends on the timer
        assert(T().pending_sleeps() == 1);
    }  // destroyed while suspended
    assert(T().pending_sleeps() == 0 && "a destroyed sleeper must leave the list");
    Task<void> b = victim(true);
    b.resume();  // resume: starts the never-run task; it waits on an Event nobody sets
    const uint64_t t0 = now();
    pump_until([] { return false; }, 30'000);
    (void)t0;
    assert(!b.done() && g_ran_past == 0 && "the old sleep must not resume anything");
    std::puts("[+] destroyed sleeper left the list; nothing resumed");
}

// --- 5 ---------------------------------------------------------------------------------------
Task<int> quick() {
    co_await step_async();
    co_return 7;
}
int g_task_won, g_timeout_won;
uint64_t g_after;
bool g_race_done;
Task<void> racer() {
    for (int i = 0; i < 40; ++i) {
        auto r = co_await with_timeout(quick(), T(), 100'000u);
        if (r.index() == 0) ++g_task_won;
        else ++g_timeout_won;
    }
    const uint64_t a = now();
    co_await T().sleep_ms_async(1);
    g_after = now() - a;
    g_race_done = true;
}

void test_with_timeout_releases_the_timer() {
    assert(spawn(racer(), "racer"));
    assert(pump_until([] { return g_race_done; }, 3'000'000));
    std::printf("[+] 40 x with_timeout: task won %d, timeout won %d; next 1 ms sleep took %llu us\n",
                g_task_won, g_timeout_won, (unsigned long long)g_after);
    assert(g_task_won == 40 && g_timeout_won == 0);
    assert(g_after < 20'000 && "a lost timeout must not hold up the next sleep");
    pump_until([] { return T().pending_sleeps() == 0; }, 1'000'000);  // the losers' 100 ms sleeps end
    assert(T().pending_sleeps() == 0);
}

} // namespace

int main() {
    Config cfg{};
    cfg.trace.status_period_ms = 0;
    assert(init(cfg));
    test_concurrent_sleeps();
    test_fast_loop_beside_slow_sleep();
    test_steady_state();
    test_destroyed_sleeper();
    test_with_timeout_releases_the_timer();
    std::puts("[SUCCESS] timer concurrency [SPEC-HAL-05]");
    return 0;
}
