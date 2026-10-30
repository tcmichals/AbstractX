/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Counters of the E906 DRDY-triggered auto-DMA channel.
 * @impl [SPEC-E906-HAL-05] targets/allwinner_e906/SPECIFICATION.md
 */
#pragma once

#include <stdint.h>

namespace hal::auto_dma {

// min/max/sum of a cycle count (mcycle, 200 MHz).
struct CycleStat {
    uint32_t min{0xFFFFFFFFU};
    uint32_t max{0};
    uint64_t sum{0};
    uint32_t n{0};
    void add(uint32_t v) noexcept {
        if (v < min) min = v;
        if (v > max) max = v;
        sum += v;
        ++n;
    }
};

struct Stats {
    uint32_t  edges{0};         // trigger edges handed to the engine
    uint32_t  busy_drops{0};    // edges dropped because SPI1 was busy with another transfer
    uint32_t  completed{0};     // DMA transfers finished OK
    uint32_t  failed{0};        // DMA transfers finished with an error
    CycleStat edge_to_start{};  // handler entry -> DMA started
    CycleStat dma{};            // DMA started -> DMA done interrupt
};

// Copy of the counters, taken with interrupts masked.
Stats stats() noexcept;
void reset() noexcept;

} // namespace hal::auto_dma
