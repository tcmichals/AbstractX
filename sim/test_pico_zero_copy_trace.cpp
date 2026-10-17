/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: Pico 2 W Zero-Copy lwIP PBUF_POOL Trace Transport
 * @verifies [SPEC-PICO-TRACE-01] [SPEC-TRACE-07] [SPEC-ARCH-08] targets/pico2w_rp2350/SPECIFICATION.md#37-zero-copy-trace--telemetry-over-udp-spec-pico-trace-01
 */

#include "hal/pico_trace_transport.hpp"
#include "abstractx/trace/tracer.hpp"

#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>

namespace {

// Mock lwIP memory and state tracker
struct MockPbufArena {
    static constexpr size_t kPoolSize = 8;
    static constexpr size_t kBufSize = 1500;

    alignas(4) uint8_t storage[kPoolSize][kBufSize];
    pbuf pbufs[kPoolSize];
    bool in_use[kPoolSize]{false};

    bool force_alloc_fail{false};
    bool force_chained_pbuf{false};
    pbuf chained_extra{};

    // Spy on udp_sendto calls
    struct SentPacket {
        const void* payload_ptr{nullptr};
        uint16_t    length{0};
        uint16_t    dst_port{0};
    };
    std::vector<SentPacket> sent_packets;

    void reset() {
        for (size_t i = 0; i < kPoolSize; ++i) {
            in_use[i] = false;
            pbufs[i].payload = storage[i];
            pbufs[i].len = kBufSize;
            pbufs[i].tot_len = kBufSize;
            pbufs[i].next = nullptr;
            pbufs[i].ref = 1;
        }
        force_alloc_fail = false;
        force_chained_pbuf = false;
        chained_extra = pbuf{};
        sent_packets.clear();
    }
} g_mock_lwip;

} // anonymous namespace

// Implementation of mock lwIP C API for host testing
extern "C" {

struct pbuf* pbuf_alloc(pbuf_layer /*layer*/, uint16_t length, pbuf_type type) {
    assert(type == PBUF_POOL && "Only PBUF_POOL is permitted for zero-copy tracing [SPEC-PICO-TRACE-01]");
    if (g_mock_lwip.force_alloc_fail) {
        return nullptr;
    }
    for (size_t i = 0; i < MockPbufArena::kPoolSize; ++i) {
        if (!g_mock_lwip.in_use[i]) {
            g_mock_lwip.in_use[i] = true;
            pbuf* p = &g_mock_lwip.pbufs[i];
            p->len = length;
            p->tot_len = length;
            p->ref = 1;
            p->next = nullptr;
            if (g_mock_lwip.force_chained_pbuf) {
                p->next = &g_mock_lwip.chained_extra;
            }
            return p;
        }
    }
    return nullptr; // Pool exhausted
}

void pbuf_realloc(struct pbuf* p, uint16_t size) {
    assert(p != nullptr);
    assert(size <= p->tot_len);
    p->len = size;
    p->tot_len = size;
}

uint8_t pbuf_free(struct pbuf* p) {
    if (!p) return 0;
    for (size_t i = 0; i < MockPbufArena::kPoolSize; ++i) {
        if (p == &g_mock_lwip.pbufs[i]) {
            assert(g_mock_lwip.in_use[i] && "Double free of pbuf detected");
            g_mock_lwip.in_use[i] = false;
            return 1;
        }
    }
    return 0;
}

uint8_t udp_sendto(struct udp_pcb* /*pcb*/, struct pbuf* p, const ip_addr_t* /*dst_ip*/, uint16_t dst_port) {
    assert(p != nullptr);
    g_mock_lwip.sent_packets.push_back({p->payload, p->len, dst_port});
    return 0;
}

} // extern "C"

namespace {

using namespace abstractx;
using namespace abstractx::hal;
using namespace abstractx::trace;

// 1. Verify Pointer Identity: CTF packet constructed directly in pbuf payload
void test_pico_trace_pointer_identity() {
    g_mock_lwip.reset();
    udp_pcb dummy_pcb{};
    ip_addr_t dest_ip{};
    PicoUdpTraceTransport transport;
    transport.init(&dummy_pcb, dest_ip, 9870);

    CtfTraceEngine<1024, 0> engine;
    engine.set_transport(&transport);

    assert(transport.acquires() == 0);
    assert(transport.commits() == 0);

    // Emitting an IMU event must borrow a pbuf directly from PBUF_POOL
    engine.trace_imu(42, 100, -200, 980, 10, 20, -30, 2550, 1000);
    assert(transport.acquires() == 1);
    assert(transport.current_pbuf() != nullptr);
    void* expected_payload = transport.current_pbuf()->payload;

    engine.flush(2000);
    assert(transport.commits() == 1);
    assert(g_mock_lwip.sent_packets.size() == 1);

    // Verify pointer identity: the pointer passed to udp_sendto must match the acquired pbuf payload exactly!
    assert(g_mock_lwip.sent_packets[0].payload_ptr == expected_payload && "Pointer identity invariant violated!");
    assert(g_mock_lwip.sent_packets[0].dst_port == 9870);

    // Verify CTF Packet Header at payload location
    CtfPacketHeaderWire hdr{};
    std::memcpy(&hdr, expected_payload, sizeof(hdr));
    assert(hdr.magic == CTF_MAGIC);
    assert(hdr.timestamp_begin_us == 1000);
    assert(hdr.timestamp_end_us == 2000);
    std::cout << "  -> verified pointer identity: trace engine wrote directly into pbuf payload (0 memcpy)\n";
}

// 2. Verify Chained PBUF Rejection
void test_chained_pbuf_rejected() {
    g_mock_lwip.reset();
    g_mock_lwip.force_chained_pbuf = true;

    udp_pcb dummy_pcb{};
    ip_addr_t dest_ip{};
    PicoUdpTraceTransport transport;
    transport.init(&dummy_pcb, dest_ip, 9870);

    CtfTraceEngine<1024, 0> engine;
    engine.set_transport(&transport);

    // Should reject non-contiguous chained pbuf to guarantee single-datagram invariant
    engine.trace_imu(1, 0, 0, 0, 0, 0, 0, 0, 100);
    assert(transport.chain_rejected() == 1);
    assert(engine.get_discarded_count() == 1);
    assert(g_mock_lwip.sent_packets.empty());
    std::cout << "  -> verified non-contiguous chained pbuf rejected and safely discarded\n";
}

// 3. Verify PBUF Pool Exhaustion Handled Gracefully Without Blocking
void test_pbuf_pool_exhaustion() {
    g_mock_lwip.reset();
    g_mock_lwip.force_alloc_fail = true;

    udp_pcb dummy_pcb{};
    ip_addr_t dest_ip{};
    PicoUdpTraceTransport transport;
    transport.init(&dummy_pcb, dest_ip, 9870);

    CtfTraceEngine<1024, 0> engine;
    engine.set_transport(&transport);

    // Pool empty: events are counted as discarded, never blocks
    engine.trace_imu(1, 0, 0, 0, 0, 0, 0, 0, 100);
    engine.trace_imu(2, 0, 0, 0, 0, 0, 0, 0, 200);
    assert(transport.alloc_failed() == 2);
    assert(engine.get_discarded_count() == 2);
    assert(transport.commits() == 0);
    std::cout << "  -> verified pool exhaustion drops events without blocking\n";
}

// 4. Verify Steady-State Repeated Trace Cycles Do Not Leak PBUFs
void test_steady_state_pbuf_lifecycle() {
    g_mock_lwip.reset();
    udp_pcb dummy_pcb{};
    ip_addr_t dest_ip{};
    PicoUdpTraceTransport transport;
    transport.init(&dummy_pcb, dest_ip, 9870);

    CtfTraceEngine<1024, 0> engine;
    engine.set_transport(&transport);

    for (uint32_t i = 0; i < 1000; ++i) {
        engine.trace_imu(static_cast<uint16_t>(i), 1, 2, 3, 4, 5, 6, 2500, i * 10);
        engine.flush(i * 10 + 5);
    }

    assert(transport.commits() == 1000);
    assert(g_mock_lwip.sent_packets.size() == 1000);
    // All pbufs must be returned to the pool after commit
    for (size_t i = 0; i < MockPbufArena::kPoolSize; ++i) {
        assert(!g_mock_lwip.in_use[i] && "PBUF leak detected in steady-state loop!");
    }
    std::cout << "  -> verified 1,000 steady-state acquire/commit cycles with 0 leaked pbufs\n";
}

} // namespace

int main() {
    std::cout << "[TEST] Starting Pico 2 W Zero-Copy lwIP PBUF_POOL Trace Transport [SPEC-PICO-TRACE-01]...\n";
    test_pico_trace_pointer_identity();
    test_chained_pbuf_rejected();
    test_pbuf_pool_exhaustion();
    test_steady_state_pbuf_lifecycle();
    std::cout << "[PASS] Pico 2 W Zero-Copy lwIP PBUF_POOL Trace Transport\n";
    return 0;
}
