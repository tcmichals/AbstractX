// @verifies [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The host conformance checks ([SPEC-ARCH-16] requirement 10) against the POSIX host.
 */
#include "host_conformance.hpp"
#include "abstractx/host/posix_host.hpp"

namespace {
struct PosixHost {
    static bool run_until(bool (*done)(void*) noexcept, void* ctx, uint64_t give_up_us) noexcept {
        return abstractx::posix_host::run_until(done, ctx, give_up_us);
    }
    static uint32_t waits(bool reset) noexcept { return abstractx::posix_host::waits(reset); }
};
} // namespace

int main() {
    abstractx::Config cfg{};
    cfg.trace.status_period_ms = 0;
    assert(abstractx::init(cfg));
    host_conformance::run_all<PosixHost>("POSIX (eventfd + ppoll)");
    return 0;
}
