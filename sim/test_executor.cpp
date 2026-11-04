// @verifies [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * [SPEC-ARCH-16] requirement 5: every ready coroutine goes through post() / post_from_isr(), and the
 * host chooses where they go. A custom executor receives every post (timer, Event, step_async, a
 * driver-style completion); {} restores the AbstractX ring; a refused post is reported.
 */
#undef NDEBUG
#include "abstractx/abstractx.hpp"

#include <cassert>
#include <cstdio>
#include <optional>

using namespace abstractx;
using namespace abstractx::coro;

namespace {

// A host executor: counts, then hands the handle to the ring (so step() still resumes it).
int g_posts = 0, g_isr_posts = 0;
bool count_post(std::coroutine_handle<> h) noexcept { ++g_posts; return ring_post(h); }
bool count_isr(std::coroutine_handle<> h) noexcept { ++g_isr_posts; return ring_post_from_isr(h); }
bool refuse(std::coroutine_handle<>) noexcept { return false; }

Event g_ev;
bool g_done = false;
bool g_at_event = false;
Task<void> body() {
    co_await step_async();                                    // post()
    co_await hal::get_timer_driver().sleep_ms_async(2);       // expire_sleeps -> post()
    g_at_event = true;
    co_await g_ev;                                            // Event::set -> post()
    g_done = true;
}

void pump_until(bool& f) {
    const uint64_t t0 = hal::get_timer_driver().get_time_us();
    while (!f) {
        if (step() != kNow && !f) hal::platform_idle_wait();
        assert(hal::get_timer_driver().get_time_us() - t0 < 2'000'000);
    }
}

} // namespace

int main() {
    Config cfg{};
    cfg.trace.status_period_ms = 0;
    assert(init(cfg));

    set_executor({&count_post, &count_isr});
    assert(spawn(body(), "body"));
    pump_until(g_at_event);
    while (step() == kNow) {}                                 // now waiting on the Event
    const int before = g_posts;
    g_ev.set();
    assert(g_posts == before + 1 && "Event::set goes through the executor");
    pump_until(g_done);
    std::printf("[+] custom executor: %d post(), %d post_from_isr()\n", g_posts, g_isr_posts);
    assert(g_posts >= 3 && "step_async, the timer and Event::set all go through the executor");

    // A completion from "interrupt context" goes through post_from_isr().
    bool isr_done = false;
    auto t = [](bool& d) -> Task<void> { co_await std::suspend_always{}; d = true; }(isr_done);
    t.resume();  // resume: starts the never-run task; it suspends
    assert(post_from_isr(t.handle()) && g_isr_posts == 1);
    while (step() == kNow) {}
    assert(isr_done);

    // A refused post is reported (false), never silent.
    set_executor({&refuse, &refuse});
    auto u = []() -> Task<void> { co_await std::suspend_always{}; }();
    u.resume();  // resume: starts the never-run task
    assert(!post(u.handle()) && !post_from_isr(u.handle()));

    set_executor({});  // default: the AbstractX ring
    assert(post(u.handle()));
    while (step() == kNow) {}

    // [SPEC-ARCH-16] 5a (review T-4): 40 waiters on one Event all wake; with a ring of 32, 8 were lost.
    {
        static Event ev;
        static int woken = 0;
        static std::optional<Task<void>> waiters[40];
        const uint32_t dropped0 = Dispatcher::dropped_resumes();
        for (auto& w : waiters) {
            w.emplace([]() -> Task<void> { co_await ev; ++woken; }());
            w->resume();  // resume: starts the never-run task; it waits on the Event
        }
        ev.set();
        while (step() == kNow) {}
        std::printf("[+] 40 Event waiters: %d woken, %u dropped, ring %zu, high water %u\n", woken,
                    Dispatcher::dropped_resumes() - dropped0, Dispatcher::QUEUE_CAPACITY, Dispatcher::high_water());
        assert(woken == 40 && Dispatcher::dropped_resumes() == dropped0);
    }
    std::puts("[SUCCESS] executor [SPEC-ARCH-16]");
    return 0;
}
