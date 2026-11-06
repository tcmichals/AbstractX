// @verifies [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md
// @verifies [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md (asio_host run_until waits)
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The host conformance checks ([SPEC-ARCH-16] requirement 10) against the Boost.Asio host.
 */
#include "host_conformance.hpp"
#include "abstractx/host/asio_host.hpp"

namespace {
struct AsioHost {
    static bool run_until(bool (*done)(void*) noexcept, void* ctx, uint64_t give_up_us) noexcept {
        return abstractx::asio_host::run_until(done, ctx, give_up_us);
    }
    static uint32_t waits(bool reset) noexcept { return abstractx::asio_host::waits(reset); }
};
} // namespace

int main() {
    abstractx::Config cfg{};
    cfg.trace.status_period_ms = 0;
    assert(abstractx::init(cfg));
    host_conformance::run_all<AsioHost>("Boost.Asio (io_context + steady_timer + post)");
    return 0;
}
