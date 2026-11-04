// @verifies [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * [SPEC-ARCH-16] requirements 4 and 9 (2026-11-04):
 * 1. an auto channel numbers its packets: 100 edges give sequence 0..99 (before: every packet 0, so a
 *    receiver could not see a gap)
 * 2. a receiving CoroutineIoEngine drops stream packets for a channel nobody waits on and counts
 *    them per channel (before: ignored silently); a waiting consumer still gets its packet
 */
#undef NDEBUG
#include "abstractx/hal/io_engine.hpp"
#include "abstractx/coro.hpp"

#include <cassert>
#include <cstdio>

using namespace abstractx;
using namespace abstractx::coro;
using namespace abstractx::hal;

namespace {
struct InlineBackend : IIoBackend {  // completes every transfer at once (like a fast bus)
    bool init_channel(const AutoChannelConfig&) noexcept override { return true; }
    bool start_transfer(const IoTransfer& t) noexcept override {
        IoResult r{};
        r.ok = true;
        r.transferred = t.rx.size();
        t.done(r);
        return true;
    }
};

MpscTlpRing<> g_egress, g_ingress;
SpscTlpRing<> g_tx, g_rx;
CoroutineIoEngine g_eng{g_tx, g_rx};
int g_got = 0;
Task<void> wait_stream() {
    (void)co_await g_eng.async_await_stream(Channel::Telemetry);
    ++g_got;
}

Tlp64 stream_packet(uint16_t seq) {
    Tlp64 t = Tlp64::make_stream_tx(Channel::Telemetry, seq, {});
    t.wire.type = static_cast<uint8_t>(TlpType::DmaStream);
    return t;
}
} // namespace

int main() {
    {   // 1
        InlineBackend be;
        IoEngine e(be);
        AutoChannelConfig ch{};
        ch.trigger_pin = 5;
        ch.tx_len = 1;
        ch.rx_len = 14;
        IoProcessorSetup s{};
        s.channels = std::span<const AutoChannelConfig>(&ch, 1);
        s.egress_tx_ring = &g_egress;
        s.ingress_rx_ring = &g_ingress;
        e.configure(s);
        e.start();
        Tlp64 t{};
        uint16_t expect = 0;
        int n = 0;
        for (uint32_t i = 1; i <= 100; ++i) {
            e.on_trigger(5, i);
            while (g_ingress.pop(t)) {  // drain each time: the ring is smaller than 100
                assert(t.wire.sequence == expect && "packets are numbered per channel, no gaps");
                ++expect;
                ++n;
            }
        }
        std::printf("[+] 1 100 edges: %d packets, sequence 0..%u (overruns %u, dropped %u)\n", n, expect - 1u,
                    e.auto_overruns(), e.ingress_dropped());
        assert(n == 100);
    }
    {   // 2
        for (uint16_t i = 0; i < 3; ++i) assert(g_rx.push(stream_packet(i)));
        g_eng.poll_completions();
        const auto d = g_eng.stream_drops(static_cast<uint8_t>(Channel::Telemetry));
        std::printf("[+] 2 3 packets with nobody waiting: no_consumer %u\n", d.no_consumer);
        assert(d.no_consumer == 3 && "unmapped data is dropped and counted, never silent");
        auto w = wait_stream();
        w.resume();  // resume: starts the never-run task; it waits on the channel
        assert(g_rx.push(stream_packet(3)));
        g_eng.poll_completions();
        while (Dispatcher::has_pending_resumes()) Dispatcher::process();
        assert(g_got == 1 && g_eng.stream_drops(static_cast<uint8_t>(Channel::Telemetry)).no_consumer == 3);
        std::puts("[+] 2 a waiting consumer gets its packet; the count is unchanged");
    }
    std::puts("[SUCCESS] stream sequence numbers and per-channel drops [SPEC-ARCH-16]");
    return 0;
}
