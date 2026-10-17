/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: zero-copy trace transport [SPEC-TRACE-07] [SPEC-ARCH-08]
 * The engine must write each CTF packet directly into the buffer the transport
 * lends it and hand back that same memory: checked by pointer identity.
 */

#include "abstractx/trace/tracer.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>

using namespace abstractx::trace;

namespace {

// Lends slices of a fixed arena and records every acquire/commit.
class RecordingTransport final : public ITraceTransport {
public:
    static constexpr size_t kSlots = 4;
    static constexpr size_t kSlotBytes = 256;

    explicit RecordingTransport(size_t misalign = 0) : misalign_(misalign) {}

    std::span<uint8_t> acquire(size_t max_bytes) noexcept override {
        ++acquires;
        if (fail_acquire) {
            return {};
        }
        uint8_t* slot = arena_ + misalign_ + (next_++ % kSlots) * kSlotBytes;
        last_acquired = slot;
        return {slot, max_bytes < kSlotBytes ? max_bytes : kSlotBytes};
    }

    void commit(std::span<uint8_t> buffer, size_t used_bytes) noexcept override {
        committed_ptr[commits] = buffer.data();
        committed_len[commits] = used_bytes;
        ++commits;
    }

    bool     fail_acquire{false};
    int      acquires{0};
    int      commits{0};
    uint8_t* last_acquired{nullptr};
    uint8_t* committed_ptr[16]{};
    size_t   committed_len[16]{};

private:
    alignas(16) uint8_t arena_[kSlots * kSlotBytes + 16]{};
    size_t misalign_;
    size_t next_{0};
};

CtfPacketHeader read_header(const uint8_t* p) {
    CtfPacketHeader h{};
    std::memcpy(&h, p, sizeof(h)); // test-side parse of possibly unaligned bytes
    return h;
}

constexpr size_t kHdr = sizeof(CtfPacketHeader);

void test_packet_written_in_place() {
    CtfTraceEngine<1024, 2> eng;
    RecordingTransport t;
    eng.set_transport(&t);

    assert(t.acquires == 0 && "no buffer may be borrowed before the first event");
    eng.flush(1);
    assert(t.commits == 0 && "empty packet must not be sent");

    eng.trace_imu(7, 1, 2, 3, 4, 5, 6, 2500, 1000);
    assert(t.acquires == 1);
    eng.flush(2000);

    assert(t.commits == 1);
    assert(t.committed_ptr[0] == t.last_acquired && "commit must hand back the borrowed buffer");
    assert(t.committed_len[0] == kHdr + sizeof(ImuSamplePayload));

    const CtfPacketHeader h = read_header(t.committed_ptr[0]);
    assert(h.magic == CTF_MAGIC);
    assert(h.content_size_bits == t.committed_len[0] * 8);
    assert(h.timestamp_begin_us == 1000 && h.timestamp_end_us == 2000);

    ImuSamplePayload ev{};
    std::memcpy(&ev, t.committed_ptr[0] + kHdr, sizeof(ev));
    assert(ev.sample_seq == 7 && ev.accel_x_mg == 1 && ev.gyro_z_dps == 6 && ev.temp_c_1e2 == 2500);
    std::cout << "  -> packet built in the lent buffer and committed by pointer\n";
}

void test_rollover_when_full() {
    CtfTraceEngine<1024, 2> eng;
    RecordingTransport t; // 256-byte buffers
    eng.set_transport(&t);

    const size_t per_packet = (RecordingTransport::kSlotBytes - kHdr) / sizeof(TlpTracePayload);
    for (size_t i = 0; i < per_packet + 1; ++i) {
        eng.trace_tlp(true, static_cast<uint8_t>(i), 1, 0x1000, 16, 100 + i);
    }
    assert(t.commits == 1 && "full buffer must be committed before the next event");
    assert(t.committed_len[0] == kHdr + per_packet * sizeof(TlpTracePayload));
    assert(t.acquires == 2);
    eng.flush(999);
    assert(t.commits == 2 && t.committed_len[1] == kHdr + sizeof(TlpTracePayload));
    assert(t.committed_ptr[0] != t.committed_ptr[1]);
    std::cout << "  -> full buffer committed, next one borrowed (" << per_packet << " events/packet)\n";
}

void test_acquire_failure_discards_without_blocking() {
    CtfTraceEngine<1024, 2> eng;
    RecordingTransport t;
    eng.set_transport(&t);

    t.fail_acquire = true;
    eng.trace_hal(1, 4, 4, 10, 0, 10);
    eng.trace_hal(1, 4, 4, 10, 0, 11);
    assert(eng.get_discarded_count() == 2);
    assert(t.commits == 0);

    t.fail_acquire = false;
    eng.trace_hal(1, 4, 4, 10, 0, 12);
    eng.flush(13);
    assert(t.commits == 1);
    assert(read_header(t.committed_ptr[0]).events_discarded == 2 && "drops must be reported in the next packet");
    std::cout << "  -> no buffer: events dropped and counted, never blocked\n";
}

void test_misaligned_transport_buffer() {
    CtfTraceEngine<1024, 2> eng;
    RecordingTransport t(/*misalign=*/2); // like an lwIP UDP payload
    eng.set_transport(&t);
    eng.trace_gps(1, 2, 3, 4, 5, 6, 7, 3, 50);
    eng.flush(60);
    assert(t.commits == 1);
    assert((reinterpret_cast<uintptr_t>(t.committed_ptr[0]) & 7u) == 2u);
    assert(read_header(t.committed_ptr[0]).magic == CTF_MAGIC);
    std::cout << "  -> header and events written into a 2-byte-aligned buffer (UBSan-clean)\n";
}

void test_switching_transport_flushes_pending_packet() {
    CtfTraceEngine<1024, 2> eng;
    RecordingTransport a;
    RecordingTransport b;
    eng.set_transport(&a);
    eng.trace_coro(1, 0x10, CoroState::Spawn, 0, 5);
    eng.set_transport(&b);
    assert(a.commits == 1 && "pending packet goes to the transport that lent its buffer");
    eng.trace_coro(2, 0x20, CoroState::Resume, 0, 6);
    eng.flush(7);
    assert(b.commits == 1 && a.commits == 1);
    std::cout << "  -> switching transports commits the pending packet to its owner\n";
}

const uint8_t* g_flushed_ptr = nullptr;
size_t g_flushed_len = 0;

void test_static_pool_keeps_callback_api() {
    CtfTraceEngine<1024, 2> eng;
    eng.set_flush_handler([](const uint8_t* d, size_t n, void*) {
        g_flushed_ptr = d;
        g_flushed_len = n;
    }, nullptr);
    const size_t before = eng.get_active_fill_idx();
    eng.trace_imu(1, 0, 0, 0, 0, 0, 0, 0, 1);
    eng.flush(2);
    assert(g_flushed_ptr && g_flushed_len == kHdr + sizeof(ImuSamplePayload));
    assert(eng.get_active_fill_idx() == (before + 1) % 2 && "ping-pong swap preserved");
    std::cout << "  -> static-pool transport keeps the flush-callback API and ping-pong swap\n";
}

// @verifies [SPEC-TRACE-07]
void test_zero_copy_engine_allocates_no_static_pool() {
    // A zero-copy engine must not allocate static packet buffers in .bss
    static_assert(sizeof(CtfTraceEngine<1024, 0>) <= 64, "Zero-copy trace engine must not embed a static pool");
    static_assert(CtfTraceEngine<1024, 0>::NUM_PACKETS == 0);
    static_assert(CtfTraceEngine<1024, 0>::TOTAL_CAPACITY_BYTES == 0);
    static_assert(!CtfTraceEngine<1024, 0>::is_ping_pong());

    CtfTraceEngine<1024, 0> eng;
    assert(eng.get_active_fill_idx() == 0);
    assert(eng.get_ready_transmit_idx() == 0);

    // Without a transport, events are discarded safely without crashing
    eng.trace_imu(1, 1, 2, 3, 4, 5, 6, 2500, 100);
    assert(eng.get_discarded_count() == 1);

    // When bound to a zero-copy transport, events write in place and commit
    RecordingTransport t;
    eng.set_transport(&t);
    eng.trace_imu(2, 10, 20, 30, 40, 50, 60, 2500, 200);
    eng.flush(300);

    assert(t.commits == 1);
    assert(t.committed_ptr[0] == t.last_acquired);
    assert(t.committed_len[0] == kHdr + sizeof(ImuSamplePayload));

    const CtfPacketHeader h = read_header(t.committed_ptr[0]);
    assert(h.magic == CTF_MAGIC);
    assert(h.content_size_bits == t.committed_len[0] * 8);
    assert(h.timestamp_begin_us == 200 && h.timestamp_end_us == 300);
    std::cout << "  -> CtfTraceEngine<1024, 0> embeds zero static pool and operates zero-copy\n";
}

} // namespace

int main() {
    std::cout << "[TEST] Zero-copy trace transport [SPEC-TRACE-07]\n";
    test_packet_written_in_place();
    test_rollover_when_full();
    test_acquire_failure_discards_without_blocking();
    test_misaligned_transport_buffer();
    test_switching_transport_flushes_pending_packet();
    test_static_pool_keeps_callback_api();
    test_zero_copy_engine_allocates_no_static_pool();
    std::cout << "[PASS] zero-copy trace transport\n";
    return 0;
}
