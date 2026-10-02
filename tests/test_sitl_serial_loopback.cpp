/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX SITL: Serial Loopback & TLP Ping-Echo 2-Queue Verification
 * -------------------------------------------------------------------
 * Tests software coroutine loopback using in-memory SPSC rings (RxRing <-> TxRing)
 * with zero TTY / PTY / OS kernel dependencies.
 */

#include "CppUTest/TestHarness.h"
#include "abstractx/abstractx.hpp"
#include "asp_tlp64.hpp"
#include <cstring>

using namespace abstractx;
using namespace abstractx::coro;

static SpscTlpRing<64> s_test_rx_ring;
static SpscTlpRing<64> s_test_tx_ring;

// @impl [SPEC-LOOP-01] [SPEC-LOOP-08] [SPEC-LOOP-09] apps/serial_loopback_app/SPECIFICATION.md
static Task<void> sitl_loopback_coroutine() {
    uint32_t echo_sequence = 0;
    while (true) {
        Tlp64 in_tlp{};
        if (s_test_rx_ring.pop(in_tlp)) {
            if (in_tlp.type() == TlpType::MemRead || in_tlp.type() == TlpType::MemWrite) {
                Tlp64 echo_tlp{};
                echo_tlp.wire.type = static_cast<uint8_t>(TlpType::Completion);
                echo_tlp.wire.flags = 0; // ASP_STATUS_OK
                echo_tlp.wire.tag = in_tlp.tag();
                echo_tlp.wire.channel = in_tlp.wire.channel;
                echo_tlp.wire.target_address = in_tlp.target_address();
                echo_tlp.wire.length_dw = in_tlp.wire.length_dw;
                echo_tlp.wire.sequence = ++echo_sequence;
                echo_tlp.wire.timestamp_ns = 5000ULL * echo_sequence;
                std::memcpy(echo_tlp.wire.payload, in_tlp.wire.payload, ASP_TLP64_PAYLOAD_SIZE);
                s_test_tx_ring.push(echo_tlp);
            }
        }
        co_await abstractx::step_async();
    }
}

TEST_GROUP(SitlSerialLoopback) {
    void setup() override {
        Tlp64 drain{};
        while (s_test_rx_ring.pop(drain)) {}
        while (s_test_tx_ring.pop(drain)) {}
    }

    void teardown() override {
    }
};

// Test 1: Single 64-byte TLP Ping request into RX ring yields Completion in TX ring
TEST(SitlSerialLoopback, SinglePingEcho) {
    auto task = sitl_loopback_coroutine();
    task.resume();

    // 1. Prepare Ping Request TLP
    Tlp64 ping{};
    ping.wire.type = static_cast<uint8_t>(TlpType::MemRead);
    ping.wire.tag = 0x42;
    ping.wire.channel = static_cast<uint8_t>(Channel::Control);
    ping.wire.target_address = 0x40000100;
    ping.wire.length_dw = 2;
    ping.wire.sequence = 1;
    ping.wire.timestamp_ns = 1000000ULL;
    std::strcpy(reinterpret_cast<char*>(ping.wire.payload), "PING_TEST_01");

    // 2. Push to RX ring
    bool pushed = s_test_rx_ring.push(ping);
    CHECK_TRUE(pushed);
    LONGS_EQUAL(1, s_test_rx_ring.size());
    LONGS_EQUAL(0, s_test_tx_ring.size());

    // 3. Step coroutine task
    task.resume();

    // 4. Verify packet consumed from RX and echoed into TX
    LONGS_EQUAL(0, s_test_rx_ring.size());
    LONGS_EQUAL(1, s_test_tx_ring.size());

    Tlp64 echo{};
    bool popped = s_test_tx_ring.pop(echo);
    CHECK_TRUE(popped);
    LONGS_EQUAL(static_cast<uint8_t>(TlpType::Completion), echo.wire.type);
    LONGS_EQUAL(0x42, echo.wire.tag);
    LONGS_EQUAL(1, echo.wire.sequence);
    LONGS_EQUAL(0, echo.wire.flags);
    STRCMP_EQUAL("PING_TEST_01", reinterpret_cast<const char*>(echo.wire.payload));
}

// Test 2: In-memory two-queue swap loopback stress test
TEST(SitlSerialLoopback, SustainedTwoQueueSwapLoopback) {
    auto task = sitl_loopback_coroutine();
    task.resume();

    constexpr uint32_t NUM_PACKETS = 100;

    for (uint32_t i = 1; i <= NUM_PACKETS; ++i) {
        Tlp64 ping{};
        ping.wire.type = static_cast<uint8_t>(TlpType::MemRead);
        ping.wire.tag = static_cast<uint8_t>(i & 0xFF);
        ping.wire.channel = static_cast<uint8_t>(Channel::Control);
        ping.wire.sequence = static_cast<uint16_t>(i);
        std::snprintf(reinterpret_cast<char*>(ping.wire.payload), sizeof(ping.wire.payload), "BURST_%u", i);

        // Push to RX queue
        CHECK_TRUE(s_test_rx_ring.push(ping));

        // Step coroutine
        task.resume();

        // Pop from TX queue (swap verification)
        Tlp64 echo{};
        CHECK_TRUE(s_test_tx_ring.pop(echo));
        LONGS_EQUAL(static_cast<uint8_t>(TlpType::Completion), echo.wire.type);
        LONGS_EQUAL(static_cast<uint8_t>(i & 0xFF), echo.wire.tag);
        LONGS_EQUAL(i, echo.wire.sequence);
    }

    LONGS_EQUAL(0, s_test_rx_ring.size());
    LONGS_EQUAL(0, s_test_tx_ring.size());
}
