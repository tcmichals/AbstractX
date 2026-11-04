/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Deadlines the host waits for, and drift-free periodic schedules.
 * @impl [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md
 */
#ifndef ABSTRACTX_DEADLINE_HPP
#define ABSTRACTX_DEADLINE_HPP

#include <cstdint>

namespace abstractx {

// step() / ITimer::service_due_us(): call again now, at a deadline (us), or only on a wake.
inline constexpr uint64_t kNow   = 0;
inline constexpr uint64_t kNever = UINT64_MAX;

// next = previous + period. When that slot is already past, skip to the latest slot that is not
// in the future and count the skipped slots in overruns; never one call per missed period.
constexpr uint64_t next_period(uint64_t previous, uint64_t period, uint64_t now,
                               uint32_t& overruns) noexcept {
    if (period == 0) return now;
    uint64_t next = previous + period;
    if (next < now) {
        const uint64_t missed = (now - next - 1u) / period + 1u;
        overruns += static_cast<uint32_t>(missed);
        next += missed * period;
    }
    return next;
}

} // namespace abstractx

#endif // ABSTRACTX_DEADLINE_HPP
