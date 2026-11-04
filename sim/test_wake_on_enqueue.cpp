// @verifies [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Every enqueue from another context wakes the host ([SPEC-ARCH-15], review T-5). The host here
 * blocks ONLY on (deadline returned by step(), wake hook), the way a Boost.Asio host does: no
 * polling interval. A completion that posts without waking would leave it asleep until give-up.
 * 1. a Linux I2C worker-thread completion (driver on /dev/null: completes with an error, fast)
 * 2. 1000 values posted by a plain thread
 * 3. a timer sleep (the deadline path)
 */
#undef NDEBUG
#include "abstractx/abstractx.hpp"
#include "linux_i2c_driver.hpp"

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

// The host's latched wake (like an Asio post or a task notification).
std::mutex g_mu;
std::condition_variable g_cv;
uint32_t g_wakes = 0;  // shared: wake hook (any thread) and host loop; g_mu
void host_wake() noexcept {
    { std::lock_guard<std::mutex> l(g_mu); ++g_wakes; }
    g_cv.notify_one();
}

// Run the host until done() or give_up; returns how many times it blocked.
template <typename Done>
uint32_t host_until(Done done, std::chrono::milliseconds give_up) {
    const auto limit = std::chrono::steady_clock::now() + give_up;
    uint32_t blocks = 0;
    while (!done()) {
        const uint64_t next = step();
        if (next == kNow || done()) continue;
        std::unique_lock<std::mutex> l(g_mu);
        auto until = limit;
        if (next != kNever) {
            const uint64_t now = hal::get_timer_driver().get_time_us();
            const auto d = std::chrono::steady_clock::now() + std::chrono::microseconds(next > now ? next - now : 0);
            if (d < until) until = d;
        }
        g_cv.wait_until(l, until, [] { return g_wakes != 0; });
        g_wakes = 0;
        ++blocks;
        assert(std::chrono::steady_clock::now() < limit && "host slept through a completion");
    }
    return blocks;
}

// --- 1 ---------------------------------------------------------------------------------------
bool g_i2c_done = false;
hal::I2cResult g_i2c{};
Task<void> i2c_read(hal::LinuxI2cDriver& d) {
    static uint8_t out[2];
    g_i2c = co_await d.read_regs_async(uint16_t{0x68}, uint8_t{0x10}, std::span<uint8_t>(out, 2));
    g_i2c_done = true;
}

void test_worker_completion_wakes() {
    hal::LinuxI2cDriver d("/dev/null");  // every I2C_RDWR fails: the worker completes with an error
    hal::I2cConfig c{};
    d.init(c);
    assert(spawn(i2c_read(d), "i2c"));
    const uint32_t blocks = host_until([] { return g_i2c_done; }, std::chrono::seconds(5));
    std::printf("[+] Linux I2C worker completion woke the host (status %d, blocked %u time(s))\n",
                static_cast<int>(g_i2c.status), blocks);
    d.shutdown();
}

// --- 2 ---------------------------------------------------------------------------------------
struct Mailbox {
    std::atomic<std::coroutine_handle<>> waiter{nullptr};  // shared: consumer stores, producer takes
    uint32_t value{0};  // shared: producer writes before post, consumer reads after resume
    struct Awaiter {
        Mailbox& m;
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept { m.waiter.store(h, std::memory_order_release); }
        uint32_t await_resume() const noexcept { return m.value; }
    };
} g_box;
uint64_t g_sum = 0;
bool g_mail_done = false;
Task<void> consumer() {
    for (uint32_t i = 0; i < 1000; ++i) g_sum += co_await Mailbox::Awaiter{g_box};
    g_mail_done = true;
}

void test_thread_posts_wake() {
    assert(spawn(consumer(), "consumer"));
    std::thread producer([] {
        for (uint32_t i = 1; i <= 1000; ++i) {
            std::coroutine_handle<> h;
            while (!(h = g_box.waiter.exchange(nullptr, std::memory_order_acq_rel))) std::this_thread::yield();
            g_box.value = i;
            assert(Dispatcher::post(h));  // no explicit wake(): post() must wake on hosted builds
        }
    });
    host_until([] { return g_mail_done; }, std::chrono::seconds(20));
    producer.join();
    assert(g_sum == 1000u * 1001u / 2u);
    std::puts("[+] 1000 posts from a thread, each woke the host (no explicit wake call)");
}

// --- 3 ---------------------------------------------------------------------------------------
bool g_slept = false;
Task<void> sleeper() {
    co_await hal::get_timer_driver().sleep_ms_async(20);
    g_slept = true;
}

void test_timer_wakes() {
    const auto t0 = std::chrono::steady_clock::now();
    assert(spawn(sleeper(), "sleeper"));
    const uint32_t blocks = host_until([] { return g_slept; }, std::chrono::seconds(5));
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("[+] 20 ms sleep woke the host after %lld ms, blocked %u time(s)\n", static_cast<long long>(ms), blocks);
    assert(ms >= 20 && ms < 200);
    assert(blocks <= 5 && "the host must not wake repeatedly while waiting (no polling)");
}

} // namespace

int main() {
    Config cfg{};
    cfg.trace.status_period_ms = 0;
    assert(init(cfg));
    set_wake({&host_wake, &host_wake});
    test_worker_completion_wakes();
    test_thread_posts_wake();
    test_timer_wakes();
    set_wake({});
    std::puts("[SUCCESS] every enqueue wakes the host [SPEC-ARCH-15]");
    return 0;
}
