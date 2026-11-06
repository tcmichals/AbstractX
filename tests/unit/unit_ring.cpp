// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// SPSC ring on the target's atomics ([SPEC-AUDIT-18]): order, full / empty, 1000 wraps.
// @verifies [SPEC-TLP-03] docs/DESIGN_SPECIFICATION.md (SpscRingBuffer push pop)
#include "abstractx/test/unit.hpp"
#include "tlp_ring.hpp"

using abstractx::test::Context;

namespace abx_unit {

void spsc_ring_order_and_bounds(Context& c) {
    static abstractx::SpscRingBuffer<uint32_t, 8> ring;   // static: no stack pressure on the target
    uint32_t v = 0;
    ABX_CHECK(c, !ring.pop(v));
    uint32_t pushed = 0;
    while (ring.push(pushed)) ++pushed;
    ABX_CHECK(c, pushed >= 7 && pushed <= 8);               // full stops pushes
    for (uint32_t i = 0; i < pushed; ++i) ABX_CHECK(c, ring.pop(v) && v == i);
    ABX_CHECK(c, !ring.pop(v));
    for (uint32_t i = 0; i < 1000; ++i) {                  // wraps many times, order kept
        ABX_CHECK(c, ring.push(i));
        ABX_CHECK(c, ring.pop(v) && v == i);
    }
}

} // namespace abx_unit
