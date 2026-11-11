// @verifies [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The shared FreeRTOS host (hosts/freertos/src/freertos_host.cpp, built here in XTIMER mode)
 * against a fake FreeRTOS API (sim/fakes/freertos/): timer failures never leave the task waiting
 * without a timeout ([SPEC-ARCH-15], review R2 2026-11-11).
 * 1. xTimerChangePeriod fails: the wait is bounded by the deadline, the next pass re-arms
 * 2. xTimerStop fails: the wait is kCmdRetryTicks, the next pass retries the stop
 * 3. xTimerCreateStatic returns NULL: configASSERT fires, no timer call ever gets NULL, WAIT mode
 * 4. steady state: 1000 re-arms, every other one failing: every failure bounded, counts exact,
 *    no coroutine pool growth
 */
#undef NDEBUG
#include "abstractx/abstractx.hpp"
#include "abstractx/host/freertos_host.hpp"

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
}

#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace abstractx;
using namespace abstractx::coro;
namespace fh = abstractx::freertos_host;

// --- fake FreeRTOS ----------------------------------------------------------------------------
int fake_config_assert_failures = 0;

namespace {
struct Fake {
    bool create_returns_null = false;
    int change_fail = 0;  // fail the next N xTimerChangePeriod calls
    int stop_fail = 0;    // fail the next N xTimerStop calls
    bool active = false;  // the one-shot timer is running
    int creates = 0, changes = 0, stops = 0, actives = 0, waits = 0, yields = 0;
    TickType_t last_wait = 0;
    TickType_t last_period = 0;
};
Fake g;
int g_timer_obj;
int g_task_obj;
TimerHandle_t the_timer() { return reinterpret_cast<TimerHandle_t>(&g_timer_obj); }
} // namespace

extern "C" {
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return reinterpret_cast<TaskHandle_t>(&g_task_obj); }
BaseType_t xTaskNotifyGive(TaskHandle_t) { return pdPASS; }
void vTaskNotifyGiveFromISR(TaskHandle_t, BaseType_t*) {}
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t ticks) {
    ++g.waits;
    g.last_wait = ticks;
    return 0;  // timed out / spurious: the host loops, step() decides what is due
}
TickType_t xTaskGetTickCount(void) { return 0; }
TickType_t xTaskGetTickCountFromISR(void) { return 0; }
void fake_task_yield(void) { ++g.yields; }

TimerHandle_t xTimerCreateStatic(const char*, TickType_t, UBaseType_t, void*, TimerCallbackFunction_t,
                                 StaticTimer_t* buffer) {
    assert(buffer != nullptr);
    ++g.creates;
    return g.create_returns_null ? nullptr : the_timer();
}
BaseType_t xTimerChangePeriod(TimerHandle_t t, TickType_t period, TickType_t) {
    assert(t != nullptr && "a NULL timer handle must never be used");
    ++g.changes;
    if (g.change_fail > 0) { --g.change_fail; return pdFAIL; }
    g.active = true;
    g.last_period = period;
    return pdPASS;
}
BaseType_t xTimerStop(TimerHandle_t t, TickType_t) {
    assert(t != nullptr && "a NULL timer handle must never be used");
    ++g.stops;
    if (g.stop_fail > 0) { --g.stop_fail; return pdFAIL; }
    g.active = false;
    return pdPASS;
}
BaseType_t xTimerIsTimerActive(TimerHandle_t t) {
    assert(t != nullptr && "a NULL timer handle must never be used");
    ++g.actives;
    return g.active ? pdTRUE : pdFALSE;
}
} // extern "C"

// --- helpers ----------------------------------------------------------------------------------
namespace {

constexpr TickType_t kRetry = 1;  // kCmdRetryTicks in freertos_host.cpp

Task<void> sleeper(uint32_t ms) {
    co_await hal::get_timer_driver().sleep_ms_async(ms);
}

// Runs run_pass() until it waited once (kNow passes only yield); returns the timeout it used.
TickType_t pass_until_wait() {
    const int w = g.waits;
    for (int i = 0; i < 100000 && g.waits == w; ++i) fh::detail::run_pass();
    assert(g.waits == w + 1);
    return g.last_wait;
}

void reset(bool null_timer) {
    while (step() == kNow) {}
    g = Fake{};
    g.create_returns_null = null_timer;
    fake_config_assert_failures = 0;
    fh::detail::run_start();
}

void wait_idle() {  // let the sleeper finish in real time, then drain
    for (int i = 0; i < 1000 && step() != kNever; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(step() == kNever);
}

// --- 1, 2 --------------------------------------------------------------------------------------
void test_change_and_stop_failure() {
    reset(false);
    assert(g.creates == 1 && fake_config_assert_failures == 0);
    const uint32_t f0 = fh::timer_cmd_failures();
    assert(spawn(sleeper(30), "sleeper"));

    g.change_fail = 1;
    const TickType_t w1 = pass_until_wait();
    assert(g.changes == 1 && fh::timer_cmd_failures() == f0 + 1);
    assert(w1 != portMAX_DELAY && "failed xTimerChangePeriod must not leave an unbounded wait");
    assert(w1 >= 1 && w1 <= 31 && "the bounded wait is the deadline, not longer");

    const TickType_t w2 = pass_until_wait();  // the retry happens on the next pass
    assert(g.changes == 2 && g.active && w2 == portMAX_DELAY);
    assert(g.last_period >= 1 && g.last_period <= 31);

    const TickType_t w3 = pass_until_wait();  // same deadline, timer active: no new command
    assert(g.changes == 2 && w3 == portMAX_DELAY);
    std::puts("[+] failed xTimerChangePeriod: wait bounded by the deadline, re-armed next pass");

    wait_idle();  // the sleeper is done: next pass stops the timer
    g.active = true;
    g.stop_fail = 1;
    const TickType_t w4 = pass_until_wait();
    assert(g.stops == 1 && fh::timer_cmd_failures() == f0 + 2);
    assert(w4 == kRetry && "failed xTimerStop must not leave an unbounded wait");
    const TickType_t w5 = pass_until_wait();
    assert(g.stops == 2 && !g.active && w5 == portMAX_DELAY);
    const TickType_t w6 = pass_until_wait();  // stopped: nothing more to do
    assert(g.stops == 2 && w6 == portMAX_DELAY);
    assert(fh::timer_cmd_failures() == f0 + 2);
    std::puts("[+] failed xTimerStop: wait bounded by kCmdRetryTicks, stop retried next pass");
}

// --- 3 -------------------------------------------------------------------------------------------
void test_create_null() {
    const uint32_t f0 = fh::timer_cmd_failures();
    reset(true);
    assert(g.creates == 1 && fake_config_assert_failures == 1);
    assert(fh::timer_cmd_failures() == f0 + 1);
    assert(spawn(sleeper(30), "sleeper"));
    const TickType_t w1 = pass_until_wait();
    assert(w1 >= 1 && w1 <= 31 && "no timer: WAIT mode, the deadline is the timeout");
    assert(g.changes == 0 && g.stops == 0 && g.actives == 0);
    wait_idle();
    const TickType_t w2 = pass_until_wait();
    assert(w2 == portMAX_DELAY && g.changes == 0 && g.stops == 0 && g.actives == 0);
    assert(fh::timer_cmd_failures() == f0 + 1);
    std::puts("[+] xTimerCreateStatic NULL: configASSERT, NULL handle never used, WAIT mode");
}

// --- 4 -------------------------------------------------------------------------------------------
void test_steady_state() {
    reset(false);
    const uint32_t f0 = fh::timer_cmd_failures();
    assert(spawn(sleeper(5000), "long sleeper"));
    (void)pass_until_wait();  // first arm
    const auto pool0 = coro::coro_pool_high_water();
    int bounded = 0;
    for (int i = 0; i < 1000; ++i) {
        g.active = false;  // the timer fired early (spurious wake): the host must re-arm
        const bool fail = (i % 2) == 0;
        g.change_fail = fail ? 1 : 0;
        const TickType_t w = pass_until_wait();
        if (fail) {
            assert(w != portMAX_DELAY && w <= 5001);
            ++bounded;
        } else {
            assert(w == portMAX_DELAY && g.active);
        }
    }
    assert(bounded == 500 && fh::timer_cmd_failures() == f0 + 500);
    assert(coro::coro_pool_high_water() == pool0);
    assert(Dispatcher::dropped_resumes() == 0);
    std::printf("[+] steady state: 1000 re-arms, 500 failures all bounded, pool high water %zu\n",
                static_cast<size_t>(pool0));
}

} // namespace

int main() {
    Config cfg{};
    cfg.trace.status_period_ms = 0;
    cfg.trace.timeline = false;
    assert(init(cfg));
    test_change_and_stop_failure();
    test_create_null();
    test_steady_state();
    std::puts("[SUCCESS] FreeRTOS host XTIMER failures are bounded [SPEC-ARCH-15]");
    return 0;
}
