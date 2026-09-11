/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Raspberry Pi Pico 2 W (RP2350) Flight Companion Node
 * ---------------------------------------------------------------
 * Demonstrates:
 * 1. Asynchronous ICM-42688-P 6-axis IMU sampling over SPI
 * 2. Asynchronous U-Blox UBX-NAV-PVT GPS telemetry over UART
 * 3. C++20 Coroutine Async Tasks running on RP2350 ARM Cortex-M33
 * 4. Lock-free SPSC TLP ring packaging for top-level flight controller
 */

#include "abstractx_pico.hpp"
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

// Static TLP message ring for telemetry transport
static SpscTlpRing<64> g_telemetry_ring;

// Hardware HAL driver instances
static hal::PicoPioDualSpi g_spi_driver;
static hal::PicoUart        g_uart_driver;
static hal::PicoTimer       g_timer;

// @impl [SPEC-ARCH-03] docs/DESIGN_SPECIFICATION.md#spec-arch-03
// @status Complete
Task<void> imu_task(Icm42688p& imu) {
    while (true) {
        ImuSample sample = co_await imu.next_sample_async();
        if (sample.valid) {
            Tlp64 tlp = Icm42688p::to_tlp(sample);
            g_telemetry_ring.push(tlp);
        }
    }
}

// @impl [SPEC-GPS-02] docs/DESIGN_SPECIFICATION.md#spec-gps-02
// @status Complete
Task<void> gps_task(UbloxGps& gps) {
    while (true) {
        GpsFix fix = co_await gps.next_fix_async();
        if (fix.valid) {
            Tlp64 tlp = UbloxGps::to_tlp(fix);
            g_telemetry_ring.push(tlp);
        }
    }
}

int main() {
#ifdef PICO_ON_DEVICE
    stdio_init_all();
#endif

    g_uart_driver.init(115200);
    g_uart_driver.puts("\n========================================================\n");
    g_uart_driver.puts("  AbstractX - Pico 2 W (RP2350) Flight Node Initialized \n");
    g_uart_driver.puts("  Target: ARM Cortex-M33 @ 150MHz (Pico SDK)            \n");
    g_uart_driver.puts("  Pipeline: ICM-42688-P (SPI) + U-Blox GPS (UART)       \n");
    g_uart_driver.puts("========================================================\n");

    hal::SpiConfig spi_cfg{};
    spi_cfg.frequency_hz = 10'000'000;
    spi_cfg.mode = hal::SpiMode::Mode3;
    g_spi_driver.init(spi_cfg);

    Icm42688p imu(g_spi_driver);
    UbloxGps  gps(g_uart_driver);

    imu.init();

    auto imu_coro = imu_task(imu);
    auto gps_coro = gps_task(gps);

    // Initial coroutine start
    imu_coro.resume();
    gps_coro.resume();

    uint32_t last_heartbeat_ms = g_timer.get_time_ms();

    while (true) {
        // Process any ready coroutines awakened by ISR / DMA
        Dispatcher::process();

        // Drain any incoming UART bytes into the GPS parser
        uint8_t rx_byte = 0;
        GpsFix fix{};
        while (g_uart_driver.read_byte(rx_byte)) {
            if (gps.feed_byte(rx_byte, fix)) {
                Tlp64 tlp = UbloxGps::to_tlp(fix);
                g_telemetry_ring.push(tlp);
            }
        }

        uint32_t now = g_timer.get_time_ms();
        if (now - last_heartbeat_ms >= 1000) {
            last_heartbeat_ms = now;
            g_uart_driver.puts("[AbstractX Pico 2 W] Telemetry Ring Size: ");
            char num[16];
            snprintf(num, sizeof(num), "%u\n", static_cast<unsigned>(g_telemetry_ring.size()));
            g_uart_driver.puts(num);
        }

        g_timer.delay_us(50);
    }

    return 0;
}
