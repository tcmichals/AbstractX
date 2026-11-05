/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: hosted platform_idle_wait() sleeps, never spins [SPEC-ARCH-06] (EL-1027-18).
 * Built once per hosted target file (Linux, Zynq-7000). Over ~20 ms of back-to-back idle
 * waits the process CPU time must stay well below wall time; a sched_yield() loop on an
 * idle core burns ~100%.
 */
// @verifies [SPEC-ARCH-06] docs/DESIGN_SPECIFICATION.md
#include "abstractx/hal/platform.hpp"

#include <cstdio>
#include <ctime>

namespace {
double now_s(clockid_t id) {
    timespec ts{};
    clock_gettime(id, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}
} // namespace

int main() {
    const double wall0 = now_s(CLOCK_MONOTONIC);
    const double cpu0 = now_s(CLOCK_PROCESS_CPUTIME_ID);
    int calls = 0;
    while (now_s(CLOCK_MONOTONIC) - wall0 < 0.02) {
        abstractx::hal::platform_idle_wait();
        ++calls;
    }
    const double wall = now_s(CLOCK_MONOTONIC) - wall0;
    const double cpu = now_s(CLOCK_PROCESS_CPUTIME_ID) - cpu0;
    const double ratio = cpu / wall;
    std::printf("idle wait: %d calls, wall %.1f ms, cpu %.1f ms (%.0f%%)\n", calls, wall * 1e3, cpu * 1e3,
                ratio * 100.0);
    if (ratio > 0.5) {
        std::printf("[FAIL] platform_idle_wait() spins: cpu/wall %.2f > 0.50\n", ratio);
        return 1;
    }
    std::printf("[PASS] platform_idle_wait() sleeps\n");
    return 0;
}
