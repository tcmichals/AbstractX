/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Raspberry Pi Pico 2 W (RP2350) Flight Companion Node
 * ---------------------------------------------------------------
 * Demonstrates the Pure Asynchronous Work-Queue Coroutine Engine:
 * 1. Coroutines NEVER block on timers or I/O; they yield (co_await) to the work queue.
 * 2. Hardware alarms, SPI DMA completions, and UART ISRs post completion tokens.
 * 3. The work queue dispatcher awakens ready coroutines with 0 B heap allocation.
 * 4. Main loop stays in low-power WFE / event-drain with zero busy-spin polling.
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

// Lock-free static TLP ring for flight telemetry transport
static SpscTlpRing<64> g_telemetry_ring;

// Hardware HAL driver instances
static hal::PicoPioDualSpi g_spi_driver;
static hal::PicoUart        g_uart_driver;
static hal::PicoTimer       g_timer;

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
        // Asynchronously yield to work queue for 1000ms (Hardware Alarm Callback)
        co_await timer.sleep_ms_async(1000);
        count++;

        uart.puts("[AbstractX Pico 2 W] Heartbeat #");
        char num[32];
        snprintf(num, sizeof(num), "%u | TLP Ring: %u pkts\n",
                 static_cast<unsigned>(count),
                 static_cast<unsigned>(g_telemetry_ring.size()));
        uart.puts(num);
    }
}

int main() {
#ifdef PICO_ON_DEVICE
    stdio_init_all();
#endif

    // 1. Initialize Hardware Drivers
    g_uart_driver.init(115200);
    g_uart_driver.puts("\n========================================================\n");
    g_uart_driver.puts("  AbstractX - Pico 2 W (RP2350) Flight Node Ready       \n");
    g_uart_driver.puts("  Pure Work-Queue Coroutine Engine (Zero Blocking Delays)\n");
    g_uart_driver.puts("  Pipeline: ICM-42688-P (SPI) + U-Blox GPS (UART)       \n");
    g_uart_driver.puts("========================================================\n");

    hal::SpiConfig spi_cfg{};
    spi_cfg.frequency_hz = 10'000'000;
    spi_cfg.mode = hal::SpiMode::Mode3;
    g_spi_driver.init(spi_cfg);

    Icm42688p imu(g_spi_driver);
    UbloxGps  gps(g_uart_driver);

    imu.init();

    // 2. Launch Concurrent Coroutine Tasks onto Work Queue
    auto imu_coro       = imu_task(imu);
    auto gps_coro       = gps_task(gps);
    auto heartbeat_coro = heartbeat_task(g_timer, g_uart_driver);

    imu_coro.resume();
    gps_coro.resume();
    heartbeat_coro.resume();

    // 3. Main Work-Queue Event Loop: Coroutines never block; they run when ready
    while (true) {
        // Process ready coroutines scheduled from ISRs, Timers, or I/O completions
        Dispatcher::process();

        // Feed any available UART RX stream into GPS parser
        uint8_t rx_byte = 0;
        GpsFix fix{};
        while (g_uart_driver.read_byte(rx_byte)) {
            if (gps.feed_byte(rx_byte, fix)) {
                Tlp64 tlp = UbloxGps::to_tlp(fix);
                g_telemetry_ring.push(tlp);
            }
        }

#ifdef PICO_ON_DEVICE
        // Low-power wait for interrupt/event (woken by DMA, Timer Alarm, or UART IRQ)
        __asm__ volatile("wfe");
#endif
    }

    return 0;
}
