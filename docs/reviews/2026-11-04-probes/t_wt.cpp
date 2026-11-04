#include "abstractx/abstractx.hpp"
#include "abstractx/hal/timer.hpp"
#include "abstractx/hal/platform.hpp"
#include <cstdio>
using namespace abstractx; using namespace abstractx::coro;
static hal::ITimer& T() { return hal::get_timer_driver(); }
Task<int> quick() { co_await step_async(); co_await step_async(); co_return 7; }
static int g_task_won, g_timeout_won, g_err; static bool g_done;
Task<void> loop() {
    for (int i = 0; i < 40; ++i) {
        auto r = co_await with_timeout(quick(), T(), 100000u);
        if (r.index() == 0) ++g_task_won; else { ++g_timeout_won; if (std::get<1>(r).status == hal::TimerStatus::Error) ++g_err; }
    }
    g_done = true;
}
int main() {
    init(); spawn(loop());
    const uint64_t t0 = T().get_time_us();
    while (!g_done && T().get_time_us() - t0 < 2000000) { step(); if (!Dispatcher::has_pending_resumes()) hal::platform_idle_wait(); }
    std::printf("40x with_timeout(quick task that finishes in 2 steps, 100 ms): task won %d, timeout won %d (Error %d), elapsed %llu us\n",
                g_task_won, g_timeout_won, g_err, (unsigned long long)(T().get_time_us() - t0));
}
