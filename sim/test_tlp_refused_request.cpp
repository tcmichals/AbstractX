// @verifies [SPEC-ARCH-09] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * [SPEC-ARCH-09] a refused TLP request is an error, never stale data (review 2026-11-04 Q-2, Q-12).
 * 255 good reads use every tag once (each completion carries its tag in the data); then the TX ring
 * is filled, and the next read must return the QUEUE_FULL error completion, not the stored response
 * of the earlier read on the same tag.
 */
#undef NDEBUG
#include "abstractx/coro.hpp"

#include <cassert>
#include <cstdio>
#include <set>

using namespace abstractx;
using namespace abstractx::coro;

namespace {
SpscTlpRing<> g_tx, g_rx;
CoroutineIoEngine g_eng{g_tx, g_rx};
int g_reads = 0;
Task<void> reader(Tlp64* out) {
    *out = co_await g_eng.async_read(0x100);
    ++g_reads;
}
} // namespace

int main() {
    std::set<uint8_t> tags;
    for (int i = 0; i < 255; ++i) {
        Tlp64 out{};
        auto t = reader(&out);
        t.resume();  // resume: starts the never-run task; it sends a MemRd
        Tlp64 sent{};
        assert(g_tx.pop(sent));
        tags.insert(sent.wire.tag);
        assert(g_rx.push(Tlp64::make_completion_data(sent.wire.tag, 0xA5000000u | sent.wire.tag, Channel::Control)));
        g_eng.poll_completions();
        Dispatcher::process();
        assert(t.done() && out.wire.flags == 0);
    }
    std::printf("[+] 255 good reads used %zu distinct tags (1..255)\n", tags.size());
    assert(tags.size() == 255 && tags.count(0) == 0 && "tags use the whole range 1..255");

    Tlp64 dummy{};
    while (g_tx.push(dummy)) {}  // TX ring full: the next request cannot be sent
    Tlp64 out{};
    auto t = reader(&out);
    t.resume();  // resume: starts the never-run task; its submit is refused, it does not suspend
    assert(t.done() && "a refused request completes at once");
    std::printf("[+] refused read: type %u flags %u tag %u status 0x%02x\n", out.wire.type, out.wire.flags,
                out.wire.tag, out.wire.payload[0]);
    assert(out.wire.flags == 1 && out.wire.payload[0] == ASP_STATUS_QUEUE_FULL && out.wire.tag == 0 &&
           "a refused request returns the QUEUE_FULL error, not an earlier response");
    std::puts("[SUCCESS] refused TLP request is an error, never stale data [SPEC-ARCH-09]");
    return 0;
}
