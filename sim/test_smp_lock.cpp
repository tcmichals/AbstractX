/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: cross-core InterruptLock [SPEC-ARCH-01]
 * SmpInterruptLock with two host threads as the two cores (thread-local core id, interrupt mask
 * modelled per core). Checks mutual exclusion, per-core nesting and exact interrupt restore, and
 * an ETL queue_spsc_isr shared by two "cores" (the Pico 2 W Core 0 / Core 1 case).
 * Build with -DABSTRACTX_TEST_LEGACY_LOCK to run the same cases against the pre-2026-10-28
 * bare-metal algorithm (one global depth / saved state, no spinlock): it fails.
 */
// @verifies [SPEC-ARCH-01] docs/DESIGN_SPECIFICATION.md

#include "abstractx/interrupt_lock.hpp"

#include <etl/queue_spsc_isr.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

using abstractx::interrupt_flags_t;

namespace {

thread_local uint32_t t_core = 0;
thread_local bool t_irq_on = true;   // this "core"'s interrupt enable

struct HostCores {
    static uint32_t core_id() noexcept { return t_core; }
    static interrupt_flags_t irq_save() noexcept {
        const interrupt_flags_t was = t_irq_on ? 1U : 0U;
        t_irq_on = false;
        return was;
    }
    static void irq_restore(interrupt_flags_t f) noexcept { t_irq_on = f != 0U; }
};

#ifdef ABSTRACTX_TEST_LEGACY_LOCK
// The bare-metal InterruptLock before 2026-10-28, with the host interrupt model.
struct Lock {
    static void lock() noexcept {
        const interrupt_flags_t flags = HostCores::irq_save();
        if (depth_ == 0U) saved_ = flags;
        ++depth_;
    }
    static void unlock() noexcept {
        if (depth_ == 0U) return;
        if (--depth_ == 0U) HostCores::irq_restore(saved_);
    }
    static inline uint32_t depth_{0U};
    static inline interrupt_flags_t saved_{0U};
};
#else
using Lock = abstractx::SmpInterruptLock<HostCores, 2>;
#endif

constexpr int kRounds = 200'000;

// 1. Mutual exclusion: a plain counter only stays exact if the cores never overlap.
bool exclusion() {
    long counter = 0;   // guarded by Lock only
    std::atomic<int> inside{0};
    std::atomic<bool> overlap{false};
    auto work = [&](uint32_t core) {
        t_core = core;
        for (int i = 0; i < kRounds; ++i) {
            Lock::lock();
            if (inside.fetch_add(1, std::memory_order_relaxed) != 0) overlap = true;
            counter = counter + 1;
            inside.fetch_sub(1, std::memory_order_relaxed);
            Lock::unlock();
        }
    };
    std::thread a(work, 0U), b(work, 1U);
    a.join();
    b.join();
    const bool ok = !overlap && counter == 2L * kRounds;
    if (!ok) std::printf("[FAIL] exclusion: counter %ld of %ld, overlap %d\n", counter, 2L * kRounds, overlap.load());
    return ok;
}

// 2. Per-core nesting: each core nests 3 deep while the other does the same; interrupts come
//    back on only at that core's outermost unlock, and exactly as they were.
bool nesting() {
    std::atomic<bool> bad{false};
    auto work = [&](uint32_t core) {
        t_core = core;
        for (int i = 0; i < kRounds / 4; ++i) {
            t_irq_on = (i & 1) == 0;           // sometimes enter with interrupts already off
            const bool entry = t_irq_on;
            Lock::lock();
            Lock::lock();
            Lock::lock();
            Lock::unlock();
            Lock::unlock();
            if (t_irq_on) bad = true;          // still nested: must stay masked
            Lock::unlock();
            if (t_irq_on != entry) bad = true; // outermost: exactly the entry state
        }
    };
    std::thread a(work, 0U), b(work, 1U);
    a.join();
    b.join();
    if (bad) std::printf("[FAIL] nesting: interrupt state restored early or wrongly\n");
    return !bad;
}

// 3. One ETL queue_spsc_isr shared by two cores (producer on core 0, consumer on core 1), every
//    operation through the lock: every item arrives once, in order.
bool shared_queue() {
    etl::queue_spsc_isr<uint32_t, 64, Lock> q;
    constexpr uint32_t kItems = 100'000;
    bool ordered = true;
    std::atomic<bool> stop{false};
    std::thread prod([&] {
        t_core = 0;
        for (uint32_t i = 0; i < kItems && !stop;) {
            if (q.push(i)) ++i;
        }
    });
    uint32_t got = 0;
    std::thread cons([&] {
        t_core = 1;
        uint32_t v = 0;
        auto last = std::chrono::steady_clock::now();
        while (got < kItems) {
            if (q.pop(v)) {
                ordered = ordered && v == got;
                ++got;
                last = std::chrono::steady_clock::now();
            } else if (std::chrono::steady_clock::now() - last > std::chrono::seconds(3)) {
                stop = true;   // a lost or corrupted count: report instead of hanging
                break;
            }
        }
    });
    prod.join();
    cons.join();
    const bool ok = ordered && got == kItems;
    if (!ok) std::printf("[FAIL] shared queue: %u of %u items, in order %d\n", got, kItems, ordered);
    return ok;
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const bool e = exclusion();
    const bool n = nesting();
    const bool q = shared_queue();
    if (!(e && n && q)) return 1;
    std::printf("[PASS] SMP InterruptLock: exclusion %d x 2, per-core nesting and restore, shared queue in order\n", kRounds);
    return 0;
}
