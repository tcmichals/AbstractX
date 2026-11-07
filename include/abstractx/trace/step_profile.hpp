/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * step_profile.hpp - cycles per phase of abstractx::step(), for bench builds only.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "abstractx/trace/runtime_stats.hpp"

#ifndef ABX_STEP_PROFILE
#define ABX_STEP_PROFILE 0
#endif

namespace abstractx::stats {

enum class StepPhase : uint8_t { Timer, Dispatch, Io, Network, Flush, Tasks, Deadline, Count };

// One context only: step() writes it, and the bench reads it from a coroutine run by step().
// @impl [SPEC-E906-BENCH-09] apps/e906_bench/SPECIFICATION.md
struct StepProfile {
    std::array<uint64_t, static_cast<size_t>(StepPhase::Count)> cycles{};
    uint32_t passes = 0;
    uint32_t last = 0;

    // Called from a coroutine, so inside the Dispatch phase: `last` stays, or the next mark would be huge.
    void reset() noexcept {
        cycles = {};
        passes = 0;
    }
    void begin(uint32_t now) noexcept {
        last = now;
        ++passes;
    }
    // The time since the previous mark, wrap-safe, goes to this phase.
    void mark(StepPhase phase, uint32_t now) noexcept {
        cycles[static_cast<size_t>(phase)] += static_cast<uint32_t>(now - last);
        last = now;
    }
    uint64_t total(StepPhase phase) const noexcept { return cycles[static_cast<size_t>(phase)]; }
};

#if ABX_STEP_PROFILE
inline StepProfile g_step_profile;  // shared: none, see StepProfile
inline void step_begin() noexcept { g_step_profile.begin(static_cast<uint32_t>(now_cycles())); }
inline void step_mark(StepPhase phase) noexcept { g_step_profile.mark(phase, static_cast<uint32_t>(now_cycles())); }
#else
inline void step_begin() noexcept {}
inline void step_mark(StepPhase) noexcept {}
#endif

} // namespace abstractx::stats
