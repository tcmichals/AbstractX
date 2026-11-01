/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX XuanTie E906 Generic I/O Co-Processor Firmware (e906_coprocessor)
 * ---------------------------------------------------------------------------
 * Sensor-agnostic C++20 I/O Processor for the Allwinner XuanTie E906:
 *   - Contains ZERO sensor-specific drivers (no IMU, Compass, GPS, or AHRS math).
 *   - Executes generic 64-byte TLP bridge requests from Linux over RPMsg
 *     endpoint 1024 ("abstractx-tlp") via IoEngine + E906IoBackend:
 *       * SPI Bridge   (Channel::SpiBridge = 0x06, SPI1 DMA)
 *       * I2C Bridge   (Channel::I2cBridge = 0x07, TWI1 / TWI0)
 *       * Serial/UART  (Channel::EscSerial = 0x05, UART3 DMA RX/TX)
 *       * GPIO Bridge  (Channel::GpioBridge = 0x08)
 *       * Auto-DMA     (ASP_TLP_TYPE_DMA_CFG = 0x31: programmable GPIO-edge
 *                       or timer-triggered SPI/I2C DMA bursts -> TLP stream 0x10)
 *   - Streams CTF 1.8 runtime status, profiler, and timeline packets over
 *     RPMsg endpoint 1025 ("abstractx-trace").
 */

// @impl [SPEC-ARCH-04] [SPEC-ARCH-13] [SPEC-TRACE-03] [SPEC-E906-IPC-04] [SPEC-APP-11] docs/DESIGN_SPECIFICATION.md#spec-trace-03 apps/gps_imu_app/SPECIFICATION.md#spec-app-11
// @status Complete

#include "abstractx/abstractx.hpp"
#include "abstractx/hal/io_processor.hpp"

namespace {

// Static lock-free TLP rings connecting RPMsg endpoint 1024 ("abstractx-tlp") to IoEngine
abstractx::MpscTlpRing<> g_e906_tx_ring; // Linux RPMsg 1024 -> E906 IoEngine (Bridge & DMA_CFG TLPs)
abstractx::MpscTlpRing<> g_e906_rx_ring; // E906 IoEngine -> Linux RPMsg 1024 (Completions & Auto-DMA Stream TLPs)

// Coroutine that drains IoEngine completions and Auto-DMA stream TLPs to RPMsg endpoint 1024
abstractx::coro::Task<void> io_egress_task() {
    while (true) {
        abstractx::Tlp64 tlp{};
        while (g_e906_rx_ring.pop(tlp)) {
            abstractx::hal::platform_send_telemetry(
                reinterpret_cast<const uint8_t*>(&tlp), sizeof(tlp));
        }
        co_await abstractx::step_async();
    }
}

} // namespace

int main() {
    static abstractx::hal::AutoChannelConfig auto_channels[2]{};
    auto_channels[0].channel_id     = 0;
    auto_channels[0].auto_mode      = false; // Enabled dynamically by Linux/Studio via ASP_TLP_TYPE_DMA_CFG (0x31)
    auto_channels[0].trigger_mode   = abstractx::hal::TriggerMode::GpioEdge;
    auto_channels[0].trigger_pin    = abstractx::hal::platform_default_imu_drdy_pin();
    auto_channels[0].trigger_rising = true;
    auto_channels[0].bus_type       = abstractx::hal::BusType::Spi;
    auto_channels[0].bus_index      = 1;
    auto_channels[0].cs_pin         = 0;
    auto_channels[0].bus_speed_hz   = 10'000'000;
    auto_channels[0].tx_cmd[0]      = 0x9D;
    auto_channels[0].tx_len         = 1;
    auto_channels[0].rx_len         = 14;
    auto_channels[0].tlp_channel    = static_cast<uint8_t>(abstractx::Channel::Telemetry);
    auto_channels[0].tlp_tag        = 1;

    auto_channels[1].channel_id     = 1;
    auto_channels[1].auto_mode      = false;
    auto_channels[1].trigger_mode   = abstractx::hal::TriggerMode::Timer;
    auto_channels[1].trigger_pin    = 0;
    auto_channels[1].trigger_rising = true;
    auto_channels[1].bus_type       = abstractx::hal::BusType::I2c;
    auto_channels[1].bus_index      = 1;
    auto_channels[1].cs_pin         = 0x0D;
    auto_channels[1].bus_speed_hz   = 400'000;
    auto_channels[1].tx_cmd[0]      = 0x00;
    auto_channels[1].tx_len         = 1;
    auto_channels[1].rx_len         = 6;
    auto_channels[1].tlp_channel    = static_cast<uint8_t>(abstractx::Channel::Telemetry);
    auto_channels[1].tlp_tag        = 3;

    abstractx::Config cfg{};
    cfg.io_setup.channels        = std::span<const abstractx::hal::AutoChannelConfig>(auto_channels, 2);
    cfg.io_setup.egress_tx_ring  = &g_e906_tx_ring;
    cfg.io_setup.ingress_rx_ring = &g_e906_rx_ring;

    abstractx::init(cfg);
    abstractx::run(io_egress_task());
    return 0;
}
