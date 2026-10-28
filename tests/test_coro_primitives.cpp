// @verifies [SPEC-ARCH-10] [SPEC-ARCH-11] [SPEC-HAL-14] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX SITL: Coroutine Primitives & RAII Cancellation Safety Test
 * -------------------------------------------------------------------
 * Verifies Dispatcher-driven coroutine primitives, intrusive awaiter RAII unlinking,
 * BusLock cancellation unlinking, and steady-state coroutine pools.
 */

#include "CppUTest/TestHarness.h"
#include "asp_coro.hpp"
#include "abstractx/hal/bus_lock.hpp"
#include "abstractx/hal/timer.hpp"
#include <atomic>

using namespace abstractx;
using namespace abstractx::coro;
using namespace abstractx::hal;

TEST_GROUP(CoroPrimitivesTest) {
    void setup() override {
        while (Dispatcher::has_pending_resumes()) {
            Dispatcher::process();
        }
    }

    void teardown() override {
        while (Dispatcher::has_pending_resumes()) {
            Dispatcher::process();
        }
    }
};

// 1. Test Event with multiple awaiters and RAII destruction unlinking
TEST(CoroPrimitivesTest, EventMultipleWaitersAndUnlink) {
    Event evt;
    bool waiter1_resumed = false;
    bool waiter2_resumed = false;

    auto make_task = [](Event& e, bool& flag) -> Task<void> {
        co_await e;
        flag = true;
    };

    auto t1 = make_task(evt, waiter1_resumed);
    auto t2 = make_task(evt, waiter2_resumed);

    t1.resume();
    t2.resume();

    CHECK_FALSE(waiter1_resumed);
    CHECK_FALSE(waiter2_resumed);

    // Trigger event
    evt.set();

    // Pump Dispatcher
    while (Dispatcher::has_pending_resumes()) {
        Dispatcher::process();
    }

    CHECK_TRUE(waiter1_resumed);
    CHECK_TRUE(waiter2_resumed);
    CHECK_TRUE(t1.done());
    CHECK_TRUE(t2.done());
}

// 2. Test Semaphore with FIFO wakeups
TEST(CoroPrimitivesTest, SemaphoreFifoOrdering) {
    Semaphore sem(1); // 1 token
    int order = 0;
    int t1_order = 0;
    int t2_order = 0;

    auto make_task = [](Semaphore& s, int& order_var, int& my_order) -> Task<void> {
        co_await s;
        my_order = ++order_var;
        // Hold token briefly then release
        s.release();
    };

    auto t1 = make_task(sem, order, t1_order);
    auto t2 = make_task(sem, order, t2_order);

    t1.resume();
    t2.resume();

    while (Dispatcher::has_pending_resumes()) {
        Dispatcher::process();
    }

    CHECK_EQUAL(1, t1_order);
    CHECK_EQUAL(2, t2_order);
}

// 3. Test BusLock remove & RAII BusLockAwaiter cancellation
TEST(CoroPrimitivesTest, BusLockCancellationUnlink) {
    BusLock lock;

    // Acquire bus directly
    uint32_t tok = lock.try_acquire();
    CHECK_TRUE(tok != 0);
    CHECK_TRUE(lock.held());

    // Create awaiter node
    BusLock::Waiter waiter;
    waiter.handle = {};
    waiter.granted = 0;
    lock.wait(&waiter);

    CHECK_TRUE(lock.has_waiters());

    // Remove waiter
    CHECK_TRUE(lock.remove(&waiter));
    CHECK_FALSE(lock.has_waiters());

    // Removing a second time returns false cleanly
    CHECK_FALSE(lock.remove(&waiter));

    // Release lock
    lock.release(tok);
    CHECK_FALSE(lock.held());
}

// 4. Test YieldAwaiter pushes to Dispatcher and does not resume inline
TEST(CoroPrimitivesTest, YieldAwaiterReschedulesViaDispatcher) {
    bool resumed = false;

    auto yield_task = [](bool& flag) -> Task<void> {
        co_await YieldAwaiter{};
        flag = true;
    };

    auto t = yield_task(resumed);
    t.resume();

    // Must NOT have resumed inline
    CHECK_FALSE(resumed);
    CHECK_TRUE(Dispatcher::has_pending_resumes());

    // Process Dispatcher
    Dispatcher::process();
    CHECK_TRUE(resumed);
    CHECK_TRUE(t.done());
}

// 5. Steady state: 1000 coroutines run through Dispatcher without dropped resumes
TEST(CoroPrimitivesTest, SteadyStateDispatcherResumes) {
    const uint32_t dropped_before = Dispatcher::dropped_resumes();

    for (int i = 0; i < 1000; ++i) {
        bool step_ran = false;
        auto simple_task = [](bool& ran) -> Task<void> {
            co_await YieldAwaiter{};
            ran = true;
        };

        auto t = simple_task(step_ran);
        t.resume();
        CHECK_FALSE(step_ran);
        Dispatcher::process();
        CHECK_TRUE(step_ran);
    }

    CHECK_EQUAL(dropped_before, Dispatcher::dropped_resumes());
}

// 6. Semaphore hand-off: a waiter woken by release() owns that permit and must not also
//    take the next one (2026-10-28: two releases woke the waiter only once).
TEST(CoroPrimitivesTest, SemaphoreHandoffKeepsEveryPermit) {
    auto take_two = [](Semaphore& s, int& got) -> Task<void> {
        co_await s.acquire();
        ++got;
        co_await s.acquire();
        ++got;
    };

    for (int i = 0; i < 1000; ++i) {
        Semaphore sem(0);
        int got = 0;
        auto t = take_two(sem, got);
        t.resume();          // waits on the first acquire
        sem.release();       // handed to the waiter
        sem.release();       // no waiter queued yet: count = 1
        while (Dispatcher::has_pending_resumes()) {
            Dispatcher::process();
        }
        CHECK_EQUAL(2, got);
        CHECK_TRUE(t.done());
        CHECK_EQUAL(0U, sem.count());
    }
}

// 7. detach() on a task that never started frees it now: nothing would ever resume it.
TEST(CoroPrimitivesTest, DetachNeverStartedTaskFreesFrame) {
    auto never_run = []() -> Task<int> { co_return 1; };
    const size_t used0 = coro_pool_used();
    for (int i = 0; i < 1000; ++i) {
        auto t = never_run();
        CHECK_TRUE(t.handle() != nullptr);
        t.detach();
        CHECK_EQUAL(used0, coro_pool_used());
    }
}

// [SPEC-ARCH-10] A sleep the timer driver cannot queue completes at once with Error, not Ok.
// (2026-10-28: SleepAwaiter returned false from await_suspend and left the default Ok.)
namespace {
class StuckTimer : public AsyncTimerDriver {
public:
    uint64_t get_time_us() const override { return 0; }
    uint32_t get_time_ms() const override { return 0; }
    void delay_us(uint32_t) override {}
    void delay_ms(uint32_t) override {}

protected:
    void start_hardware_transfer_from_isr(const TimerRequest&) noexcept override {}  // never fires
};
} // namespace

TEST(CoroPrimitivesTest, TimerSubmitFailureIsAnError) {
    StuckTimer timer;
    size_t queued = 0;
    while (timer.submit_request(TimerRequest{})) {
        ++queued;
        CHECK_TRUE(queued <= 64);
    }
    TimerStatus st = TimerStatus::Ok;
    auto sleeper = [](AsyncTimerDriver& t, TimerStatus& out) -> Task<void> {
        TimerResult r = co_await t.sleep_ms_async(5);
        out = r.status;
    }(timer, st);
    sleeper.resume();
    CHECK_TRUE(sleeper.done());                 // refused: resumed at once, never left waiting
    CHECK_TRUE(st == TimerStatus::Error);
}
