/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Heterogeneous Sensor Benchmark & Testing Node (gps_imu_app)
 * ---------------------------------------------------------------------
 * Portable across:
 * - Raspberry Pi Pico 2 W (RP2350 dual-core ARM)
 * - Linux (Desktop Workstation, SITL, and Allwinner Cubie A5E Cortex-A55)
 * - Allwinner XuanTie E906 RISC-V Co-Processor
 *
 * Modernized Unified Architecture:
 * - 100% Event-Driven C++20 Coroutine Application
 * - Zero #ifdef directives across all target platforms
 * - Unified abstractx::init() and abstractx::run() API
 * - Transparent CTF 1.8 binary telemetry tracing (UDP :9870 / File)
 */

// @impl [SPEC-APP-01] [SPEC-ARCH-03] apps/gps_imu_app/SPECIFICATION.md#spec-app-01
#include "abstractx/abstractx.hpp"
#include "gps_imu_tasks.hpp"

// ============================================================================
// Application Boot & Master Entry Point
// ============================================================================

// @impl [SPEC-ARCH-03] [SPEC-ARCH-05] [SPEC-ARCH-06] [SPEC-ARCH-07] [SPEC-TRACE-03] [SPEC-TRACE-04] [SPEC-TRACE-05] [SPEC-STUDIO-07] [SPEC-APP-09] docs/DESIGN_SPECIFICATION.md#spec-arch-06
// @status Complete
int main() {
    // 1. Unified Configuration
    Config config{};
    config.trace.sink_type = TraceSinkType::Udp;
    config.trace.sink_target = "127.0.0.1:9870";
    config.trace.flush_interval_ms = 10;

    // 2. Configure Sensor HW Fusion Channel
    static hal::AutoChannelConfig auto_channels[1]{};
    auto_channels[0].channel_id = 0;
    auto_channels[0].auto_mode = true;
    auto_channels[0].trigger_mode = hal::TriggerMode::GpioEdge;
    auto_channels[0].trigger_pin = 20; // Pin 3 / GP20 DRDY
    auto_channels[0].trigger_rising = true;
    auto_channels[0].bus_type = hal::BusType::Spi;
    auto_channels[0].bus_index = 1;
    auto_channels[0].bus_speed_hz = 10'000'000;
    auto_channels[0].tx_cmd[0] = 0x80 | 0x1D;
    auto_channels[0].tx_len = 1;
    auto_channels[0].rx_len = 15;
    auto_channels[0].tlp_channel = 0x02;
    auto_channels[0].tlp_tag = 1;

    config.io_setup.channels = auto_channels;
    config.io_setup.egress_tx_ring = &g_tx_ring;
    config.io_setup.ingress_rx_ring = &g_sensor_ring;

    // 3. One-line initialization: handles clocks, HAL, SPSC rings, coprocessor, and tracing
    abstractx::init(config);

    // 4. One-line execution: runs coroutines, I/O reactor, and trace dispatcher to completion
    abstractx::run(app_main());

    return 0;
}
