// @verifies [SPEC-E906-IPC-04] [SPEC-TRACE-07]
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: E906 RPMsg endpoints [SPEC-E906-IPC-04] [SPEC-TRACE-07]
 * abstractx-tlp (1024) and abstractx-trace (1025) over one virtio device.
 * Runs the real targets/allwinner_e906/src/rpmsg.cpp + rpmsg_endpoints.cpp
 * against vrings laid out by this test the way Linux virtio_rpmsg_bus does
 * (independent layout code) and a fake message box. Memory is mapped below
 * 4 GB because resource-table vring addresses are 32-bit.
 */

#include "hal/rpmsg_endpoints.hpp"
#include "resource_table.h"

#include <sys/mman.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace hal { extern int g_fake_msgbox_sends[4]; }

namespace {

constexpr uint32_t kNum = 16;
constexpr uint32_t kAlign = 4096;
constexpr uint32_t kBufSize = 512;
constexpr uint16_t VRING_DESC_F_WRITE = 2;

struct LinuxVring { // Linux's view, independent of rpmsg.cpp's structs
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

struct Rig {
    uint8_t* mem = map_low(64 * 1024);
    rpmsg_resource_table* rsc = reinterpret_cast<rpmsg_resource_table*>(mem);
    LinuxVring tx{mem + 0x1000}; // vring[0]: remote -> host
    LinuxVring rx{mem + 0x4000}; // vring[1]: host -> remote
    uint8_t* bufs = mem + 0x8000;
    uint32_t rx_next = 0;

    uint8_t* buf(uint32_t i) const { return bufs + i * kBufSize; }
    static uint32_t addr(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

    Rig() {
        rsc->vdev.vring[0] = {addr(tx.base), kAlign, kNum, 0, 0};
        rsc->vdev.vring[1] = {addr(rx.base), kAlign, kNum, 1, 0};
        for (uint32_t i = 0; i < kNum; ++i) {
            tx.set_desc(i, addr(buf(i)), kBufSize, VRING_DESC_F_WRITE);
            tx.post(static_cast<uint16_t>(i));
        }
        rsc->vdev.status = VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER | VIRTIO_CONFIG_S_DRIVER_OK;
    }

    // Linux sends a message to an E906 endpoint (host buffers kNum.. for RX).
    void host_send(uint32_t src, uint32_t dst, const void* data, uint16_t len) {
        uint8_t* b = buf(kNum + rx_next);
        Hdr h{src, dst, 0, len, 0};
        std::memcpy(b, &h, sizeof(h));
        std::memcpy(b + sizeof(h), data, len);
        rx.set_desc(rx_next, addr(b), sizeof(h) + len, 0);
        rx.post(static_cast<uint16_t>(rx_next));
        ++rx_next;
    }

    Hdr sent_hdr(uint16_t used_slot, uint32_t& id, uint32_t& len) const {
        tx.used_elem(used_slot, id, len);
        Hdr h{};
        std::memcpy(&h, buf(id), sizeof(h));
        return h;
    }
};

abstractx::Tlp64 make_tlp(uint8_t tag) {
    abstractx::Tlp64 t{};
    t.wire.tag = tag;
    t.wire.payload[0] = static_cast<uint8_t>(0xA0 + tag);
    return t;
}

} // namespace

int main() {
    std::cout << "[TEST] E906 RPMsg endpoints: abstractx-tlp + abstractx-trace [SPEC-E906-IPC-04]\n";
    Rig rig;
    hal::Rpmsg rpmsg;
    rpmsg.init(rig.rsc);
    hal::RpmsgTlpChannel tlp(rpmsg);
    hal::RpmsgTraceTransport trace(rpmsg);
    tlp.attach();
    trace.attach();
    abstractx::trace::CtfTraceEngine<1024, 0> engine;
    engine.set_transport(&trace);
    abstractx::SpscTlpRing<> ingress;
    tlp.set_ingress_ring(&ingress);
    auto loop = [&] { tlp.service(); trace.service(); (void)rpmsg.poll(); tlp.flush(); };

    uint32_t id, len;
    // 1. Both services announced, one host buffer each.
    loop();
    assert(rig.tx.used_idx() == 2);
    Hdr h = rig.sent_hdr(0, id, len);
    assert(h.dst == RPMSG_NS_ADDR && std::strcmp(reinterpret_cast<char*>(rig.buf(id) + sizeof(Hdr)), "abstractx-tlp") == 0);
    h = rig.sent_hdr(1, id, len);
    assert(h.dst == RPMSG_NS_ADDR && std::strcmp(reinterpret_cast<char*>(rig.buf(id) + sizeof(Hdr)), "abstractx-trace") == 0);
    std::cout << "  -> 'abstractx-tlp' and 'abstractx-trace' announced\n";

    // 2. Nothing can be sent before each Linux reader says hello.
    assert(!tlp.send(make_tlp(1)) && tlp.tx_dropped() == 1);
    rig.host_send(0x401, hal::RpmsgTlpChannel::kEndpoint, "helo", 4);
    rig.host_send(0x400, hal::RpmsgTraceTransport::kEndpoint, "helo", 4);
    loop();
    assert(tlp.host_connected() && trace.host_connected() && tlp.rx_rejected() == 0);
    std::cout << "  -> both readers connected (hello learned host addresses)\n";

    // 3. A held trace packet never blocks TLPs; completions may be out of order.
    engine.trace_imu(1, 1, 2, 3, 4, 5, 6, 2500, 10); // trace borrows buffer 2, keeps it
    for (uint8_t i = 0; i < 9; ++i) {
        assert(tlp.send(make_tlp(i)));
    }
    tlp.flush();
    assert(rig.tx.used_idx() == 4 && "two TLP batches published while trace holds its buffer");
    h = rig.sent_hdr(2, id, len);
    assert(id == 3 && h.dst == 0x401 && h.src == hal::RpmsgTlpChannel::kEndpoint && h.len == 7 * 64);
    asp_tlp64_t first{}; // test-side parse via memcpy (aligned local)
    std::memcpy(&first, rig.buf(id) + sizeof(Hdr), sizeof(first));
    assert(first.tag == 0 && first.payload[0] == 0xA0);
    h = rig.sent_hdr(3, id, len);
    assert(id == 4 && h.len == 2 * 64);
    engine.flush(20);
    h = rig.sent_hdr(4, id, len);
    assert(id == 2 && h.dst == 0x400 && h.src == hal::RpmsgTraceTransport::kEndpoint);
    uint32_t magic;
    std::memcpy(&magic, rig.buf(id) + sizeof(Hdr), 4);
    assert(magic == abstractx::trace::CTF_MAGIC);
    std::cout << "  -> 9 TLPs sent as batches of 7 + 2 while trace held a buffer; trace committed after (out of order)\n";

    // 4. Trace stops at the TLP reserve; TLPs still get buffers.
    const size_t dropped_before = engine.get_discarded_count();
    for (int i = 0; i < 20; ++i) {
        engine.trace_hal(1, 4, 4, 1, 0, 30 + i);
        engine.flush(31 + i);
    }
    assert(rpmsg.tx_free_buffers() == hal::kTlpReserve && "trace must leave the TLP reserve");
    assert(engine.get_discarded_count() > dropped_before);
    assert(tlp.send(make_tlp(42)));
    tlp.flush();
    assert(rpmsg.tx_free_buffers() == hal::kTlpReserve - 1);
    std::cout << "  -> trace held back at " << hal::kTlpReserve << " free buffers; TLP still sent from the reserve\n";

    // 5. Host -> E906 TLPs land in the ingress ring; malformed batches rejected.
    asp_tlp64_t in[2]{};
    in[0].tag = 7;
    in[1].tag = 8;
    rig.host_send(0x401, hal::RpmsgTlpChannel::kEndpoint, in, sizeof(in));
    uint8_t junk[100]{};
    rig.host_send(0x401, hal::RpmsgTlpChannel::kEndpoint, junk, sizeof(junk));
    loop();
    abstractx::Tlp64 got{};
    assert(ingress.pop(got) && got.wire.tag == 7);
    assert(ingress.pop(got) && got.wire.tag == 8);
    assert(!ingress.pop(got) && tlp.rx_rejected() == 1);
    std::cout << "  -> host batch of 2 TLPs delivered to the ingress ring; 100-byte message rejected\n";

    // 6. Right-sized E906 TLP ring (capacity 8) fits maximum 7-TLP RPMsg batch [SPEC-E906-MEM-01] [SPEC-E906-IPC-04]
    // @verifies [SPEC-E906-MEM-01] [SPEC-E906-IPC-04]
    static_assert(sizeof(abstractx::SpscRingBuffer<abstractx::Tlp64, 8>) == 640,
                  "Right-sized SpscTlpRing<8> must be exactly 640 bytes");
    abstractx::SpscRingBuffer<abstractx::Tlp64, 8> e906_ring{};
    for (uint8_t i = 0; i < 7; ++i) {
        assert(e906_ring.push(make_tlp(static_cast<uint8_t>(i + 10))));
    }
    assert(e906_ring.size() == 7);
    for (uint8_t i = 0; i < 7; ++i) {
        abstractx::Tlp64 t{};
        assert(e906_ring.pop(t) && t.wire.tag == (i + 10));
    }
    std::cout << "  -> 7-TLP RPMsg max batch verified on right-sized 640-byte ring (capacity 8)\n";

    assert(hal::g_fake_msgbox_sends[0] == 0 && hal::g_fake_msgbox_sends[1] > 0 && "all kicks on Channel1");
    std::cout << "[PASS] E906 RPMsg endpoints\n";
    return 0;
}
