/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Serial Loopback & CPU Profiling Reference Application
 * ----------------------------------------------------------------
 * Demonstrates:
 * 1. C++20 Stackless Coroutines (co_await async_read_packet, async_write)
 * 2. Threshold-Balanced Serial I/O (Fast CPU FIFO <= 32B vs Hardware DMA > 32B)
 * 3. 64-byte TLP Packet Framing & barectf CTF 1.8 Binary Event Emission
 * 4. Microsecond CPU Active vs WFI Sleep Cycle Profiling
 * 5. App-layer Ping Dispatch (TLP request -> TLP completion echo)
 */

#include "abstractx/abstractx.hpp"
#include "asp_tlp64.hpp"
#include <stdint.h>
#include <stddef.h>
#include <cstring>

using namespace abstractx;
using namespace abstractx::coro;

static SpscTlpRing<64> g_serial_tx_ring;
static SpscTlpRing<64> g_serial_rx_ring;

// @impl [SPEC-LOOP-01] [SPEC-LOOP-08] [SPEC-LOOP-09] apps/serial_loopback_app/SPECIFICATION.md
Task<void> serial_ping_loopback_task(hal::ITimer& timer) {
    uint32_t echo_sequence = 0;
    while (true) {
        Tlp64 in_tlp{};
        // Check for ingress TLP packet from abstract transport ring (Serial / RPMSG / UDP)
        if (g_serial_rx_ring.pop(in_tlp)) {
            // Verify if this is an App-Layer Ping Request (MemRead / 0x01)
            if (in_tlp.type() == TlpType::MemRead || in_tlp.type() == TlpType::MemWrite) {
                Tlp64 echo_tlp{};
                echo_tlp.wire.type = static_cast<uint8_t>(TlpType::Completion);
                echo_tlp.wire.flags = 0; // ASP_STATUS_OK
                echo_tlp.wire.tag = in_tlp.tag();
                echo_tlp.wire.channel = in_tlp.wire.channel;
                echo_tlp.wire.target_address = in_tlp.target_address();
                echo_tlp.wire.length_dw = in_tlp.wire.length_dw;
                echo_tlp.wire.sequence = ++echo_sequence;
                echo_tlp.wire.timestamp_ns = timer.get_time_us() * 1000ULL;
                std::memcpy(echo_tlp.wire.payload, in_tlp.wire.payload, ASP_TLP64_PAYLOAD_SIZE);

                // Push echo completion into egress ring (Threshold-balanced DMA/FIFO transport)
                g_serial_tx_ring.push(echo_tlp);
            }
        }
        co_await abstractx::step_async();
    }
}

// @impl [SPEC-LOOP-01] [SPEC-LOOP-04] apps/serial_loopback_app/SPECIFICATION.md
Task<void> app_main() {
    auto& timer = hal::get_timer_driver();
    abstractx::spawn(serial_ping_loopback_task(timer));

    while (true) {
        co_await abstractx::step_async();
    }
}

// @impl [SPEC-LOOP-04] [SPEC-LOOP-09] apps/serial_loopback_app/SPECIFICATION.md
int main() {
    abstractx::Config config{};
    config.io_setup.egress_tx_ring = &g_serial_tx_ring;
    config.io_setup.ingress_rx_ring = &g_serial_rx_ring;

    abstractx::init(config);
    abstractx::run(app_main());
    return 0;
}
