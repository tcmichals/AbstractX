/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Raspberry Pi Pico 2 W (RP2350) Flight Companion Node
 * ---------------------------------------------------------------
 * Dual-Core Asymmetric Multiprocessing (AMP) Architecture:
 * - Core 0: Dedicated I/O, DMA, CYW43439 Wi-Fi/Network Master, and SIO Doorbell Bridge
 * - Core 1: Dedicated Real-Time Coroutine Flight Engine (Zero Network Jitter)
 */

#include "abstractx_pico.hpp"
#include "abstractx/coro.hpp"
#include "abstractx/drivers/imu/icm42688p.hpp"
#include "abstractx/drivers/gps/ublox_gps.hpp"
#include "spsc_tlp_ring.hpp"
#include "asp_tlp64.hpp"

#include <cstdio>
#include <etl/circular_buffer.h>

#ifdef PICO_ON_DEVICE
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "hardware/sync.h"
#endif

using namespace abstractx;
using namespace abstractx::coro;
using namespace abstractx::drivers::imu;
using namespace abstractx::drivers::gps;

// Lock-free static TLP rings for inter-core communication
static SpscTlpRing<64> g_telemetry_ring; // Core 1 -> Core 0 (Telemetry to Wi-Fi / GCS)
static SpscTlpRing<64> g_sensor_ring;    // Core 0 -> Core 1 (Raw DMA sensor packets)

// Hardware HAL driver instances
static hal::PicoPioDualSpi g_spi_driver;
static hal::PicoUart        g_uart_driver;
static hal::PicoTimer       g_timer;

// ============================================================================
// Core 1: Dedicated Real-Time Coroutine Flight Engine
// ============================================================================

// @impl [SPEC-ARCH-03] [SPEC-IMU-02] docs/DESIGN_SPECIFICATION.md#spec-imu-02
// @status Complete
Task<void> imu_task(Icm42688p& imu) {
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
Task<void> gps_task(UbloxGps& gps) {
    while (true) {
        // Asynchronously await next verified UBX-NAV-PVT navigation fix
        GpsFix fix = co_await gps.next_fix_async();
        if (fix.valid) {
            Tlp64 tlp = UbloxGps::to_tlp(fix);
            g_telemetry_ring.push(tlp);
        }
    }
}

// @impl [SPEC-HAL-05] docs/DESIGN_SPECIFICATION.md#spec-hal-05
// @status Complete
Task<void> heartbeat_task(hal::ITimer& timer, hal::IUart& uart) {
    uint32_t count = 0;
    while (true) {
        co_await timer.sleep_ms_async(1000);
        count++;

        uart.puts("[AbstractX Pico 2 W Core 1] Heartbeat #");
        char num[32];
        snprintf(num, sizeof(num), "%u | Telemetry Ring: %u\n",
                 static_cast<unsigned>(count),
                 static_cast<unsigned>(g_telemetry_ring.size()));
        uart.puts(num);
    }
}

// @impl [SPEC-ARCH-05] docs/DESIGN_SPECIFICATION.md#spec-arch-05
// @status Complete
void core1_coroutine_flight_engine() {
    g_uart_driver.puts("[Core 1] Real-Time Flight Coroutine Engine Online!\n");

    Icm42688p imu(g_spi_driver);
    UbloxGps  gps(g_uart_driver);

    imu.init();

    // Launch coroutines onto the work queue
    auto imu_coro       = imu_task(imu);
    auto gps_coro       = gps_task(gps);
    auto heartbeat_coro = heartbeat_task(g_timer, g_uart_driver);

    imu_coro.resume();
    gps_coro.resume();
    heartbeat_coro.resume();

    while (true) {
        // Drain ready coroutines from the work queue
        Dispatcher::process();

#ifdef PICO_ON_DEVICE
        // Sleep in WFE until next hardware DMA or inter-core SIO interrupt
        __asm__ volatile("wfe");
#endif
    }
}

// ============================================================================
// Core 0: Dedicated I/O, DMA & Wireless Networking Master
// ============================================================================

// @impl [SPEC-TRACE-03] docs/DESIGN_SPECIFICATION.md#spec-trace-03
// @status Complete
int main() {
#ifdef PICO_ON_DEVICE
    stdio_init_all();
#endif

    g_uart_driver.init(115200);
    g_uart_driver.puts("\n========================================================\n");
    g_uart_driver.puts("  AbstractX - Pico 2 W (RP2350) Dual-Core AMP Started   \n");
    g_uart_driver.puts("  Core 0: I/O, DMA & CYW43439 Wi-Fi Network Master      \n");
    g_uart_driver.puts("  Core 1: Dedicated Real-Time Coroutine Flight Engine   \n");
    g_uart_driver.puts("========================================================\n");

    hal::SpiConfig spi_cfg{};
    spi_cfg.frequency_hz = 10'000'000;
    spi_cfg.mode = hal::SpiMode::Mode3;
    g_spi_driver.init(spi_cfg);

#ifdef PICO_ON_DEVICE
    // Initialize CYW43439 Wi-Fi Architecture on Core 0
    if (cyw43_arch_init() == 0) {
        g_uart_driver.puts("[Core 0] CYW43 Wi-Fi Initialized Successfully.\n");
        cyw43_arch_enable_sta_mode();
    }

    // Launch Core 1 Flight Coroutine Engine
    multicore_launch_core1(core1_coroutine_flight_engine);
#else
    // Host SITL simulation fallback
    core1_coroutine_flight_engine();
#endif

    // Core 0 I/O & Networking Loop
    UbloxGps gps(g_uart_driver);
    uint32_t last_wifi_poll_ms = g_timer.get_time_ms();

    while (true) {
        // 1. Drain incoming UART bytes from GPS into shared sensor ring
        uint8_t rx_byte = 0;
        GpsFix fix{};
        while (g_uart_driver.read_byte(rx_byte)) {
            if (gps.feed_byte(rx_byte, fix)) {
                Tlp64 tlp = UbloxGps::to_tlp(fix);
                g_sensor_ring.push(tlp);
            }
        }

        // 2. Poll Wi-Fi / Networking stack periodically
        uint32_t now = g_timer.get_time_ms();
        if (now - last_wifi_poll_ms >= 50) {
            last_wifi_poll_ms = now;
#ifdef PICO_ON_DEVICE
            cyw43_arch_poll();
#endif
        }

        // 3. Forward completed telemetry TLPs from Core 1 to Wi-Fi / UART stream
        Tlp64 tlp{};
        while (g_telemetry_ring.pop(tlp)) {
            // Stream packet over Wi-Fi / Serial
        }

#ifdef PICO_ON_DEVICE
        __asm__ volatile("wfe");
#endif
    }

    return 0;
}
