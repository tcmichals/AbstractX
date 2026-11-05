// @verifies [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The host drives AbstractX ([SPEC-ARCH-15]):
 * 1. next_period(): no drift, late slots skipped and counted, never a burst
 * 2. step() returns kNow while a coroutine is ready, kNever when idle, the status deadline when on
 * 3. one wake route: wake() / wake_from_isr() reach the hooks the host registered
 * 5. longest_ready_run(): a coroutine that never waits shows up as a number
 * 4. steady state: a producer thread publishes 1000 values, posts and wakes; a host loop that
 *    blocks only on (deadline, wake) like freertos_host::run() receives all of them, with no
 *    dropped resumes and no coroutine pool growth
 */

#include "abstractx/abstractx.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace abstractx;
using namespace abstractx::coro;

namespace {

// --- 1. periodic schedule --------------------------------------------------------------
static_assert([] { uint32_t o = 0; return next_period(0, 10, 5, o) == 10 && o == 0; }());

void test_next_period() {
    uint32_t over = 0;
    assert(next_period(0, 10, 10, over) == 10 && over == 0);  // due exactly now: not late
    assert(next_period(0, 10, 25, over) == 30 && over == 2);  // slots 10 and 20 skipped
    over = 0;
    assert(next_period(0, 10, 20, over) == 20 && over == 1);  // slot 10 skipped, 20 is now
    over = 0;
    uint64_t t = 0;
    for (int i = 0; i < 1000; ++i) t = next_period(t, 1000, t, over);  // never late: no drift
    assert(t == 1'000'000 && over == 0);
    std::puts("[+] next_period: no drift, late slots skipped and counted");
}

// --- 2. step() return value ---------------------------------------------------------------
bool g_yield_done = false;
Task<void> yields_twice() {
    co_await step_async();
    co_await step_async();
    g_yield_done = true;
}

void test_step_return() {
    while (step() == kNow) {}  // drain init work
    assert(step() == kNever);  // idle, status off, host timer wakes itself
    assert(spawn(yields_twice(), "yields"));
    assert(step() == kNow);  // the task is ready again after each pass
    int passes = 1;
    while (step() == kNow) ++passes;
    assert(g_yield_done && passes <= 3);
    assert(step() == kNever);
    std::puts("[+] step(): kNow while ready, kNever when idle");
}

// --- 3. wake routing -----------------------------------------------------------------------
std::atomic<uint32_t> g_task_wakes{0};
std::atomic<uint32_t> g_isr_wakes{0};
void count_task() noexcept { g_task_wakes.fetch_add(1); }
void count_isr() noexcept { g_isr_wakes.fetch_add(1); }

void test_wake_routing() {
    wake();  // before a host registers: no-op
    set_wake({&count_task, &count_isr});
    std::thread a([] { for (int i = 0; i < 1000; ++i) wake(); });
    std::thread b([] { for (int i = 0; i < 1000; ++i) wake_from_isr(); });
    a.join();
    b.join();
    assert(g_task_wakes.load() == 1000 && g_isr_wakes.load() == 1000);
    set_wake({});
    std::puts("[+] wake routing: 1000 task + 1000 isr wakes from other threads reach their hooks");
}

// --- 4. steady state through a host loop ---------------------------------------------------
// A latched wake, like a task notification: a wake before the wait is not lost.
std::mutex g_mu;
std::condition_variable g_cv;
uint32_t g_pending_wakes = 0;  // shared: producer thread and host loop; g_mu
void host_wake() noexcept {
    { std::lock_guard<std::mutex> l(g_mu); ++g_pending_wakes; }
    g_cv.notify_one();
}

// One-slot mailbox filled by another thread. Lifetime: the awaiter lives in the consumer frame.
struct Mailbox {
    std::atomic<std::coroutine_handle<>> waiter{nullptr};  // shared: consumer stores, producer takes; atomic
    uint32_t value{0};  // shared: producer writes before post(), consumer reads after resume
    struct Awaiter {
        Mailbox& m;
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept { m.waiter.store(h, std::memory_order_release); }
        uint32_t await_resume() const noexcept { return m.value; }
    };
    Awaiter next() noexcept { return Awaiter{*this}; }
};

constexpr uint32_t kMessages = 1000;
Mailbox g_box;
uint64_t g_sum = 0;
bool g_consumer_done = false;

Task<void> consumer() {
    for (uint32_t i = 0; i < kMessages; ++i) g_sum += co_await g_box.next();
    g_consumer_done = true;
}

void test_steady_state() {
    const uint32_t dropped0 = Dispatcher::dropped_resumes();
    set_wake({&host_wake, &host_wake});
    assert(spawn(consumer(), "consumer"));
    const size_t pool_after_spawn = coro_pool_high_water();

    std::thread producer([] {
        for (uint32_t i = 1; i <= kMessages; ++i) {
            std::coroutine_handle<> h;
            while (!(h = g_box.waiter.exchange(nullptr, std::memory_order_acq_rel))) std::this_thread::yield();
            g_box.value = i;
            assert(IsrDispatcher::post(h));  // publish first,
            wake();                          // then wake the host
        }
    });

    // The host: block until the deadline step() returned or a wake (no polling interval).
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!g_consumer_done) {
        const uint64_t next = step();
        // The step may have finished the work: check before sleeping. Without this the loop waited for
        // a wake that would never come whenever no stale wake was latched (2026-11-05: 3/3 hangs pinned
        // to one core, sum complete; flaky under the loaded gate).
        if (next == kNow || g_consumer_done) continue;
        std::unique_lock<std::mutex> l(g_mu);
        if (next == kNever) {
            g_cv.wait_until(l, give_up, [] { return g_pending_wakes != 0; });
        } else {
            g_cv.wait_for(l, std::chrono::microseconds(next - hal::get_timer_driver().get_time_us()),
                          [] { return g_pending_wakes != 0; });
        }
        g_pending_wakes = 0;
        assert(std::chrono::steady_clock::now() < give_up && "host loop missed a wake");
    }
    producer.join();
    while (step() == kNow) {}
    set_wake({});

    assert(g_sum == uint64_t{kMessages} * (kMessages + 1u) / 2u);
    assert(Dispatcher::dropped_resumes() == dropped0);
    assert(coro_pool_high_water() == pool_after_spawn);
    std::printf("[+] steady state: %u values through post + wake, 0 dropped, pool high water %zu\n",
                kMessages, coro_pool_high_water());
}

// --- 5. a host never spins silently ([SPEC-ARCH-15]) ------------------------------------------
bool g_spin_done = false;
Task<void> spinner() {
    for (int i = 0; i < 500; ++i) co_await step_async();  // never waits for anything
    g_spin_done = true;
}

void test_longest_ready_run() {
    while (step() == kNow) {}
    (void)longest_ready_run(true);
    assert(longest_ready_run() == 0);
    assert(spawn(spinner(), "spinner"));
    while (step() == kNow) {}
    assert(g_spin_done);
    const uint32_t run = longest_ready_run();
    std::printf("[+] longest ready run after a 500-yield coroutine: %u\n", run);
    assert(run >= 499 && "a coroutine that never waits must be visible");  // spawn() ran the first
    assert(step() == kNever && longest_ready_run() == run);  // idle keeps the max
    (void)longest_ready_run(true);
    assert(step() == kNever && longest_ready_run() == 0);    // new window, idle: 0
}

void test_status_deadline() {
    Config cfg{};
    cfg.trace.status_period_ms = 1000;
    assert(init(cfg));
    uint64_t next = step();
    while (next == kNow) next = step();
    const uint64_t now = hal::get_timer_driver().get_time_us();
    assert(next != kNever && next <= now + 1'100'000u);
    std::puts("[+] step(): returns the status report deadline when it is on");
}

} // namespace

int main() {
    Config cfg{};
    cfg.trace.status_period_ms = 0;
    cfg.trace.timeline = false;
    assert(init(cfg));
    test_next_period();
    test_step_return();
    test_wake_routing();
    test_steady_state();
    test_longest_ready_run();
    test_status_deadline();
    std::puts("[SUCCESS] host-driven step [SPEC-ARCH-15]");
    return 0;
}
