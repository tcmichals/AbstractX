/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: CoroutineIoEngine completions go through the Dispatcher [SPEC-ARCH-10]
 * poll_completions() matches a completion to its awaiter and posts it; the awaiter runs on the
 * next Dispatcher::process(), never inside poll_completions() (2026-10-29: it resumed inline).
 * Also: a second stream waiter on one channel is refused, not overwritten; 1000 rounds flat.
 */
// @verifies [SPEC-ARCH-10] docs/DESIGN_SPECIFICATION.md
#undef NDEBUG

#include "asp_coro.hpp"

#include <cassert>
#include <cstdio>

using abstractx::Dispatcher;
using abstractx::coro::CoroutineIoEngine;
using abstractx::coro::Task;
using abstractx::coro::coro_pool_high_water;
using abstractx::Channel;
using abstractx::SpscTlpRing;
using abstractx::Tlp64;

namespace {

void pump() {
    while (Dispatcher::has_pending_resumes()) Dispatcher::process();
}

Task<void> reader(CoroutineIoEngine& io, uint32_t& out, int& resumes) {
    Tlp64 r = co_await io.async_read(0x40000100u);
    ++resumes;
    out = r.wire.payload[3];
}

Task<void> streamer(CoroutineIoEngine& io, Tlp64& out, int& resumes) {
    out = co_await io.async_await_stream(Channel::Telemetry);
    ++resumes;
}

// Answer the request the engine queued, the way an I/O processor would.
bool answer(SpscTlpRing<>& tx, SpscTlpRing<>& rx, uint8_t value) {
    auto req = tx.pop();
    if (!req) return false;
    Tlp64 cpl = Tlp64::make_cpl_d(req->wire.tag, value);
    return rx.push(cpl);
}

} // namespace

int main() {
    SpscTlpRing<> tx, rx;
    CoroutineIoEngine io(tx, rx);

    // 1. Completion is posted: not resumed inside poll_completions().
    {
        uint32_t v = 0;
        int resumes = 0;
        auto t = reader(io, v, resumes);
        t.resume();
        assert(!t.done() && answer(tx, rx, 0x5A));
        assert(io.poll_completions() == 1);
        assert(resumes == 0 && !t.done());        // old code: resumed here
        pump();
        assert(resumes == 1 && t.done() && v == 0x5A);
    }

    // 2. Two stream waiters on one channel: the second is refused at once (empty TLP), the first
    //    still gets the packet.
    {
        Tlp64 a{}, b{};
        int ra = 0, rb = 0;
        auto ta = streamer(io, a, ra);
        auto tb = streamer(io, b, rb);
        ta.resume();
        tb.resume();
        assert(!ta.done() && tb.done() && rb == 1);
        Tlp64 s = Tlp64::make_cpl_d(0, 0x77);
        s.wire.type = static_cast<uint8_t>(abstractx::TlpType::DmaStream);
        s.wire.channel = static_cast<uint8_t>(Channel::Telemetry);
        assert(rx.push(s));
        io.poll_completions();
        assert(ra == 0);
        pump();
        assert(ra == 1 && ta.done() && a.wire.payload[3] == 0x77);
    }

    // 3. Steady state: 1000 read round trips, pool and Dispatcher flat.
    {
        uint32_t v = 0;
        int resumes = 0;
        { auto w = reader(io, v, resumes); w.resume(); answer(tx, rx, 1); io.poll_completions(); pump(); }
        const size_t hw0 = coro_pool_high_water();
        const uint32_t dq0 = Dispatcher::dropped_resumes();
        for (int i = 0; i < 1000; ++i) {
            resumes = 0;
            auto t = reader(io, v, resumes);
            t.resume();
            assert(answer(tx, rx, static_cast<uint8_t>(i)));
            io.poll_completions();
            pump();
            assert(t.done() && resumes == 1 && v == static_cast<uint8_t>(i));
        }
        assert(coro_pool_high_water() == hw0 && Dispatcher::dropped_resumes() == dq0);
        assert(Dispatcher::queue_depth() == 0);
    }

    std::printf("[PASS] CoroutineIoEngine: completions posted, second stream waiter refused, 1000 rounds flat\n");
    return 0;
}
