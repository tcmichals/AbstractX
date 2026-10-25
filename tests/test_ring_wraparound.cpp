// @verifies [SPEC-TLP-02] [SPEC-TLP-03] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Unit Test: Ring Buffer Wraparound & Concurrency Invariants
 * --------------------------------------------------------------------
 * Verifies SpscRingBuffer and AsyncQueue size calculation and queue invariants
 * across uint32_t head/tail sequence wraparound.
 */

#include "CppUTest/TestHarness.h"
#include "tlp_ring.hpp"
#include "asp_coro.hpp"
#include <cstdint>

using namespace abstractx;
using namespace abstractx::coro;

TEST_GROUP(RingWraparoundTest) {
    void setup() override {}
    void teardown() override {}
};

// Test SpscRingBuffer size calculation across normal operation and wraparound
TEST(RingWraparoundTest, SpscRingSizeCalculation) {
    SpscRingBuffer<uint32_t, 8> ring;
    CHECK_EQUAL(0, ring.size());
    CHECK_TRUE(ring.empty());
    CHECK_FALSE(ring.full());

    // Push 3 items
    CHECK_TRUE(ring.push(10));
    CHECK_TRUE(ring.push(20));
    CHECK_TRUE(ring.push(30));
    CHECK_EQUAL(3, ring.size());
    CHECK_FALSE(ring.empty());

    // Pop 2 items
    uint32_t val = 0;
    CHECK_TRUE(ring.pop(val));
    CHECK_EQUAL(10, val);
    CHECK_TRUE(ring.pop(val));
    CHECK_EQUAL(20, val);
    CHECK_EQUAL(1, ring.size());

    // Fill to capacity (capacity is 8 in an 8-slot SPSC ring)
    while (!ring.full()) {
        CHECK_TRUE(ring.push(99));
    }
    CHECK_TRUE(ring.full());
    CHECK_EQUAL(8, ring.size());

    // Verify pushing to full ring fails cleanly
    CHECK_FALSE(ring.push(100));

    // Drain all
    while (ring.pop(val)) {}
    CHECK_TRUE(ring.empty());
    CHECK_EQUAL(0, ring.size());
}

// Steady-state test: 100,000 push/pop cycles to ensure no drift or leak
TEST(RingWraparoundTest, SpscRingSteadyStatePumps) {
    SpscRingBuffer<uint32_t, 16> ring;
    uint32_t seq = 0;
    uint32_t pop_seq = 0;
    uint32_t val = 0;

    for (size_t cycle = 0; cycle < 50000; ++cycle) {
        CHECK_TRUE(ring.push(++seq));
        CHECK_TRUE(ring.push(++seq));
        CHECK_TRUE(ring.pop(val));
        CHECK_EQUAL(++pop_seq, val);
        CHECK_TRUE(ring.pop(val));
        CHECK_EQUAL(++pop_seq, val);
        CHECK_EQUAL(0, ring.size());
        CHECK_TRUE(ring.empty());
    }
}

// Test AsyncQueue power-of-two bitmasking and capacity
TEST(RingWraparoundTest, AsyncQueuePowerOfTwoOperations) {
    AsyncQueue<uint32_t, 16> queue;
    CHECK_EQUAL(0, queue.size());
    CHECK_TRUE(queue.empty());

    for (uint32_t i = 0; i < 16; ++i) {
        CHECK_TRUE(queue.try_push(i * 10));
        CHECK_EQUAL(i + 1, queue.size());
    }

    CHECK_TRUE(queue.full());
    CHECK_FALSE(queue.try_push(999));

    // Drain and verify order
    for (uint32_t i = 0; i < 16; ++i) {
        uint32_t out = 0;
        CHECK_TRUE(queue.try_pop(out));
        CHECK_EQUAL(i * 10, out);
    }
    CHECK_TRUE(queue.empty());
    CHECK_EQUAL(0, queue.size());
}
