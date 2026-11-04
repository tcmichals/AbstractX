// @verifies [SPEC-ARCH-16] [SPEC-ARCH-11] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * [SPEC-ARCH-16] requirement 8: destroying a waiting task cancels it; nothing is ever resumed or
 * written in a destroyed frame (review 2026-11-04 T-3, Q-6, Q-7).
 * 1. a task already woken (its handle in the ready ring) and then destroyed is never resumed; a new
 *    task that reuses the frame is not run by the stale wake
 * 3. a driver request whose task is destroyed (abort, else wait): not started -> removed, never
 *    started; running + abortable -> aborted; running + not abortable -> the destructor waits until
 *    the hardware finishes, and that late completion writes nothing and resumes nothing
 * 4. the Linux SPI worker skips a request whose awaiter is gone
 * 5. other awaiters withdraw: a TLP read destroyed while waiting (its late response frees the tag and
 *    resumes nothing); a UART RX read destroyed while waiting (later data wakes nothing)
 * 2. when_any cancels its loser: a loser waiting on something that never happens frees its frame at
 *    once (before: it kept the frame forever; 200 races leaked 25,600 B of pool); with_timeout won by
 *    the operation cancels its timer (no sleep left pending)
 */
#undef NDEBUG
#include "abstractx/abstractx.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

#include "linux_spi_driver.hpp"
#include "abstractx/hal/uart_rx.hpp"

using namespace abstractx;
using namespace abstractx::coro;

namespace {
Event g_go;
Event g_never;
int g_ran_past = 0;
int g_which = 0;
Task<void> victim(bool wait_never) {
    if (wait_never) co_await g_never;
    else co_await g_go;
    g_which = wait_never ? 2 : 1;
    ++g_ran_past;
}

void woken_then_destroyed() {
    {
        Task<void> a = victim(false);
        a.resume();      // resume: starts the never-run task; it waits on g_go
        g_go.set();      // a's handle is now in the ready ring
        assert(Dispatcher::has_pending_resumes());
    }                    // a destroyed before step(): its stale wake must go
    Task<void> b = victim(true);  // likely reuses a's frame
    b.resume();          // resume: starts the never-run task; it waits on g_never
    for (int i = 0; i < 5; ++i) (void)step();
    std::printf("[+] 1 woken-then-destroyed task: ran_past=%d which=%d, b.done=%d\n", g_ran_past, g_which,
                static_cast<int>(b.done()));
    assert(g_ran_past == 0 && !b.done() && "a stale wake must not resume a destroyed or reused frame");
}
Task<int> fast() { co_return 1; }
Task<int> hangs() { co_await g_never; co_return 2; }
int g_races = 0;
Task<void> racer() {
    for (int i = 0; i < 200; ++i) {
        auto v = co_await when_any(hangs(), fast());
        assert(v.index() == 1);
        ++g_races;
    }
}
Task<int> quick() { co_await step_async(); co_return 7; }
int g_wt = 0;
Task<void> timed() {
    for (int i = 0; i < 50; ++i) {
        auto r = co_await with_timeout(quick(), hal::get_timer_driver(), 100'000u);
        assert(r.index() == 0);
        ++g_wt;
    }
}

void losers_are_cancelled() {
    const size_t pool0 = coro_pool_used();
    assert(spawn(racer(), "racer"));
    while (g_races < 200) (void)step();
    while (step() == kNow) {}
    const size_t pool1 = coro_pool_used();
    std::printf("[+] 2 200 when_any races with a never-finishing loser: pool used %zu -> %zu\n", pool0, pool1);
    assert(pool1 == pool0 && "a cancelled loser frees its frame at once");
    assert(spawn(timed(), "timed"));
    while (g_wt < 50) { if (step() != kNow) hal::platform_idle_wait(); }
    while (step() == kNow) {}
    std::printf("[+] 2 50 with_timeout won by the operation: %zu sleeps left pending\n",
                hal::get_timer_driver().pending_sleeps());
    assert(hal::get_timer_driver().pending_sleeps() == 0 && "the losing timeout is cancelled");
}
// A SPI controller whose "hardware" is the test.
class FakeSpi : public hal::ISpi {
public:
    explicit FakeSpi(bool can_abort) : can_abort_(can_abort) {}
    bool init(const hal::SpiConfig&) override { return true; }
    void select(bool) override {}
    int starts{0}, aborts{0};
    std::atomic<bool> active_valid{false};
    void complete_one() {  // the transfer finishes (called from another thread in case 3c)
        hal::SpiRequest req = active_;
        active_valid.store(false);
        hal::SpiResult r{};
        r.transferred_bytes = 4;
        push_completion_from_isr(req, r);
        set_hardware_idle_from_isr();
    }

protected:
    void start_hardware_transfer_from_isr(const hal::SpiRequest& req) noexcept override {
        active_ = req;
        active_valid.store(true);
        ++starts;
    }
    bool abort_active_from_isr() noexcept override {
        if (!can_abort_ || !active_valid.load()) return false;
        active_valid.store(false);
        ++aborts;
        push_completion_from_isr(active_, hal::make_cancelled_result<hal::SpiResult>());
        return true;
    }

private:
    bool can_abort_;
    hal::SpiRequest active_{};
};

const uint8_t kTx[4] = {1, 2, 3, 4};
int g_resumed_after = 0;
Task<void> xfer(hal::ISpi& bus, hal::SpiResult& out) {
    uint8_t rx[4] = {};
    out = co_await bus.transfer_async(std::span<const uint8_t>(kTx, 4), std::span<uint8_t>(rx, 4));
    ++g_resumed_after;
}

void driver_requests_are_cancelled() {
    {   // 3a: not started (queued behind another) -> removed, never started
        FakeSpi bus(false);
        hal::SpiResult r0{}, r1{};
        r1.status = hal::SpiStatus::BusError;  // sentinel: must stay untouched
        auto t0 = xfer(bus, r0); t0.resume();  // resume: starts the never-run task; in flight
        {
            auto t1 = xfer(bus, r1); t1.resume();  // resume: starts the never-run task; queued
            assert(bus.has_pending_requests());
        }                                          // destroyed while queued
        assert(!bus.has_pending_requests() && bus.starts == 1 && r1.status == hal::SpiStatus::BusError);
        bus.complete_one();
        while (step() == kNow) {}
        assert(t0.done());
        std::puts("[+] 3a queued request of a destroyed task: removed, never started, nothing written");
    }
    {   // 3b: running, the driver can abort -> aborted
        FakeSpi bus(true);
        hal::SpiResult r{};
        r.status = hal::SpiStatus::BusError;
        { auto t = xfer(bus, r); t.resume(); }  // resume: starts the never-run task; destroyed in flight
        assert(bus.aborts == 1 && !bus.active_valid.load() && r.status == hal::SpiStatus::BusError);
        std::puts("[+] 3b running request of a destroyed task: aborted, nothing written");
    }
    {   // 3c: running, cannot abort -> the destructor waits for the hardware; nothing written or resumed
        FakeSpi bus(false);
        hal::SpiResult r{};
        r.status = hal::SpiStatus::BusError;
        g_resumed_after = 0;
        const auto t0 = std::chrono::steady_clock::now();
        std::thread hw([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            bus.complete_one();
        });
        { auto t = xfer(bus, r); t.resume(); }  // resume: starts the never-run task; destroyed in flight
        const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        hw.join();
        for (int i = 0; i < 5; ++i) (void)step();
        std::printf("[+] 3c running, not abortable: destructor waited %lld ms; result untouched, resumed %d\n",
                    static_cast<long long>(waited), g_resumed_after);
        assert(waited >= 40 && r.status == hal::SpiStatus::BusError && g_resumed_after == 0);
    }
}

void linux_worker_skips_abandoned() {
    hal::LinuxSpiDriver d("");  // no device: the worker completes requests itself
    hal::SpiConfig c{};
    d.init(c);
    hal::SpiResult r{};
    g_resumed_after = 0;
    for (int i = 0; i < 100; ++i) {
        auto t = xfer(d, r);
        t.resume();  // resume: starts the never-run task; handed to the worker, then destroyed
    }
    for (int i = 0; i < 20; ++i) { (void)step(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    d.shutdown();
    std::printf("[+] 4 100 Linux SPI requests destroyed while on the worker: resumed %d\n", g_resumed_after);
    assert(g_resumed_after == 0);
}
SpscTlpRing<> g_tx, g_rx;
CoroutineIoEngine g_eng{g_tx, g_rx};
Task<void> tlp_read() {
    (void)co_await g_eng.async_read(0x100);
    ++g_resumed_after;
}
hal::UartRxRing<64, 4> g_uart;
Task<void> uart_read() {
    (void)co_await g_uart.read_async(4, 0);
    ++g_resumed_after;
}

void other_awaiters_withdraw() {
    g_resumed_after = 0;
    {
        auto t = tlp_read();
        t.resume();  // resume: starts the never-run task; MemRd sent, waiting
    }                // destroyed while waiting
    Tlp64 sent{};
    assert(g_tx.pop(sent));
    assert(g_rx.push(Tlp64::make_completion_data(sent.wire.tag, 1, Channel::Control)));
    g_eng.poll_completions();
    for (int i = 0; i < 3; ++i) (void)step();
    // the tag was freed: 255 more reads can each get one
    int ok = 0;
    for (int i = 0; i < 255; ++i) {
        auto t = tlp_read();
        t.resume();  // resume: starts the never-run task
        Tlp64 req{};
        if (g_tx.pop(req)) ++ok;
        assert(g_rx.push(Tlp64::make_completion_data(req.wire.tag, 1, Channel::Control)));
        g_eng.poll_completions();
        while (step() == kNow) {}
    }
    std::printf("[+] 5 TLP read destroyed while waiting: then %d of 255 reads got a tag; resumed %d\n",
                ok, g_resumed_after);
    assert(ok == 255 && g_resumed_after == 255 && "no leaked tag; the destroyed read was not resumed");

    g_resumed_after = 0;
    {
        auto t = uart_read();
        t.resume();  // resume: starts the never-run task; waits for 4 bytes
    }                // destroyed while waiting
    g_uart.produced_from_isr(8, false);
    for (int i = 0; i < 3; ++i) (void)step();
    std::printf("[+] 5 UART RX read destroyed while waiting: data arrived, resumed %d\n", g_resumed_after);
    assert(g_resumed_after == 0);
}
} // namespace

int main() {
    Config cfg{};
    cfg.trace.status_period_ms = 0;
    assert(init(cfg));
    woken_then_destroyed();
    losers_are_cancelled();
    driver_requests_are_cancelled();
    linux_worker_skips_abandoned();
    other_awaiters_withdraw();
    std::puts("[SUCCESS] cancellation [SPEC-ARCH-16]");
    return 0;
}
