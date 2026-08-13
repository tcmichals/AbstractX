/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX C++20 Virtual BAR Device Address Map & Register Layouts
 */

#ifndef PCIE_BAR_MAP_HPP
#define PCIE_BAR_MAP_HPP

#include "pcie_reg_api.h"
#include <cstdint>

namespace abstractx {

namespace bar {
    constexpr uint32_t SystemBase   = PCIE_BAR_SYS_BASE;    // 0x40000000
    constexpr uint32_t ImuBase      = PCIE_BAR_IMU_BASE;    // 0x40000100
    constexpr uint32_t EscBase      = PCIE_BAR_ESC_BASE;    // 0x40000200
    constexpr uint32_t BaroBase     = PCIE_BAR_BARO_BASE;   // 0x40000300
    constexpr uint32_t MagBase      = PCIE_BAR_MAG_BASE;    // 0x40000400
    constexpr uint32_t SerialBase   = PCIE_BAR_SERIAL_BASE; // 0x40000500
} // namespace bar

// System BAR Register Offsets
namespace reg::sys {
    constexpr uint32_t WhoAmI        = 0x00; // Expected: 0x41535036 ("ASP6")
    constexpr uint32_t Version       = 0x04; // Hardware version ID
    constexpr uint32_t Scratch       = 0x08; // R/W scratchpad
    constexpr uint32_t Status        = 0x0C; // Global system status
    constexpr uint32_t UptimeUs      = 0x10; // Hardware 64-bit microsecond counter low
} // namespace reg::sys

// IMU Auto-DMA BAR Register Offsets
namespace reg::imu {
    constexpr uint32_t Control       = 0x00; // Bit 0: Enable, Bit 1: Continuous Read, Bit 2: IRQ Enable
    constexpr uint32_t Status        = 0x04; // Bit 0: DRDY pending, Bit 1: DMA active, Bit 2: Error
    constexpr uint32_t SampleRateHz  = 0x08; // Target rate (1000, 8000 Hz)
    constexpr uint32_t ContinuousAddr = 0x0C; // Target SPI starting register (e.g. 0x1D continuous read)
    constexpr uint32_t ContinuousLen  = 0x10; // Target continuous burst length (e.g. 14 bytes)
    constexpr uint32_t AccelX        = 0x14; // Latched Accel X (16-bit packed)
    constexpr uint32_t AccelY        = 0x18; // Latched Accel Y
    constexpr uint32_t AccelZ        = 0x1C; // Latched Accel Z
    constexpr uint32_t GyroX         = 0x20; // Latched Gyro X
    constexpr uint32_t GyroY         = 0x24; // Latched Gyro Y
    constexpr uint32_t GyroZ         = 0x28; // Latched Gyro Z
    constexpr uint32_t TimestampNsHi = 0x2C; // 64-bit nanosecond hardware timestamp high DWORD
    constexpr uint32_t TimestampNsLo = 0x30; // 64-bit nanosecond hardware timestamp low DWORD
} // namespace reg::imu

// ESC DShot BAR Register Offsets
namespace reg::esc {
    constexpr uint32_t Control       = 0x00; // Bit 0: Enable, Bit 1: DShot protocol (300/600/1200)
    constexpr uint32_t Status        = 0x04; // Motor arm status & telemetry status
    constexpr uint32_t Motor1        = 0x08; // DShot command Motor 1 (0..2047)
    constexpr uint32_t Motor2        = 0x0C; // DShot command Motor 2 (0..2047)
    constexpr uint32_t Motor3        = 0x10; // DShot command Motor 3 (0..2047)
    constexpr uint32_t Motor4        = 0x14; // DShot command Motor 4 (0..2047)
    constexpr uint32_t TelemetryRpm1  = 0x18; // Bidirectional DShot RPM Motor 1
    constexpr uint32_t TelemetryRpm2  = 0x1C; // Bidirectional DShot RPM Motor 2
    constexpr uint32_t TelemetryRpm3  = 0x20; // Bidirectional DShot RPM Motor 3
    constexpr uint32_t TelemetryRpm4  = 0x24; // Bidirectional DShot RPM Motor 4
} // namespace reg::esc

} // namespace abstractx

#endif // PCIE_BAR_MAP_HPP
