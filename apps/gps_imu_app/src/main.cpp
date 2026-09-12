/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Heterogeneous Sensor Benchmark & Testing Node (gps_imu_app)
 * ---------------------------------------------------------------------
 * Portable across:
 * - Raspberry Pi Pico 2 W (RP2350 dual-core ARM)
 * - Linux (Desktop Workstation, SITL, and Allwinner Cubie A5E Cortex-A55)
 * - Allwinner XuanTie E907 RISC-V Co-Processor
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
#include <cstdio>

using namespace abstractx;
using namespace abstractx::coro;
using namespace abstractx::drivers::imu;
using namespace abstractx::drivers::gps;

// Lock-free static TLP rings for inter-domain communication
static SpscTlpRing<64> g_tx_ring;        // Sensor Test -> I/O (Requests / IOCTL)
static SpscTlpRing<64> g_sensor_ring;    // I/O -> Sensor Test (Raw DMA sensor packets & Completions)
static SpscTlpRing<64> g_telemetry_ring; // Sensor Test -> Egress / Log (Telemetry to Network / Serial)

// ============================================================================
// Sensor Testing Coroutines
// ============================================================================

// @impl [SPEC-ARCH-03] [SPEC-IMU-02] docs/DESIGN_SPECIFICATION.md#spec-imu-02
// @status Complete
Task<void> imu_test_task(Icm42688p& imu) {
    uint32_t seq = 0;
    while (true) {
        // Asynchronously await next 8 kHz SPI DMA auto-burst sample
        ImuSample sample = co_await imu.next_sample_async();
        if (sample.valid) {
            seq++;
            Tlp64 tlp = Icm42688p::to_tlp(sample);
            g_telemetry_ring.push(tlp);

            // Record binary CTF 1.8 telemetry event
            trace::g_tracer.trace_imu(
                seq,
                static_cast<int16_t>(sample.accel_g[0] * 1000.0f),
                static_cast<int16_t>(sample.accel_g[1] * 1000.0f),
                static_cast<int16_t>(sample.accel_g[2] * 1000.0f),
                static_cast<int16_t>(sample.gyro_dps[0]),
                static_cast<int16_t>(sample.gyro_dps[1]),
                static_cast<int16_t>(sample.gyro_dps[2]),
                static_cast<int16_t>(sample.temp_deg_c * 100.0f),
                sample.timestamp_us ? sample.timestamp_us : hal::get_timer_driver().get_time_us()
            );
        }
    }
}

// @impl [SPEC-GPS-02] docs/DESIGN_SPECIFICATION.md#spec-gps-02
// @status Complete
Task<void> gps_test_task(UbloxGps& gps) {
    while (true) {
        // Asynchronously await next verified UBX-NAV-PVT navigation fix
        GpsFix fix = co_await gps.next_fix_async();
        if (fix.valid) {
            Tlp64 tlp = UbloxGps::to_tlp(fix);
            g_telemetry_ring.push(tlp);

            // Record binary CTF 1.8 GPS fix event
            trace::g_tracer.trace_gps(
                fix.itow_ms,
                fix.lat_1e7,
                fix.lon_1e7,
                fix.alt_msl_mm,
                fix.ground_speed_mm_s,
                fix.heading_1e5,
                fix.satellites,
                static_cast<uint8_t>(fix.fix_type),
                fix.timestamp_us ? fix.timestamp_us : hal::get_timer_driver().get_time_us()
            );
        }
    }
}

// Optional I2C Sensor Probe Task
Task<void> i2c_test_task(hal::II2c& i2c, hal::ITimer& timer) {
    while (true) {
        co_await timer.sleep_ms_async(500);
        uint8_t dummy_reg = 0x00;
        uint8_t rx_byte = 0;
        i2c.write_read_sync(0x68, std::span(&dummy_reg, 1), std::span(&rx_byte, 1));
    }
}

// @impl [SPEC-HAL-05] docs/DESIGN_SPECIFICATION.md#spec-hal-05
// @status Complete
Task<void> heartbeat_task(hal::ITimer& timer, hal::IUart& uart) {
    uint32_t count = 0;
    while (true) {
        co_await timer.sleep_ms_async(1000);
        count++;

        uart.puts("[AbstractX App] Heartbeat #");
        char num[64];
        snprintf(num, sizeof(num), "%u | Egress Queue: %u pkts\n",
                 static_cast<unsigned>(count),
                 static_cast<unsigned>(g_telemetry_ring.size()));
        uart.puts(num);
    }
}

// ============================================================================
// Unified Application Main Task
// ============================================================================

// @impl [SPEC-ARCH-06] docs/DESIGN_SPECIFICATION.md#spec-arch-06
// @status Complete
Task<void> app_main() {
    auto& uart  = hal::get_uart_driver();
    auto& timer = hal::get_timer_driver();
    auto& spi   = hal::get_spi_driver();
    auto& i2c   = hal::get_i2c_driver();

    uart.puts("\n========================================================\n");
    uart.puts("  AbstractX - Heterogeneous Flight Application (gps_imu)\n");
    uart.puts("  Single Unified Event-Driven Coroutine Architecture    \n");
    uart.puts("========================================================\n");

    static Icm42688p imu(spi);
    static UbloxGps  gps(uart);

    imu.init();

    // Spawn concurrent application coroutines into AbstractX runtime
    abstractx::spawn(imu_test_task(imu));
    abstractx::spawn(gps_test_task(gps));
    abstractx::spawn(i2c_test_task(i2c, timer));
    abstractx::spawn(heartbeat_task(timer, uart));

    // Yield cooperatively to runtime dispatcher
    while (true) {
        co_await abstractx::step_async();
    }
}

// ============================================================================
// Application Boot & Master Entry Point
// ============================================================================

// @impl [SPEC-TRACE-03] [SPEC-ARCH-06] docs/DESIGN_SPECIFICATION.md#spec-arch-06
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
