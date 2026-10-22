/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Runtime status ([SPEC-TRACE-08]): per-coroutine run time, idle time, interrupt
 * time, driver counters and pool health, reported once per period as one CTF
 * packet (stream 3). Time is charged in slices, like an RTOS switch-in/out hook:
 * every resume start/end, idle start/end and report charges the cycles since the
 * last mark (minus interrupt cycles) to whoever owned the CPU.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace abstractx::stats {

inline constexpr size_t   kMaxTasks   = 8;  // the last slot collects untracked frames ("other")
inline constexpr size_t   kMaxDrivers = 6;
inline constexpr uint8_t  kStreamId   = 3;
inline constexpr uint8_t  kEventId    = 0x20;
inline constexpr uint8_t  kVersion    = 1;

// @impl [SPEC-TRACE-08] docs/DESIGN_SPECIFICATION.md
#pragma pack(push, 1)
struct RuntimeStatsHeader {
    uint8_t  event_id{kEventId};
    uint8_t  version{kVersion};
    uint8_t  n_tasks{0};
    uint8_t  n_drivers{0};
    uint64_t timestamp_us{0};
    uint32_t cycle_hz{0};
    uint32_t cycles{0};
    uint32_t idle_cycles{0};
    uint32_t steps{0};
    uint32_t pool_used{0};
    uint32_t pool_high_water{0};
    uint32_t pool_capacity{0};
    uint32_t dropped_resumes{0};
    uint16_t queue_depth{0};
    uint16_t queue_capacity{0};
    uint32_t trace_discarded{0};
    uint32_t reserved{0};
};

struct TaskStatsRecord {
    char     name[12]{};
    uint32_t frame_addr{0};
    uint32_t resumes{0};
    uint32_t run_cycles{0};
    uint32_t max_cycles{0};
};

struct DriverStatsRecord {
    char     name[8]{};
    uint16_t id{0};
    uint16_t reserved{0};
    uint32_t events{0};
    uint32_t cycles{0};
    uint32_t errors{0};
};
#pragma pack(pop)

static_assert(sizeof(RuntimeStatsHeader) == 56, "wire layout [SPEC-TRACE-08]");
static_assert(sizeof(TaskStatsRecord) == 28, "wire layout [SPEC-TRACE-08]");
static_assert(sizeof(DriverStatsRecord) == 24, "wire layout [SPEC-TRACE-08]");

inline constexpr size_t kMaxReportBytes =
    sizeof(RuntimeStatsHeader) + kMaxTasks * sizeof(TaskStatsRecord) + kMaxDrivers * sizeof(DriverStatsRecord);
static_assert(kMaxReportBytes + 40 <= 496, "a full report must fit one E906 RPMsg buffer");

// Filled by the platform for each report.
struct DriverStats {
    const char* name{nullptr};
    uint16_t    id{0};
    uint32_t    events{0};
    uint32_t    cycles{0};
    uint32_t    errors{0};
};

struct PlatformStats {
    DriverStats drivers[kMaxDrivers]{};
    size_t      n_drivers{0};
};

using CycleClock = uint64_t (*)() noexcept;
// Timeline span sink ([SPEC-TRACE-10]): kind (1 resume, 3 idle), id, start cycle, cycles.
using SpanHook = void (*)(uint8_t kind, uint32_t id, uint64_t start, uint64_t cycles) noexcept;

struct TaskSlot {
    uintptr_t   frame{0};
    const char* name{nullptr};
    uint32_t    resumes{0};
    uint64_t    run_cycles{0};
    uint64_t    max_cycles{0};
};

// Coroutine-domain state: touched only by Dispatcher::process(), the idle loop and
// the report, which all run in the cooperative loop. Interrupt handlers only add to
// isr_cycles.
struct State {
    CycleClock clock{nullptr};
    uint32_t   hz{0};
    TaskSlot   tasks[kMaxTasks]{};
    uint64_t   idle_cycles{0};
    uint32_t   steps{0};
    TaskSlot*  owner{nullptr};  // nullptr = loop overhead (busy, no task)
    uint64_t   mark{0};
    uint32_t   isr_mark{0};
    bool       idle{false};
    uint64_t   idle_start{0};
    SpanHook   span_hook{nullptr};
};

inline State g_state{};
inline std::atomic<uint32_t> g_isr_cycles{0};

inline uint64_t now_cycles() noexcept { return g_state.clock ? g_state.clock() : 0; }

inline void set_cycle_clock(CycleClock clock, uint32_t hz) noexcept {
    g_state.clock = clock;
    g_state.hz = hz;
    g_state.mark = now_cycles();
    g_state.isr_mark = g_isr_cycles.load(std::memory_order_relaxed);
}

// Interrupt handlers report their own cost (any context).
inline void add_isr_cycles(uint32_t cycles) noexcept {
    g_isr_cycles.fetch_add(cycles, std::memory_order_relaxed);
}

// Charge the time since the last mark, minus interrupt time, to the current owner.
inline void account() noexcept {
    const uint64_t now = now_cycles();
    const uint32_t isr = g_isr_cycles.load(std::memory_order_relaxed);
    uint64_t elapsed = now - g_state.mark;
    const uint32_t in_isr = isr - g_state.isr_mark;
    elapsed = elapsed > in_isr ? elapsed - in_isr : 0;
    if (g_state.idle) {
        g_state.idle_cycles += elapsed;
    } else if (g_state.owner) {
        g_state.owner->run_cycles += elapsed;
    }
    g_state.mark = now;
    g_state.isr_mark = isr;
}

inline TaskSlot* slot_for(uintptr_t frame) noexcept {
    constexpr size_t kOther = kMaxTasks - 1;
    for (size_t i = 0; i < kOther; ++i) {
        if (g_state.tasks[i].frame == frame) return &g_state.tasks[i];
    }
    for (size_t i = 0; i < kOther; ++i) {
        if (g_state.tasks[i].frame == 0) {
            g_state.tasks[i].frame = frame;
            return &g_state.tasks[i];
        }
    }
    g_state.tasks[kOther].name = "other";
    return &g_state.tasks[kOther];
}

// Name a root frame (abstractx::spawn). Reusing a freed frame's address restarts its counts.
inline void name_task(const void* frame, const char* name) noexcept {
    TaskSlot* s = slot_for(reinterpret_cast<uintptr_t>(frame));
    if (s == &g_state.tasks[kMaxTasks - 1]) return;
    *s = TaskSlot{reinterpret_cast<uintptr_t>(frame), name, 0, 0, 0};
}

// Wrap one resume: the frame owns the CPU until it returns (nested resumes and
// idle inside it are charged to themselves).
struct ResumeScope {
    TaskSlot* prev;
    TaskSlot* self;
    uint64_t  start;
    explicit ResumeScope(const void* frame) noexcept
        : prev(g_state.owner), self(slot_for(reinterpret_cast<uintptr_t>(frame))), start(0) {
        account();
        start = g_state.mark;
        ++self->resumes;
        g_state.owner = self;
    }
    ~ResumeScope() {
        account();
        const uint64_t wall = g_state.mark - start;
        if (wall > self->max_cycles) self->max_cycles = wall;
        g_state.owner = prev;
        // @impl [SPEC-TRACE-10] docs/DESIGN_SPECIFICATION.md
        if (g_state.span_hook) g_state.span_hook(1, static_cast<uint32_t>(self->frame), start, wall);
    }
    ResumeScope(const ResumeScope&) = delete;
    ResumeScope& operator=(const ResumeScope&) = delete;
};

// The platform's sleep (wfi / wfe / yield) is wrapped in these.
inline void idle_begin() noexcept {
    account();
    g_state.idle = true;
    g_state.idle_start = g_state.mark;
}
inline void idle_end() noexcept {
    account();
    g_state.idle = false;
    // @impl [SPEC-TRACE-10] docs/DESIGN_SPECIFICATION.md
    if (g_state.span_hook) g_state.span_hook(3, 0, g_state.idle_start, g_state.mark - g_state.idle_start);
}

inline void count_step() noexcept { ++g_state.steps; }

inline void reset() noexcept {
    const CycleClock c = g_state.clock;
    const uint32_t hz = g_state.hz;
    const SpanHook hook = g_state.span_hook;
    g_state = State{};
    g_state.span_hook = hook;
    set_cycle_clock(c, hz);
}

} // namespace abstractx::stats
