// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// TLP wire layout on the compiler that builds it ([SPEC-TLP-01], [SPEC-AUDIT-18]): host and E906.
// @verifies [SPEC-TLP-01] docs/DESIGN_SPECIFICATION.md (Tlp64 payload_word set_payload_word make_mem_write)
#include "abstractx/test/unit.hpp"
#include "asp_tlp64.hpp"

#include <cstddef>

using abstractx::Tlp64;
using abstractx::test::Context;

namespace abx_unit {

void tlp_layout_offsets(Context& c) {
    ABX_CHECK(c, sizeof(asp_tlp64_t) == 64);
    ABX_CHECK(c, offsetof(asp_tlp64_t, target_address) == 4);
    ABX_CHECK(c, offsetof(asp_tlp64_t, length_dw) == 8);
    ABX_CHECK(c, offsetof(asp_tlp64_t, timestamp_ns) == 12);
    ABX_CHECK(c, offsetof(asp_tlp64_t, payload) == 20);
    ABX_CHECK(c, offsetof(asp_tlp64_t, crc32) == 60);
}

void tlp_little_endian_bytes(Context& c) {
    const Tlp64 w = Tlp64::make_mem_write(0x40000008u, 0xCAFEF00Du, 1);
    const auto* b = reinterpret_cast<const uint8_t*>(&w.wire);
    ABX_CHECK(c, b[0] == 0x02 && b[2] == 0x01 && b[3] == 0x01);
    ABX_CHECK(c, b[4] == 0x08 && b[7] == 0x40);              // address little-endian
    ABX_CHECK(c, b[20] == 0x0D && b[21] == 0xF0 && b[22] == 0xFE && b[23] == 0xCA);
    ABX_CHECK(c, w.payload_word(0) == 0xCAFEF00Du);
}

void tlp_payload_words_round_trip(Context& c) {
    Tlp64 t{};
    for (uint32_t k = 0; k < 10; ++k) t.set_payload_word(k, 0x11223344u ^ (k * 0x01010101u));
    for (uint32_t k = 0; k < 10; ++k) ABX_CHECK(c, t.payload_word(k) == (0x11223344u ^ (k * 0x01010101u)));
}

} // namespace abx_unit
