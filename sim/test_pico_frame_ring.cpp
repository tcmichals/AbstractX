/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: Pico 2 W Core 1 -> Core 0 frame ring (two threads stand in for the two cores)
 * @verifies [SPEC-PICO-CORE-01] targets/pico2w_rp2350/SPECIFICATION.md
 */

#include "hal/frame_ring.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

using abstractx::hal::pico::FrameRing;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

// Frame i has length 1 + (i % SlotBytes) and bytes (i + k) & 0xFF, so loss, reorder and torn
// slots are all visible to the consumer.
template <size_t SlotBytes>
size_t frame_len(uint32_t i) { return 1U + (i % SlotBytes); }

void test_single_thread_basics() {
    FrameRing<64, 4> r;
    CHECK(r.peek().empty());
    uint8_t buf[64]{};
    for (uint8_t i = 0; i < 4; ++i) {
        buf[0] = i;
        CHECK(r.push(buf, 10U + i));
    }
    CHECK(!r.push(buf, 10));  // full
    CHECK(r.dropped() == 1);
    CHECK(!r.push(buf, 65));  // too big
    CHECK(r.dropped() == 2);
    for (uint8_t i = 0; i < 4; ++i) {
        auto f = r.peek();
        CHECK(f.size() == 10U + i && f[0] == i);
        r.release();
    }
    CHECK(r.peek().empty());
    r.release();  // release on empty is a no-op
    CHECK(r.size() == 0);
}

void test_acquire_is_stable_until_commit() {
    FrameRing<64, 4> r;
    auto a = r.acquire();
    auto b = r.acquire();
    CHECK(a.data() == b.data());
    CHECK(!r.commit(a, 0));            // abandon
    CHECK(!r.commit(a, 65));           // too long
    uint8_t other[8]{};
    CHECK(!r.commit({other, 8}, 4));   // not the acquired slot
    a[0] = 0x5A;
    CHECK(r.commit(a, 1));
    CHECK(r.peek().size() == 1 && r.peek()[0] == 0x5A);
}

void test_wraparound_of_indices() {
    FrameRing<16, 2> r;
    uint8_t b = 0;
    for (uint32_t i = 0; i < 70000; ++i) {  // indices pass 65535; uint32 arithmetic is exact
        b = static_cast<uint8_t>(i);
        CHECK(r.push(&b, 1));
        auto f = r.peek();
        CHECK(f.size() == 1 && f[0] == b);
        r.release();
    }
    CHECK(r.dropped() == 0);
}

// Producer fills slots in place (like the tracer), consumer verifies each frame.
template <size_t SlotBytes, size_t Slots>
void test_two_threads(uint32_t frames, bool slow_consumer) {
    static FrameRing<SlotBytes, Slots> r;
    std::atomic<bool> done{false};
    std::atomic<uint32_t> sent{0};
    uint32_t received = 0;
    uint32_t last_id = 0;
    bool order_ok = true;
    bool content_ok = true;

    std::thread consumer([&] {
        while (true) {
            auto f = r.peek();
            if (f.empty()) {
                if (done.load(std::memory_order_acquire) && r.peek().empty()) break;
                std::this_thread::yield();
                continue;
            }
            const uint32_t id = static_cast<uint32_t>(f[0]) | (static_cast<uint32_t>(f[1]) << 8) |
                                (static_cast<uint32_t>(f[2]) << 16) | (static_cast<uint32_t>(f[3]) << 24);
            if (received != 0 && id <= last_id) order_ok = false;
            last_id = id;
            if (f.size() != 4U + frame_len<SlotBytes - 4>(id)) content_ok = false;
            for (size_t k = 4; k < f.size(); ++k) {
                if (f[k] != static_cast<uint8_t>(id + k)) { content_ok = false; break; }
            }
            ++received;
            if (slow_consumer && (received % 64) == 0) std::this_thread::yield();
            r.release();
        }
    });

    for (uint32_t id = 1; id <= frames; ++id) {
        auto slot = r.acquire();
        if (slot.empty()) {
            r.note_drop();
            std::this_thread::yield();
            continue;
        }
        const size_t len = 4U + frame_len<SlotBytes - 4>(id);
        slot[0] = static_cast<uint8_t>(id);
        slot[1] = static_cast<uint8_t>(id >> 8);
        slot[2] = static_cast<uint8_t>(id >> 16);
        slot[3] = static_cast<uint8_t>(id >> 24);
        for (size_t k = 4; k < len; ++k) slot[k] = static_cast<uint8_t>(id + k);
        if (r.commit(slot, len)) sent.fetch_add(1, std::memory_order_relaxed);
    }
    done.store(true, std::memory_order_release);
    consumer.join();

    CHECK(order_ok);
    CHECK(content_ok);
    CHECK(received == sent.load());
    CHECK(received + r.dropped() == frames);  // every frame either delivered or counted
    CHECK(r.size() == 0);
    std::printf("  two threads <%zu x %zu>: %u frames, %u delivered, %u dropped\n", SlotBytes, Slots,
                frames, received, r.dropped());
}

} // namespace

int main() {
    test_single_thread_basics();
    test_acquire_is_stable_until_commit();
    test_wraparound_of_indices();
    test_two_threads<128, 32>(100000, false);
    test_two_threads<1024, 8>(100000, true);
    if (g_failures != 0) {
        std::printf("test_pico_frame_ring: %d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("test_pico_frame_ring: all checks passed\n");
    return EXIT_SUCCESS;
}
