/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: runtime status report [SPEC-TRACE-08]
 * A fake cycle clock drives the accounting: run time is charged to the resumed
 * frame, nested resumes, idle and interrupt cycles are not, and one stream-3 CTF
 * packet carries header + task + driver records at the documented offsets.
 * Steady state over 1000 rounds: pool high water and dropped resumes do not grow.
 */
// @verifies [SPEC-TRACE-08] docs/DESIGN_SPECIFICATION.md
#undef NDEBUG

#include "abstractx/abstractx.hpp"
#include "abstractx/trace/runtime_stats.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace abstractx;
using namespace abstractx::coro;

namespace {

uint64_t g_clock = 0;
uint64_t fake_clock() noexcept { return g_clock; }

struct Packet {
    uint8_t bytes[512]{};
    size_t len{0};
    bool empty() const { return len == 0; }
    size_t size() const { return len; }
    const uint8_t* data() const { return bytes; }
    uint8_t operator[](size_t i) const { return bytes[i]; }
    void clear() { len = 0; }
};
Packet g_last_status;
void capture(const uint8_t* data, size_t len, void*) {
    if (len >= 40 && len <= sizeof(g_last_status.bytes) && data[4] == stats::kStreamId) {
        std::memcpy(g_last_status.bytes, data, len);
        g_last_status.len = len;
    }
}

Event g_go;

// Busy 100 cycles per resume, of which 10 are spent in an "interrupt".
Task<void> worker(int rounds) {
    for (int i = 0; i < rounds; ++i) {
        g_clock += 100;
        stats::add_isr_cycles(10);
        co_await step_async();
    }
}

// 30 cycles of its own, then a nested Dispatcher pass (another task runs inside
// it, like the MicroPython VM hook) and 40 idle cycles (a sleep inside a script).
Task<void> host_task(int rounds) {
    for (int i = 0; i < rounds; ++i) {
        g_clock += 30;
        Dispatcher::process();
        stats::idle_begin();
        g_clock += 40;
        stats::idle_end();
        co_await step_async();
    }
}

template <typename T>
T rd(const Packet& b, size_t off) {
    T v{};
    std::memcpy(&v, b.data() + off, sizeof(T));
    return v;
}

const stats::TaskSlot* find(const char* name) {
    for (const auto& t : stats::g_state.tasks) {
        if (t.name && std::strcmp(t.name, name) == 0) return &t;
    }
    return nullptr;
}

} // namespace

int main() {
    Config cfg{};
    cfg.trace.status_period_ms = 1000;
    init(cfg);
    trace::g_tracer.set_flush_handler(capture, nullptr);
    stats::set_cycle_clock(fake_clock, 1'000'000);
    stats::reset();

    // 1. Charging: worker alone, 1000 rounds.
    constexpr int kRounds = 1000;
    spawn(worker(kRounds), "worker");
    const size_t hw0 = coro_pool_high_water();
    const uint32_t dropped0 = Dispatcher::dropped_resumes();
    while (Dispatcher::has_pending_resumes()) Dispatcher::process();
    const auto* w = find("worker");
    assert(w && "spawn(task, name) names the slot");
    if (w->resumes != kRounds + 1) {
        std::fprintf(stderr, "resumes %u run %llu max %llu\n", w->resumes,
                     static_cast<unsigned long long>(w->run_cycles), static_cast<unsigned long long>(w->max_cycles));
    }
    assert(w->resumes == kRounds + 1);        // initial run + one per step_async
    assert(w->run_cycles == kRounds * 90u);   // interrupt cycles are not task time
    assert(w->max_cycles == 100u);            // longest single resume, wall time
    assert(coro_pool_high_water() == hw0);
    assert(Dispatcher::dropped_resumes() == dropped0);

    // 2. Nesting and idle: host_task runs worker2 inside itself and sleeps.
    stats::reset();
    spawn(worker(2), "inner");
    spawn(host_task(2), "outer");
    while (Dispatcher::has_pending_resumes()) Dispatcher::process();
    const auto* outer = find("outer");
    const auto* inner = find("inner");
    assert(outer && inner);
    // outer: 30 own cycles twice; inner's 90 (run nested inside outer) and the idle 40s are not its own.
    assert(outer->run_cycles == 60u);
    assert(inner->run_cycles == 90u + 90u);   // initial run at spawn + the nested resume
    assert(stats::g_state.idle_cycles == 80u); // 40 at spawn + 40 on resume

    // 3. Report packet: stream 3, header + records at the documented offsets.
    g_last_status.clear();
    status_service();
    assert(!g_last_status.empty() && "status_service emits when due");
    const auto& b = g_last_status;
    assert(rd<uint32_t>(b, 0) == trace::CTF_MAGIC);
    constexpr size_t H = 40;  // CtfPacketHeaderWire
    assert(b[H + 0] == stats::kEventId && b[H + 1] == stats::kVersion);
    const uint8_t n_tasks = b[H + 2];
    const uint8_t n_drivers = b[H + 3];
    assert(n_tasks == 2 && n_drivers == 0);     // host has no interrupt counters
    assert(rd<uint32_t>(b, H + 12) == 1'000'000u);              // cycle_hz
    assert(rd<uint32_t>(b, H + 20) == 80u);                     // idle_cycles
    assert(rd<uint32_t>(b, H + 36) == coro_pool_capacity());    // pool_capacity
    assert(rd<uint16_t>(b, H + 46) == Dispatcher::QUEUE_CAPACITY);
    assert(b.size() == H + 56 + n_tasks * 28u);
    bool saw_outer = false;
    for (size_t i = 0; i < n_tasks; ++i) {
        const size_t o = H + 56 + i * 28;
        if (std::strncmp(reinterpret_cast<const char*>(b.data() + o), "outer", 12) == 0) {
            saw_outer = true;
            assert(rd<uint32_t>(b, o + 20) == 60u);  // run_cycles
        }
    }
    assert(saw_outer);

    // The Python decoder test reads this packet, so both sides share one layout.
    if (const char* path = std::getenv("RUNTIME_STATS_DUMP")) {
        if (FILE* f = std::fopen(path, "wb")) {
            std::fwrite(b.data(), 1, b.size(), f);
            std::fclose(f);
        }
    }

    // 4. Not due again until the period passes.
    g_last_status.clear();
    status_service();
    assert(g_last_status.empty());
    assert(status_deadline_us() != UINT64_MAX);

    std::printf("[PASS] runtime status: charging, nesting, idle, ISR, packet layout (%d rounds)\n", kRounds);
    return 0;
}
