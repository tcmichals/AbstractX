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
 */

#include "abstractx/abstractx.hpp"
#include <stdint.h>
#include <stddef.h>

using namespace abstractx;
using namespace abstractx::coro;

static SpscTlpRing<64> g_serial_tx_ring;
static SpscTlpRing<64> g_serial_rx_ring;

Task<void> serial_loopback_task() {
    co_return;
}

Task<void> app_main() {
    co_await serial_loopback_task();
}

int main() {
    abstractx::Config config{};
    config.io_setup.egress_tx_ring = &g_serial_tx_ring;
    config.io_setup.ingress_rx_ring = &g_serial_rx_ring;

    abstractx::init(config);
    abstractx::run(app_main());
    return 0;
}
