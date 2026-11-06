// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic bus data only on request ([SPEC-LINUX-HAL-01] no silent simulation): ABSTRACTX_SIM_BUS=1.
// @impl [SPEC-LINUX-HAL-01] targets/linux/SPECIFICATION.md
#pragma once

#include <cstdlib>

namespace abstractx::hal {

inline bool linux_bus_sim_allowed() noexcept {
    // Read once; the environment is set before the process starts (tests, SITL).
    static const bool allowed = [] {
        const char* v = std::getenv("ABSTRACTX_SIM_BUS");  // NOLINT(concurrency-mt-unsafe): read once, static init
        return v != nullptr && v[0] != '\0' && !(v[0] == '0' && v[1] == '\0');
    }();
    return allowed;
}

} // namespace abstractx::hal
