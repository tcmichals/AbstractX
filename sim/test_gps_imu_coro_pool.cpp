// @verifies [SPEC-ARCH-02] docs/DESIGN_SPECIFICATION.md#spec-arch-02
// @verifies [SPEC-APP-01] apps/gps_imu_app/SPECIFICATION.md#spec-app-01
// @verifies [SPEC-E906-MEM-01] targets/allwinner_e906/SPECIFICATION.md#spec-e906-mem-01
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: Coroutine Pool High-Water Measurement & Steady-State Verification
 * Runs the full gps_imu_app multi-rate sensor fusion pipeline and reports
 * the coroutine frame pool high-water mark.
 */

#include "abstractx/abstractx.hpp"
#include "gps_imu_tasks.hpp"
#include <iostream>
#include <cassert>

using namespace abstractx;
using namespace abstractx::coro;

static size_t g_high_water_after_warmup = 0;
static size_t g_high_water_after_steady = 0;

static Task<void> run_measured_app(int steady_steps) {
    // 1. Run app_main boot and 100 warmup steps to allow multi-rate sensors to cycle
    co_await app_main(100);
    g_high_water_after_warmup = coro_pool_high_water();

    // 2. Run 1000 steady-state steps (AGENTS.md Section 10.5 rule)
    for (int i = 0; i < steady_steps; ++i) {
        co_await abstractx::step_async();
    }
    g_high_water_after_steady = coro_pool_high_water();
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  gps_imu_app Coroutine Pool High-Water Verification     \n";
    std::cout << "========================================================\n";

    Config config{};
    config.trace.sink_type = TraceSinkType::None;

    static hal::AutoChannelConfig auto_channels[1]{};
    auto_channels[0].channel_id = 0;
    auto_channels[0].auto_mode = true;
    auto_channels[0].trigger_mode = hal::TriggerMode::GpioEdge;
    auto_channels[0].trigger_pin = 20;
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

    assert(abstractx::init(config));

    // Run for 1000 steady-state steps to satisfy steady-state test rule (AGENTS.md Section 10.5)
    constexpr int kSteadySteps = 1000;
    abstractx::run(run_measured_app(kSteadySteps));

    std::cout << "[+] Warmup high-water mark: " << g_high_water_after_warmup << " bytes\n";
    std::cout << "[+] Steady high-water mark: " << g_high_water_after_steady << " bytes\n";
    std::cout << "[+] Current live bytes:     " << coro_pool_used() << " bytes\n";

    assert(g_high_water_after_warmup > 0 && "Expected frames to be allocated");
    assert(g_high_water_after_steady == g_high_water_after_warmup &&
           "High water mark must not grow after warmup during steady-state execution!");

    std::cout << "\n[CORO_POOL_HIGH_WATER] " << g_high_water_after_steady << " bytes\n";
    std::cout << "[SUCCESS] gps_imu_app task set coroutine pool verified.\n";
    return 0;
}
