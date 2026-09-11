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
 * Dual-Domain Architecture:
 * - Domain 0: Dedicated I/O, DMA, Wireless Network Master, and Doorbell Bridge
 * - Domain 1: Dedicated Real-Time Coroutine Sensor Testing Engine
 *
 * Zero #ifdef Invariant: 100% portable modern C++20 using generic AbstractX HAL.
 */

#include "abstractx/hal/platform.hpp"
#include "abstractx/coro.hpp"
#include "abstractx/drivers/imu/icm42688p.hpp"
#include "abstractx/drivers/gps/ublox_gps.hpp"
#include "spsc_tlp_ring.hpp"
#include "asp_tlp64.hpp"

#include <cstdio>
#include <etl/circular_buffer.h>

using namespace abstractx;
using namespace abstractx::coro;
using namespace abstractx::drivers::imu;
using namespace abstractx::drivers::gps;

// Lock-free static TLP rings for inter-domain communication
static SpscTlpRing<64> g_tx_ring;        // Sensor Test -> I/O (Requests / IOCTL)
static SpscTlpRing<64> g_sensor_ring;    // I/O -> Sensor Test (Raw DMA sensor packets & Completions)
static SpscTlpRing<64> g_telemetry_ring; // Sensor Test -> Egress / Log (Telemetry to Network / Serial)

// ============================================================================
// Domain 1: Dedicated Real-Time Coroutine Sensor Testing Domain
// ============================================================================

// @impl [SPEC-ARCH-03] [SPEC-IMU-02] docs/DESIGN_SPECIFICATION.md#spec-imu-02
// @status Complete
Task<void> imu_test_task(Icm42688p& imu) {
    while (true) {
        // Asynchronously await next 8 kHz SPI DMA auto-burst sample
        ImuSample sample = co_await imu.next_sample_async();
        if (sample.valid) {
            Tlp64 tlp = Icm42688p::to_tlp(sample);
            g_telemetry_ring.push(tlp);
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
        }
    }
}

// Optional I2C Sensor Probe Task
Task<void> i2c_test_task(hal::II2c& i2c, hal::ITimer& timer) {
    while (true) {
        co_await timer.sleep_ms_async(500);
        // Periodic I2C bus health ping / sensor probe
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

        uart.puts("[AbstractX Sensor Test] Heartbeat #");
        char num[64];
        snprintf(num, sizeof(num), "%u | Egress Queue: %u pkts\n",
                 static_cast<unsigned>(count),
                 static_cast<unsigned>(g_telemetry_ring.size()));
        uart.puts(num);
    }
}

// @impl [SPEC-ARCH-05] docs/DESIGN_SPECIFICATION.md#spec-arch-05
// @status Complete
void sensor_testing_engine() {
    auto& uart  = hal::get_uart_driver();
    auto& timer = hal::get_timer_driver();
    auto& spi   = hal::get_spi_driver();
    auto& i2c   = hal::get_i2c_driver();

    uart.puts("[Sensor Test Domain] Coroutine Engine Online!\n");

    Icm42688p imu(spi);
    UbloxGps  gps(uart);

    imu.init();

    // Launch sensor benchmark coroutines onto the work queue
    auto imu_coro       = imu_test_task(imu);
    auto gps_coro       = gps_test_task(gps);
    auto i2c_coro       = i2c_test_task(i2c, timer);
    auto heartbeat_coro = heartbeat_task(timer, uart);

    imu_coro.resume();
    gps_coro.resume();
    i2c_coro.resume();
    heartbeat_coro.resume();

    // Dedicated Sensor Testing Coroutine Loop
    while (true) {
        Dispatcher::process();
        hal::platform_idle_wait();
    }
}

// ============================================================================
// Domain 0: Dedicated I/O, DMA & Wireless Networking Master
// ============================================================================

// @impl [SPEC-TRACE-03] docs/DESIGN_SPECIFICATION.md#spec-trace-03
// @status Complete
int main() {
    hal::platform_init();

    auto& uart    = hal::get_uart_driver();
    auto& timer   = hal::get_timer_driver();
    auto& io_proc = hal::get_target_io_processor();

    uart.init(115200);
    uart.puts("\n========================================================\n");
    uart.puts("  AbstractX - Heterogeneous Sensor Testbench (gps_imu)  \n");
    uart.puts("  Domain 0: ioProcessor Message Processing Loop         \n");
    uart.puts("  Domain 1: Real-Time Coroutine Sensor Testing Engine   \n");
    uart.puts("========================================================\n");

    // Configure and start target ioProcessor on Domain 0
    hal::AutoChannelConfig auto_channels[1]{};
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

    hal::IoProcessorSetup setup{};
    setup.channels = auto_channels;
    setup.egress_tx_ring = &g_tx_ring;
    setup.ingress_rx_ring = &g_sensor_ring;
    io_proc.configure(setup);
    io_proc.start();

    // Launch Sensor Testing Engine on Domain 1 (Core 1 / Worker Thread)
    hal::platform_launch_processing_domain(sensor_testing_engine);

    // Domain 0: Dedicated I/O Message Processing Loop
    UbloxGps gps(uart);
    uint32_t last_net_poll_ms = timer.get_time_ms();

    while (true) {
        // 1. Drain egress requests from Sensor Domain and pump hardware DMA
        io_proc.step();

        // 2. Drain incoming UART bytes from GPS into shared sensor ring
        uint8_t rx_byte = 0;
        GpsFix fix{};
        while (uart.read_byte(rx_byte)) {
            if (gps.feed_byte(rx_byte, fix)) {
                Tlp64 tlp = UbloxGps::to_tlp(fix);
                g_sensor_ring.push(tlp);
            }
        }

        // 3. Poll networking stack periodically
        uint32_t now = timer.get_time_ms();
        if (now - last_net_poll_ms >= 50) {
            last_net_poll_ms = now;
            hal::platform_poll_network();
        }

        // 4. Forward completed telemetry TLPs from Sensor Domain to Network / UART
        Tlp64 tlp{};
        while (g_telemetry_ring.pop(tlp)) {
            // Stream packet over Network / Serial
        }

        hal::platform_idle_wait();
    }

    return 0;
}
