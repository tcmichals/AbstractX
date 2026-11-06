// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// The one list of portable unit tests ([SPEC-AUDIT-18]); host and E906 run exactly this table.
#include "all_tests.hpp"

namespace abx_unit {
void tlp_layout_offsets(abstractx::test::Context&);
void tlp_little_endian_bytes(abstractx::test::Context&);
void tlp_payload_words_round_trip(abstractx::test::Context&);
void spsc_ring_order_and_bounds(abstractx::test::Context&);

// @impl [SPEC-AUDIT-18] docs/SPECTRACE.md (one table)
const abstractx::test::UnitTest kAllTests[] = {
    {"tlp_layout_offsets", &tlp_layout_offsets},
    {"tlp_little_endian_bytes", &tlp_little_endian_bytes},
    {"tlp_payload_words_round_trip", &tlp_payload_words_round_trip},
    {"spsc_ring_order_and_bounds", &spsc_ring_order_and_bounds},
};
const size_t kAllTestsCount = sizeof(kAllTests) / sizeof(kAllTests[0]);
} // namespace abx_unit
