// @verifies [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host conformance checks ([SPEC-ARCH-16] requirement 10), written once and run against every host
 * that can run on the PC. A Host type provides:
 *   static bool     run_until(bool (*done)(void*) noexcept, void* ctx, uint64_t give_up_us) noexcept;
 *   static uint32_t waits(bool reset) noexcept;   // times the host blocked
 * The checks:
 *   1. an idle host stays asleep (no polling interval)
 *   2. a post from another thread wakes it within a bound
 *   3. a timer deadline wakes it on time
 *   4. no post or wake is lost (1000 hand-offs from a thread, no explicit wake call)
 *   5. an always-ready coroutine does not starve another one, and is visible (longest_ready_run)
 *   6. a post that does not fit is counted, never silent (Dispatcher::dropped_resumes)
 */
#ifndef ABSTRACTX_SIM_HOST_CONFORMANCE_HPP
#define ABSTRACTX_SIM_HOST_CONFORMANCE_HPP

#undef NDEBUG
#include <atomic>
#include <cassert>
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <optional>
#include <thread>

#include "abstractx/abstractx.hpp"

namespace host_conformance {

using namespace abstractx;
using namespace abstractx::coro;

inline uint64_t now_us() { return hal::get_timer_driver().get_time_us(); }

// One-slot mailbox filled by another thread; the awaiter lives in the consumer's frame.
struct Mailbox {
    std::atomic<std::coroutine_handle<>> waiter{nullptr};  // shared: consumer stores, producer takes
    std::atomic<uint64_t> posted_at{0};                     // shared: producer writes before post
    uint32_t value{0};                                      // shared: producer writes before post
    struct Awaiter {
        Mailbox& m;
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept { m.waiter.store(h, std::memory_order_release); }
        uint32_t await_resume() const noexcept { return m.value; }
    };
    std::coroutine_handle<> take() {
        std::coroutine_handle<> h;
        while (!(h = waiter.exchange(nullptr, std::memory_order_acq_rel))) std::this_thread::yield();
        return h;
    }
};

inline bool flag(void* p) noexcept { return *static_cast<bool*>(p); }

// --- 1 ---------------------------------------------------------------------------------------
inline bool g_idle_done = false;
inline Task<void> idle_sleeper() {
    co_await hal::get_timer_driver().sleep_ms_async(200);
    g_idle_done = true;
}

template <typename Host>
void idle_stays_asleep() {
    (void)Host::waits(true);
    assert(spawn(idle_sleeper(), "idle"));
    assert(Host::run_until(&flag, &g_idle_done, 2'000'000));
    const uint32_t w = Host::waits(false);
    std::printf("[+] 1 idle: 200 ms with nothing to do, host blocked %u time(s)\n", w);
    assert(w <= 5 && "an idle host must not wake on an interval (100 us polling = ~2000)");
}

// --- 2 ---------------------------------------------------------------------------------------
inline Mailbox g_box2;
inline bool g_got2 = false;
inline uint64_t g_latency2 = 0;
inline Task<void> wait_one() {
    (void)co_await Mailbox::Awaiter{g_box2};
    g_latency2 = now_us() - g_box2.posted_at.load(std::memory_order_acquire);
    g_got2 = true;
}

template <typename Host>
void thread_post_wakes() {
    assert(spawn(wait_one(), "wait_one"));
    std::thread producer([] {
        auto h = g_box2.take();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // the host is asleep by now
        g_box2.posted_at.store(now_us(), std::memory_order_release);
        assert(abstractx::post(h));
    });
    assert(Host::run_until(&flag, &g_got2, 2'000'000));
    producer.join();
    std::printf("[+] 2 post from a thread woke the host in %llu us\n", (unsigned long long)g_latency2);
    assert(g_latency2 < 20'000 && "a post must wake the host promptly");
}

// --- 3 ---------------------------------------------------------------------------------------
inline bool g_timer3 = false;
inline uint64_t g_t3 = 0;
inline Task<void> sleep_20() {
    const uint64_t t0 = now_us();
    co_await hal::get_timer_driver().sleep_ms_async(20);
    g_t3 = now_us() - t0;
    g_timer3 = true;
}

template <typename Host>
void timer_wakes_on_time() {
    assert(spawn(sleep_20(), "sleep_20"));
    assert(Host::run_until(&flag, &g_timer3, 2'000'000));
    std::printf("[+] 3 a 20 ms sleep woke after %llu us\n", (unsigned long long)g_t3);
    assert(g_t3 >= 20'000 && g_t3 < 40'000);
}

// --- 4 ---------------------------------------------------------------------------------------
inline Mailbox g_box4;
inline uint64_t g_sum4 = 0;
inline bool g_done4 = false;
inline Task<void> consume_1000() {
    for (uint32_t i = 0; i < 1000; ++i) g_sum4 += co_await Mailbox::Awaiter{g_box4};
    g_done4 = true;
}

template <typename Host>
void nothing_lost() {
    assert(spawn(consume_1000(), "consume"));
    std::thread producer([] {
        for (uint32_t i = 1; i <= 1000; ++i) {
            auto h = g_box4.take();
            g_box4.value = i;
            assert(abstractx::post(h));  // no explicit wake(): the executor wakes the host
        }
    });
    assert(Host::run_until(&flag, &g_done4, 20'000'000) && "a lost wake leaves the host asleep");
    producer.join();
    assert(g_sum4 == 1000u * 1001u / 2u);
    std::puts("[+] 4 1000 posts from a thread, none lost");
}

// --- 5 ---------------------------------------------------------------------------------------
inline bool g_spin_stop = false;
inline bool g_other5 = false;
inline Task<void> spinner() {
    while (!g_spin_stop) co_await step_async();  // never waits
}
inline Task<void> other() {
    co_await step_async();
    g_other5 = true;
    g_spin_stop = true;
}

template <typename Host>
void ready_does_not_starve() {
    (void)longest_ready_run(true);
    assert(spawn(spinner(), "spinner"));
    assert(spawn(other(), "other"));
    assert(Host::run_until(&flag, &g_other5, 2'000'000) && "an always-ready coroutine starved another");
    std::printf("[+] 5 another coroutine ran beside an always-ready one; longest ready run %u\n",
                longest_ready_run());
    bool idle = false;
    (void)Host::run_until(&flag, &idle, 20'000);  // let the spinner see the stop flag
}

// --- 6 ---------------------------------------------------------------------------------------
inline Event g_never;
inline Task<void> waiter6() { co_await g_never; }

inline void full_queue_is_counted() {
    // Fill the ready queue past its capacity from a thread, then check every refused post is counted.
    const uint32_t before = Dispatcher::dropped_resumes();
    static std::optional<Task<void>> tasks[48];
    for (auto& t : tasks) {
        t.emplace(waiter6());
        t->resume();  // resume: starts the never-run task; it waits on an Event nobody sets
    }
    uint32_t refused = 0;
    std::thread t([&] {
        for (auto& task : tasks) if (!abstractx::post(task->handle())) ++refused;
    });
    t.join();
    const uint32_t counted = Dispatcher::dropped_resumes() - before;
    std::printf("[+] 6 %u of 48 posts did not fit; %u counted\n", refused, counted);
    assert(refused > 0 && counted == refused && "a post that does not fit must be counted");
    while (step() == kNow) {}
}

template <typename Host>
void run_all(const char* name) {
    std::printf("== host conformance: %s\n", name);
    idle_stays_asleep<Host>();
    thread_post_wakes<Host>();
    timer_wakes_on_time<Host>();
    nothing_lost<Host>();
    ready_does_not_starve<Host>();
    full_queue_is_counted();
    std::printf("[SUCCESS] host conformance: %s [SPEC-ARCH-16]\n", name);
}

} // namespace host_conformance

#endif // ABSTRACTX_SIM_HOST_CONFORMANCE_HPP
