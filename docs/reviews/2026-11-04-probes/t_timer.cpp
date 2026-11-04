// Throwaway review probes (not part of the repo).
#include "abstractx/abstractx.hpp"
#include "abstractx/hal/timer.hpp"
#include "abstractx/hal/platform.hpp"
#include <cstdio>
#include <cstring>

using namespace abstractx;
using namespace abstractx::coro;

static hal::ITimer& T() { return hal::get_timer_driver(); }
static uint64_t now() { return T().get_time_us(); }

static void pump_until(uint64_t deadline) {
    while (now() < deadline) {
        step();
        if (!Dispatcher::has_pending_resumes()) hal::platform_idle_wait();
    }
}

// ---- A: two concurrent 10 ms sleeps
static uint64_t g_t0, g_wake[4];
Task<void> sleeper(int i, uint32_t ms) {
    co_await T().sleep_ms_async(ms);
    g_wake[i] = now() - g_t0;
}

// ---- B: with_timeout winner leaves the timer busy
static bool g_quick_done;
Task<int> quick() { co_await step_async(); co_return 7; }
static uint64_t g_after;
Task<void> racer() {
    auto r = co_await with_timeout(quick(), T(), 100000u);
    (void)r;
    const uint64_t a = now();
    co_await T().sleep_ms_async(1);
    g_after = now() - a;
}

// ---- C: destroyed suspended task, frame reused
static Event g_never;
static int g_ran_past;
static int g_which;
Task<void> victim(bool use_event) {
    if (use_event) {
        co_await g_never;
        g_which = 2;
    } else {
        co_await T().sleep_ms_async(10);
        g_which = 1;
    }
    ++g_ran_past;
}

// ---- E: Event with many waiters vs dispatcher capacity
static Event g_ev;
static int g_ev_woken;
Task<void> ev_waiter() { co_await g_ev; ++g_ev_woken; }

int main(int argc, char** argv) {
    init();
    const char* which = argc > 1 ? argv[1] : "A";
    if (!std::strcmp(which, "A")) {
        g_t0 = now();
        spawn(sleeper(0, 10));
        spawn(sleeper(1, 10));
        spawn(sleeper(2, 10));
        pump_until(g_t0 + 60000);
        std::printf("A: three concurrent sleep_ms_async(10) woke at %llu, %llu, %llu us\n",
                    (unsigned long long)g_wake[0], (unsigned long long)g_wake[1], (unsigned long long)g_wake[2]);
    } else if (!std::strcmp(which, "B")) {
        g_t0 = now();
        spawn(racer());
        pump_until(g_t0 + 250000);
        std::printf("B: after with_timeout(quick, 100 ms) won by quick, sleep_ms_async(1) took %llu us\n",
                    (unsigned long long)g_after);
    } else if (!std::strcmp(which, "C")) {
        {
            Task<void> a = victim(false);
            a.resume();  // starts and suspends on the timer
        }  // a destroyed while suspended
        Task<void> b = victim(true);
        b.resume();  // suspends on g_never (event never set)
        std::printf("C: before: b.done=%d ran_past=%d\n", (int)b.done(), g_ran_past);
        const uint64_t t = now();
        pump_until(t + 30000);
        std::printf("C: after 30 ms (event never set): b.done=%d ran_past=%d which=%d dropped=%u\n",
                    (int)b.done(), g_ran_past, g_which, Dispatcher::dropped_resumes());
    } else if (!std::strcmp(which, "E")) {
        static Task<void> ts[40] = {ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(),
            ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(),
            ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(),
            ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(),
            ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(),
            ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter(), ev_waiter()};
        for (auto& t : ts) t.resume();
        g_ev.set();
        for (int i = 0; i < 10; ++i) step();
        int done = 0;
        for (auto& t : ts) done += t.done();
        std::printf("E: 40 waiters, Event::set(): woken=%d done=%d dropped_resumes=%u\n",
                    g_ev_woken, done, Dispatcher::dropped_resumes());
    }
    return 0;
}
