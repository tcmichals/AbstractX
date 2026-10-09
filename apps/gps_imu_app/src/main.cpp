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

#include "abstractx/abstractx.hpp"
#include "abstractx/drivers/imu/icm42688p.hpp"
#include "abstractx/drivers/gps/ublox_gps.hpp"
#include "abstractx/drivers/mag/qmc5883l.hpp"
#include "abstractx/fusion/attitude_filter.hpp"
#include <cstdio>

using namespace abstractx;
using namespace abstractx::coro;
using namespace abstractx::drivers::imu;
using namespace abstractx::drivers::gps;
using namespace abstractx::drivers::mag;
using namespace abstractx::fusion;

// Lock-free static TLP rings for inter-domain communication
// @impl [SPEC-TLP-01] [SPEC-TLP-03] [SPEC-ARCH-02] [SPEC-APP-07] docs/DESIGN_SPECIFICATION.md#spec-tlp-01 apps/gps_imu_app/SPECIFICATION.md#spec-app-07
static SpscTlpRing<64> g_tx_ring;        // Flight Controller -> I/O Processor (Requests)
static SpscTlpRing<64> g_sensor_ring;    // I/O Processor -> Flight Controller (Completions)
static SpscTlpRing<64> g_telemetry_ring; // Flight Controller -> Egress / Network (:9870)

// Lock-free asynchronous coroutine channels for multi-rate sensor streaming
// @impl [SPEC-APP-02] [SPEC-APP-03] [SPEC-ARCH-02] apps/gps_imu_app/SPECIFICATION.md#spec-app-02
static AsyncQueue<ImuSample, 32> g_imu_channel; // High rate (1 kHz - 8 kHz)
static AsyncQueue<MagSample, 16> g_mag_channel; // Medium rate (50 Hz - 100 Hz)
static AsyncQueue<GpsFix, 8>     g_gps_channel; // Low rate (5 Hz - 10 Hz)

// ============================================================================
// Multi-Rate Sensor Producer Coroutines (Zero State Machines)
// ============================================================================

// High-Rate IMU Task: Ingests 8 kHz SPI DMA auto-burst samples
// @impl [SPEC-IMU-01] [SPEC-IMU-02] [SPEC-HAL-02] [SPEC-TLP-01] docs/DESIGN_SPECIFICATION.md#spec-imu-01
Task<void> imu_producer_task(Icm42688p& imu) {
    while (true) {
        ImuSample sample = co_await imu.next_sample_async();
        if (sample.valid) {
            g_imu_channel.try_push(sample);
        }
    }
}

// Medium-Rate Magnetometer Task: Ingests 50 Hz I2C 3-axis magnetic compass samples
// @impl [SPEC-HAL-05] [SPEC-APP-03] apps/gps_imu_app/SPECIFICATION.md#spec-app-03
Task<void> mag_producer_task(Qmc5883l& mag, hal::ITimer& timer) {
    while (true) {
        co_await timer.sleep_ms_async(20); // 50 Hz sampling cadence
        MagSample sample = co_await mag.read_sample_async();
        if (sample.valid) {
            g_mag_channel.try_push(sample);
        }
    }
}

// Low-Rate GPS Navigation Task: Ingests 10 Hz UBX-NAV-PVT solutions from UART
// @impl [SPEC-GPS-01] [SPEC-GPS-02] [SPEC-HAL-03] docs/DESIGN_SPECIFICATION.md#spec-gps-01
Task<void> gps_producer_task(UbloxGps& gps) {
    while (true) {
        GpsFix fix = co_await gps.next_fix_async();
        if (fix.valid) {
            g_gps_channel.try_push(fix);
        }
    }
}

// ============================================================================
// Multi-Rate Sensor Fusion & AHRS Filter Coroutine
// ============================================================================
// @impl [SPEC-APP-02] [SPEC-APP-03] [SPEC-ARCH-02] [SPEC-APP-10] apps/gps_imu_app/SPECIFICATION.md
Task<void> sensor_fusion_task(hal::ITimer& timer, AttitudeFilter& filter) {
    uint32_t seq = 0;
    uint64_t last_time_us = timer.get_time_us();

    while (true) {
        // 1. Asynchronously await next high-rate IMU sample as the primary pacing clock
        ImuSample imu = co_await g_imu_channel.pop();

        uint64_t now_us = timer.get_time_us();
        float dt = static_cast<float>(now_us - last_time_us) * 1e-6f;
        if (dt <= 0.0f || dt > 0.05f) dt = 0.001f;
        last_time_us = now_us;

        // 2. High-rate Gyro integration & Accel gravity tilt correction
        filter.update_imu(imu, dt);

        // 3. Drain medium-rate I2C Magnetometer samples (tilt-compensated yaw correction)
        MagSample mag;
        while (g_mag_channel.try_pop(mag)) {
            filter.update_mag(mag);
        }

        // 4. Drain low-rate UART GPS fixes (altitude, velocity vector, course heading)
        GpsFix gps;
        while (g_gps_channel.try_pop(gps)) {
            filter.update_gps(gps);
        }

        // 5. Emit fused 64-byte TLP into telemetry stream
        seq++;
        if ((seq % 10) == 0) { // Telemetry decimation
            Tlp64 tlp = AttitudeFilter::to_tlp(filter.state());
            g_telemetry_ring.push(tlp);
        }
    }
}

// ============================================================================
// Live Flight Telemetry & Heartbeat Display
// ============================================================================

// @impl [SPEC-HAL-05] docs/DESIGN_SPECIFICATION.md#spec-hal-05
Task<void> flight_monitor_task(hal::ITimer& timer, const AttitudeFilter& filter) {
    uint32_t count = 0;
    while (true) {
        co_await timer.sleep_ms_async(1000);
        count++;

        const auto& s = filter.state();
        printf("[AbstractX AHRS #%u] Roll: %+5.1f° | Pitch: %+5.1f° | Yaw: %5.1f° (Mag: %5.1f°) | Alt: %5.1fm | Spd: %4.1f m/s | IMU: %u, Mag: %u, GPS: %u | Egress: %u pkts\n",
               static_cast<unsigned>(count),
               s.roll_deg,
               s.pitch_deg,
               s.yaw_deg,
               s.mag_heading_deg,
               s.altitude_m,
               s.ground_speed_m_s,
               static_cast<unsigned>(s.imu_updates),
               static_cast<unsigned>(s.mag_updates),
               static_cast<unsigned>(s.gps_updates),
               static_cast<unsigned>(g_telemetry_ring.size()));
    }
}

// Background Telemetry Egress Task
// @impl [SPEC-TLP-01] [SPEC-TLP-03] [SPEC-TRACE-03] [SPEC-TRACE-06] [SPEC-APP-08] docs/DESIGN_SPECIFICATION.md#spec-trace-03 apps/gps_imu_app/SPECIFICATION.md#spec-app-08
Task<void> telemetry_egress_task() {
    while (true) {
        Tlp64 tlp{};
        while (g_telemetry_ring.pop(tlp)) {
            hal::platform_send_telemetry(reinterpret_cast<const uint8_t*>(&tlp), sizeof(tlp));
        }
        co_await abstractx::step_async();
    }
}

// ============================================================================
// Unified Application Main Task
// ============================================================================

// @impl [SPEC-APP-01] [SPEC-ARCH-03] [SPEC-ARCH-05] [SPEC-ARCH-06] docs/DESIGN_SPECIFICATION.md#spec-arch-03
Task<void> app_main() {
    auto& uart  = hal::get_uart_driver();
    auto& timer = hal::get_timer_driver();
    auto& spi   = hal::get_spi_driver();
    auto& i2c   = hal::get_i2c_driver();

    printf("\n========================================================\n");
    printf("  AbstractX - Multi-Rate Flight Controller & AHRS Node   \n");
    printf("  Structured Concurrency + Channel-Based Sensor Fusion  \n");
    printf("========================================================\n");

    static Icm42688p       imu(spi);
    static UbloxGps        gps(uart);
    static Qmc5883l        mag(i2c);
    static AttitudeFilter  filter;

    // Structured Parallel Boot: Initialize SPI IMU, UART GPS, and I2C Mag concurrently!
    // @impl [SPEC-APP-01] apps/gps_imu_app/SPECIFICATION.md
    printf("[AbstractX Boot] Parallel hardware initialization (when_all: SPI IMU + UART GPS + I2C Mag)...\n");
    auto [imu_ok, gps_ok, mag_ok] = co_await coro::when_all(
        imu.init_async(),
        gps.init_async(115200),
        mag.init_async()
    );

    printf("[AbstractX Boot] Hardware Status: IMU=%s, GPS=%s, MAG=%s\n",
           imu_ok ? "OK" : "FAIL",
           gps_ok ? "OK" : "FAIL",
           mag_ok ? "OK" : "FAIL");

    // Spawn concurrent multi-rate producer and fusion tasks into AbstractX runtime
    abstractx::spawn(imu_producer_task(imu));
    abstractx::spawn(mag_producer_task(mag, timer));
    abstractx::spawn(gps_producer_task(gps));
    abstractx::spawn(sensor_fusion_task(timer, filter));
    abstractx::spawn(flight_monitor_task(timer, filter));
    abstractx::spawn(telemetry_egress_task());

    // Yield cooperatively to runtime dispatcher
    while (true) {
        co_await abstractx::step_async();
    }
}

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
