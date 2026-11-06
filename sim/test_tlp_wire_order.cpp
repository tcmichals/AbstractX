// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// @verifies [SPEC-TLP-01] docs/DESIGN_SPECIFICATION.md (little-endian wire bytes of C++-built TLPs)
// The expected bytes are the ones the cocotb tests send through the RTL (struct "<BBBBIHHQ40sI",
// sim/cocotb/test_asp_tlp_axil_master_cocotb.py), so a C++ TLP and an RTL TLP are the same 64 bytes.
#include "asp_tlp64.hpp"

#include <cstdio>
#include <cstring>

using abstractx::Tlp64;

static int g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
            ++g_fail;                                                         \
        }                                                                     \
    } while (0)

static const uint8_t* bytes(const Tlp64& t) { return reinterpret_cast<const uint8_t*>(&t.wire); }

int main() {
    // MemWrite 0x40000008 <- 0xCAFEF00D, tag 1
    const Tlp64 w = Tlp64::make_mem_write(0x40000008u, 0xCAFEF00Du, 1);
    const uint8_t exp_w[24] = {0x02, 0x00, 0x01, 0x01,             // type, flags, tag, channel
                               0x08, 0x00, 0x00, 0x40,             // target_address LE
                               0x01, 0x00, 0x00, 0x00,             // length_dw 1, sequence 0
                               0, 0, 0, 0, 0, 0, 0, 0,             // timestamp
                               0x0D, 0xF0, 0xFE, 0xCA};            // payload word 0 LE
    CHECK(std::memcmp(bytes(w), exp_w, sizeof exp_w) == 0);
    CHECK(w.payload_word(0) == 0xCAFEF00Du);

    // Burst of 9: word k at payload bytes 4k..4k+3, little-endian; count clipped to 9
    uint32_t v[12];
    for (uint32_t k = 0; k < 12; ++k) v[k] = 0x11223300u + k;
    const Tlp64 b = Tlp64::make_mem_write_burst(0x40000700u, v, 12);
    CHECK(b.wire.length_dw == 9);
    for (uint32_t k = 0; k < 9; ++k) {
        CHECK(b.payload_word(k) == v[k]);
        CHECK(bytes(b)[20 + 4 * k] == static_cast<uint8_t>(k));       // least significant byte first
        CHECK(bytes(b)[23 + 4 * k] == 0x11);
    }
    CHECK(b.payload_word(9) == 0);

    // CplD: read data little-endian (what asp_tlp_axil_master returns)
    const Tlp64 c = Tlp64::make_cpl_d(0x11, 0xABF10164u, abstractx::Channel::Control);
    CHECK(bytes(c)[0] == 0x03 && bytes(c)[2] == 0x11 && bytes(c)[3] == 0x01);
    CHECK(bytes(c)[20] == 0x64 && bytes(c)[21] == 0x01 && bytes(c)[22] == 0xF1 && bytes(c)[23] == 0xAB);

    // set / get round trip on every word
    Tlp64 r{};
    for (uint32_t k = 0; k < 10; ++k) r.set_payload_word(k, 0xA0B0C0D0u ^ k);
    for (uint32_t k = 0; k < 10; ++k) CHECK(r.payload_word(k) == (0xA0B0C0D0u ^ k));

    std::printf("%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
