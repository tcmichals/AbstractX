/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: producer/waiter wake handshake under store->load reordering [SPEC-HAL-10] [SPEC-HAL-13]
 * The producer (a thread standing in for the ISR / other core) publishes data then looks for a
 * waiter; the reader publishes itself as waiter then looks for data. Lockstep, one item per
 * round, so every round races the producer against await_suspend. A lost wake stalls the
 * reader: the watchdog fails the test instead of hanging ctest (2026-10-28: the old
 * release/acquire-only handshake hung sim_test_uart_rx_ring in ~1% of runs).
 */
// @verifies [SPEC-HAL-10] docs/DESIGN_SPECIFICATION.md
// @verifies [SPEC-HAL-13] docs/DESIGN_SPECIFICATION.md
#undef NDEBUG

#include "abstractx/hal/bus_trigger.hpp"
#include "abstractx/hal/uart_rx.hpp"
#include "asp_coro.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace abstractx::hal;
using abstractx::Dispatcher;
using abstractx::coro::Task;

namespace {

constexpr uint32_t kRounds = 300'000;

// Pump the Dispatcher until `done` or no progress for 2 s (a lost wake).
template <typename Done>
bool pump_until(Done done, const std::atomic<uint32_t>& progress) {
    uint32_t last = progress.load();
    auto t0 = std::chrono::steady_clock::now();
    while (!done()) {
        Dispatcher::process();
        const uint32_t now = progress.load(std::memory_order_acquire);
        if (now != last) {
            last = now;
            t0 = std::chrono::steady_clock::now();
        } else if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(2)) {
            return false;
        }
    }
    return true;
}

bool uart_ring_rounds() {
    UartRxRing<64, 4> ring;
    std::atomic<uint32_t> released{0};
    std::atomic<bool> stop{false};
    std::thread producer([&] {
        auto mem = ring.dma_region();
        for (uint32_t i = 0; i < kRounds; ++i) {
            while (released.load(std::memory_order_acquire) != i) {
                if (stop.load(std::memory_order_relaxed)) return;
            }
            mem[i % mem.size()] = static_cast<uint8_t>(i);
            ring.produced_from_isr(1, false);
        }
    });
    auto reader = [](UartRxRingBase& r, std::atomic<uint32_t>& rel) -> Task<void> {
        for (uint32_t i = 0; i < kRounds; ++i) {
            UartRxView v = co_await r.read_async(1, 0);
            assert(v.status == UartStatus::Ok && v.size() == 1);
            assert(v.first[0] == static_cast<uint8_t>(i));
            assert(r.release(1) == UartStatus::Ok);
            rel.store(i + 1U, std::memory_order_release);
        }
    }(ring, released);
    reader.resume();
    const bool ok = pump_until([&] { return reader.done(); }, released);
    stop.store(true);
    producer.join();
    if (!ok) {
        std::printf("[FAIL] UartRxRing: lost wake after %u of %u rounds\n", released.load(), kRounds);
    }
    return ok;
}

bool trigger_ring_rounds() {
    TriggerRing<4, 2> ring;
    std::atomic<uint32_t> released{0};
    std::atomic<bool> stop{false};
    std::thread producer([&] {
        for (uint32_t i = 0; i < kRounds; ++i) {
            while (released.load(std::memory_order_acquire) != i) {
                if (stop.load(std::memory_order_relaxed)) return;
            }
            auto slot = ring.acquire_from_isr();
            assert(!slot.empty());
            slot[0] = static_cast<uint8_t>(i);
            ring.commit_from_isr(IoStatus::Ok, 1, i);
        }
    });
    auto reader = [](TriggerRingBase& r, std::atomic<uint32_t>& rel) -> Task<void> {
        for (uint32_t i = 0; i < kRounds; ++i) {
            TriggerSample s = co_await r.pop();
            assert(s.status == IoStatus::Ok && s.data.size() == 1);
            assert(s.data[0] == static_cast<uint8_t>(i));
            r.release();
            rel.store(i + 1U, std::memory_order_release);
        }
    }(ring, released);
    reader.resume();
    const bool ok = pump_until([&] { return reader.done(); }, released);
    stop.store(true);
    producer.join();
    if (!ok) {
        std::printf("[FAIL] TriggerRing: lost wake after %u of %u rounds\n", released.load(), kRounds);
    }
    return ok;
}

} // namespace

int main() {
    const bool uart_ok = uart_ring_rounds();
    const bool trig_ok = trigger_ring_rounds();
    if (!uart_ok || !trig_ok) {
        // The stalled coroutine frames are abandoned on purpose: the test has already failed.
        std::fflush(stdout);
        std::_Exit(1);
    }
    assert(Dispatcher::queue_depth() == 0);
    std::printf("[PASS] wake handshake: %u lockstep rounds each for UartRxRing and TriggerRing, no lost wake\n", kRounds);
    return 0;
}
