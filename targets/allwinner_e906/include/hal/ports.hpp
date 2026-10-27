/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Allwinner T527 / E906 port table: one entry per controller instance.
 */

#pragma once

#include "hal/ccu.hpp"
#include "hal/clic.hpp"
#include "memory_map.h"
#include <cstdint>

// @impl [SPEC-HAL-15] docs/DESIGN_SPECIFICATION.md
// @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md
namespace hal::ports {

struct PinMux {
    uintptr_t cfg;   // PIO config register, 0 = pins not routed
    uint32_t  mask;
    uint32_t  func;
};

// @impl [SPEC-HAL-15] docs/DESIGN_SPECIFICATION.md
struct TwiPort {
    uintptr_t        base;
    uint32_t         gic;    // routed to its CLIC group line ([SPEC-E906-IRQ-02])
    Ccu::BusModule   clock;
    PinMux           pins;
    const char*      name;
};

inline constexpr TwiPort kTwi0{TWI0_BASE, clic::kGicTwi0, Ccu::BusModule::Twi0,
                               {0, 0, 0}, "twi0"};
inline constexpr TwiPort kTwi1{TWI1_BASE, clic::kGicTwi1, Ccu::BusModule::Twi1,
                               {PIO_BASE + 0x0030U, 0xFFU << 16, 0x22U << 16}, "twi1"};  // PB4/PB5, function 2

} // namespace hal::ports
