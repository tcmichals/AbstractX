// @verifies [SPEC-ARCH-13] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * [SPEC-ARCH-13] busy covers the whole auto transfer (review 2026-11-04 Q-3). The back end completes
 * on a worker thread (as LinuxIoBackend does via SpiWorker) while the main thread fires on_trigger
 * like poll_events. Under TSan the old code reports a data race on the channel snapshot, timestamp and
 * command length; every emitted packet's timestamp must be one of the edges, and every edge is either
 * emitted or counted as an overrun.
 */
#undef NDEBUG
#include <cassert>
#include "abstractx/hal/io_engine.hpp"
#include <chrono>
#include <thread>
#include <cstdio>
using namespace abstractx; using namespace abstractx::hal;
struct Be : IIoBackend {
    std::atomic<bool> has{false}; IoTransfer x{};
    bool init_channel(const AutoChannelConfig&) noexcept override { return true; }
    bool start_transfer(const IoTransfer& t) noexcept override {
        if (has.load(std::memory_order_acquire)) return false;
        x = t; has.store(true, std::memory_order_release); return true; }
};
MpscTlpRing<> eg, in;
int main(){
    Be be; IoEngine e(be);
    AutoChannelConfig ch{}; ch.trigger_pin=5; ch.tx_len=1; ch.rx_len=14;
    IoProcessorSetup s{}; s.channels = std::span<const AutoChannelConfig>(&ch,1); s.egress_tx_ring=&eg; s.ingress_rx_ring=&in;
    e.configure(s); e.start();
    std::atomic<bool> stop{false};
    std::thread w([&]{ while(!stop.load()){ if(be.has.load(std::memory_order_acquire)){ IoTransfer t=be.x; be.has.store(false,std::memory_order_release);
        IoResult r{}; r.ok=true; r.transferred=t.rx.size(); t.done(r);} } });
    uint32_t ts=0; size_t got=0; Tlp64 tlp;
    for (int i=0;i<200000;++i){ { InterruptGuard g; e.on_trigger(5, ++ts); }
        while(in.pop(tlp)) { ++got; assert(tlp.wire.timestamp_ns >= 1 && tlp.wire.timestamp_ns <= ts); } }
    while (be.has.load(std::memory_order_acquire)) std::this_thread::yield();  // last transfer completes
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    stop=true; w.join();
    while (in.pop(tlp)) ++got;
    std::printf("[+] 200000 edges: emitted %zu, overruns %u, failed %u, ingress dropped %u\n", got,
                e.auto_overruns(), e.auto_failed(), e.ingress_dropped());
    assert(got > 0 && got + e.auto_overruns() + e.auto_failed() + e.ingress_dropped() == 200000u &&
           "every edge is emitted or counted with a reason");
    std::puts("[SUCCESS] auto channel trigger vs completion [SPEC-ARCH-13]");
}
