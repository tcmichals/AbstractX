/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: E906 zero-copy trace over RPMsg [SPEC-E906-IPC-04] [SPEC-TRACE-07]
 * Runs the real targets/allwinner_e906/src/rpmsg.cpp against vrings laid out
 * by this test the way Linux virtio_rpmsg_bus does (independent layout code,
 * so a ring-layout mismatch fails here). Memory is mapped below 4 GB because
 * resource-table vring addresses are 32-bit.
 */

#include "hal/rpmsg_trace_transport.hpp"
#include "resource_table.h"

#include <sys/mman.h>
#include <cassert>
#include <cstring>
#include <iostream>

namespace hal { extern int g_fake_msgbox_sends[4]; }

namespace {

constexpr uint32_t kNum = 16;
constexpr uint32_t kAlign = 4096;
constexpr uint32_t kBufSize = 512;
constexpr uint16_t VRING_DESC_F_WRITE = 2;

struct LinuxVring { // Linux view, independent of rpmsg.cpp's structs
    uint8_t* base;
    uint8_t* desc(uint32_t i) const { return base + 16 * i; }
    uint8_t* avail() const { return base + 16 * kNum; }
    uint8_t* used() const {
        const size_t off = 16 * kNum + 6 + 2 * kNum;
        return base + ((off + kAlign - 1) & ~size_t(kAlign - 1));
    }
    void set_desc(uint32_t i, uint64_t addr, uint32_t len, uint16_t flags) {
        std::memcpy(desc(i), &addr, 8);
        std::memcpy(desc(i) + 8, &len, 4);
        std::memcpy(desc(i) + 12, &flags, 2);
    }
    uint16_t avail_idx() const { uint16_t v; std::memcpy(&v, avail() + 2, 2); return v; }
    void post(uint16_t desc_idx) {
        const uint16_t idx = avail_idx();
        std::memcpy(avail() + 4 + 2 * (idx % kNum), &desc_idx, 2);
        const uint16_t next = idx + 1;
        std::memcpy(avail() + 2, &next, 2);
    }
    uint16_t used_idx() const { uint16_t v; std::memcpy(&v, used() + 2, 2); return v; }
    void used_elem(uint16_t slot, uint32_t& id, uint32_t& len) const {
        std::memcpy(&id, used() + 4 + 8 * (slot % kNum), 4);
        std::memcpy(&len, used() + 8 + 8 * (slot % kNum), 4);
    }
};

struct Hdr { uint32_t src, dst, reserved; uint16_t len, flags; } __attribute__((packed));

uint8_t* map_low(size_t bytes) {
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    assert(p != MAP_FAILED && reinterpret_cast<uintptr_t>(p) < 0xFFFFFFFFu);
    std::memset(p, 0, bytes);
    return static_cast<uint8_t*>(p);
}

} // namespace

int main() {
    std::cout << "[TEST] E906 zero-copy trace over RPMsg [SPEC-E906-IPC-04]\n";

    uint8_t* mem = map_low(64 * 1024);
    auto* rsc = reinterpret_cast<rpmsg_resource_table*>(mem);
    LinuxVring tx{mem + 0x1000};  // vring[0]: remote -> host
    LinuxVring rx{mem + 0x4000};  // vring[1]: host -> remote
    uint8_t* bufs = mem + 0x8000; // 2 * kNum buffers of kBufSize
    auto buf = [&](uint32_t i) { return bufs + i * kBufSize; };
    auto addr = [](const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); };

    // Linux brings the vdev up and pre-posts empty buffers for the remote.
    rsc->vdev.vring[0] = {addr(tx.base), kAlign, kNum, 0, 0};
    rsc->vdev.vring[1] = {addr(rx.base), kAlign, kNum, 1, 0};
    for (uint32_t i = 0; i < kNum; ++i) {
        tx.set_desc(i, addr(buf(i)), kBufSize, VRING_DESC_F_WRITE);
        tx.post(static_cast<uint16_t>(i));
    }
    rsc->vdev.status = VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER | VIRTIO_CONFIG_S_DRIVER_OK;

    hal::Rpmsg rpmsg;
    rpmsg.init(rsc);
    hal::RpmsgTraceTransport transport(rpmsg);
    transport.attach();
    abstractx::trace::CtfTraceEngine<1024, 2> engine;
    engine.set_transport(&transport);

    // 1. Name-service announcement goes out in the first host buffer.
    transport.service();
    assert(tx.used_idx() == 1);
    uint32_t id, len;
    tx.used_elem(0, id, len);
    Hdr h{};
    std::memcpy(&h, buf(id), sizeof(h));
    assert(id == 0 && h.dst == RPMSG_NS_ADDR && h.src == hal::RpmsgTraceTransport::kEndpoint);
    assert(std::strcmp(reinterpret_cast<const char*>(buf(id) + sizeof(Hdr)), "abstractx-trace") == 0);
    assert(len == sizeof(Hdr) + 40);
    assert(hal::g_fake_msgbox_sends[1] == 1 && "kick must go to Channel1 (Linux ch 9)");
    std::cout << "  -> name service 'abstractx-trace' announced in host buffer 0, kicked on Channel1\n";

    // 2. Before the host says hello its address is unknown: trace is dropped, not blocked.
    engine.trace_imu(1, 0, 0, 1000, 0, 0, 0, 0, 10);
    assert(engine.get_discarded_count() == 1 && tx.used_idx() == 1);

    // 3. Host sends its first message (rpmsg_char endpoint at 0x400 -> 1025).
    Hdr hello{0x400, hal::RpmsgTraceTransport::kEndpoint, 0, 4, 0};
    std::memcpy(buf(kNum), &hello, sizeof(hello));
    std::memcpy(buf(kNum) + sizeof(hello), "helo", 4);
    rx.set_desc(0, addr(buf(kNum)), sizeof(hello) + 4, 0);
    rx.post(0);
    transport.service();
    assert(transport.host_connected());
    assert(rx.used_idx() == 1 && "host buffer must be returned");
    std::cout << "  -> host hello consumed and returned; host address learned\n";

    // 4. Trace packet is built directly in host buffer 1 and published.
    engine.trace_imu(2, 1, 2, 3, 4, 5, 6, 2500, 20);
    engine.trace_gps(100, 1, 2, 3, 4, 5, 9, 3, 21);
    engine.flush(30);
    assert(tx.used_idx() == 2);
    tx.used_elem(1, id, len);
    std::memcpy(&h, buf(id), sizeof(h));
    assert(id == 1 && h.src == hal::RpmsgTraceTransport::kEndpoint && h.dst == 0x400);
    const size_t ctf_len = sizeof(abstractx::trace::CtfPacketHeader) +
                           sizeof(abstractx::trace::ImuSamplePayload) + sizeof(abstractx::trace::GpsFixPayload);
    assert(h.len == ctf_len && len == sizeof(Hdr) + ctf_len);
    uint32_t magic;
    std::memcpy(&magic, buf(id) + sizeof(Hdr), 4);
    assert(magic == abstractx::trace::CTF_MAGIC && "CTF packet written in place in the host buffer");
    std::cout << "  -> CTF packet (" << ctf_len << " B) written in place in host buffer 1, dst=0x400\n";

    // 5. All host buffers used: further packets are dropped and counted.
    for (int i = 0; i < 20; ++i) {
        engine.trace_hal(1, 4, 4, 1, 0, 40 + i);
        engine.flush(41 + i);
    }
    assert(tx.used_idx() == kNum && "never uses more buffers than Linux posted");
    const size_t dropped = engine.get_discarded_count();
    assert(dropped > 1);

    // 6. Linux recycles one buffer: tracing resumes, drop count reported.
    tx.post(2);
    engine.trace_hal(1, 4, 4, 1, 0, 100);
    engine.flush(101);
    assert(tx.used_idx() == kNum + 1);
    tx.used_elem(kNum, id, len);
    abstractx::trace::CtfPacketHeader ph{};
    std::memcpy(&ph, buf(id) + sizeof(Hdr), sizeof(ph));
    assert(id == 2 && ph.events_discarded == dropped);
    std::cout << "  -> pool exhausted: " << dropped << " events dropped without blocking; resumed after recycle\n";

    std::cout << "[PASS] E906 zero-copy trace over RPMsg\n";
    return 0;
}
