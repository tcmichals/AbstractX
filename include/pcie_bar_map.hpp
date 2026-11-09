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

// @impl [SPEC-TLP-02] docs/DESIGN_SPECIFICATION.md#spec-tlp-02
namespace bar {
    constexpr uint32_t SystemBase    = 0x0000u; // 0x0000
    constexpr uint32_t ImuBase       = 0x1000u; // 0x1000
    constexpr uint32_t EscBase       = 0x2000u; // 0x2000
    constexpr uint32_t BaroBase      = 0x3000u; // 0x3000
    constexpr uint32_t MagBase       = 0x4000u; // 0x4000
    constexpr uint32_t SerialBase    = 0x5000u; // 0x5000
    constexpr uint32_t GpsBase       = 0x5000u;
    constexpr uint32_t RcBase        = 0x6000u;
    constexpr uint32_t DisplayBase   = 0x7000u;
    constexpr uint32_t LedBase       = 0x8000u;
    constexpr uint32_t PowerBase     = 0x9000u;
    constexpr uint32_t PitotBase     = 0xA000u;
    constexpr uint32_t TelemetryBase = 0xB000u;
    constexpr uint32_t OpticalFlowBase = 0xC000u;
} // namespace bar

namespace phys_bar {
    constexpr uint32_t SystemBase    = PCIE_BAR_SYS_BASE;    // 0x40000000 (sys_regs)
    constexpr uint32_t ImuBase       = PCIE_BAR_IMU_BASE;    // 0x40000100 (imu_auto_dma)
    constexpr uint32_t EscBase       = PCIE_BAR_ESC_BASE;    // 0x40000200 (dshot)
    constexpr uint32_t PwmBase       = 0x40000300u;          // 0x40000300 (pwm_decoder)
    constexpr uint32_t UartBase      = PCIE_BAR_SERIAL_BASE; // 0x40000500 (uart)
    constexpr uint32_t LedBase       = 0x40000600u;          // 0x40000600 (neopixel)
    constexpr uint32_t BaroBase      = PCIE_BAR_BARO_BASE;   // 0x40000300 (legacy virtual)
    constexpr uint32_t MagBase       = PCIE_BAR_MAG_BASE;    // 0x40000400 (legacy virtual)
    constexpr uint32_t SerialBase    = PCIE_BAR_SERIAL_BASE; // 0x40000500
} // namespace phys_bar

// System BAR Register Offsets (abstractx-fpga/cores/sys_regs.json)
namespace reg::sys {
    constexpr uint32_t IdRev         = 0x00; // Expected: 0xABF10164
    constexpr uint32_t Vendor        = 0x04; // Expected: 0x19981ACC
    constexpr uint32_t Scratch       = 0x08; // R/W scratchpad
    constexpr uint32_t Led           = 0x0C; // Diagnostic LEDs
    constexpr uint32_t TimeLow       = 0x10; // Read latches TimeHigh
    constexpr uint32_t TimeHigh      = 0x14; // High 32 bits of 64-bit cycle counter
    // Backward-compatibility aliases
    constexpr uint32_t WhoAmI        = IdRev;
    constexpr uint32_t Version       = Vendor;
    constexpr uint32_t Status        = Led;
    constexpr uint32_t UptimeUs      = TimeLow;
} // namespace reg::sys

// IMU Auto-DMA BAR Register Offsets (abstractx-fpga/cores/imu_auto_dma.json)
namespace reg::imu {
    constexpr uint32_t Ctrl          = 0x00; // Bit 0: Enable, Bit 1: Continuous Read, Bit 2: IRQ Enable
    constexpr uint32_t Addr          = 0x04; // Target SPI starting register (e.g. 0x1D)
    constexpr uint32_t Len           = 0x08; // Burst length (e.g. 14 bytes)
    constexpr uint32_t WData         = 0x0C; // Write data
    constexpr uint32_t RData         = 0x10; // Read data
    constexpr uint32_t Status        = 0x1C; // Status register
    // Backward-compatibility aliases
    constexpr uint32_t Control       = Ctrl;
    constexpr uint32_t ContinuousAddr = Addr;
    constexpr uint32_t ContinuousLen  = Len;
} // namespace reg::imu

// ESC DShot BAR Register Offsets (abstractx-fpga/cores/dshot.json)
namespace reg::esc {
    constexpr uint32_t Ctrl          = 0x00; // Mode & prescaler configuration
    constexpr uint32_t Enable        = 0x04; // Channel enable bitmask (bits 0..3)
    constexpr uint32_t Throttle0     = 0x10; // DShot throttle Motor 0 (0..2047, offset 16)
    constexpr uint32_t Throttle1     = 0x14; // DShot throttle Motor 1 (0..2047, offset 20)
    constexpr uint32_t Throttle2     = 0x18; // DShot throttle Motor 2 (0..2047, offset 24)
    constexpr uint32_t Throttle3     = 0x1C; // DShot throttle Motor 3 (0..2047, offset 28)
    // Backward-compatibility aliases
    constexpr uint32_t Control       = Ctrl;
    constexpr uint32_t Status        = Enable;
    constexpr uint32_t Motor1        = Throttle0;
    constexpr uint32_t Motor2        = Throttle1;
    constexpr uint32_t Motor3        = Throttle2;
    constexpr uint32_t Motor4        = Throttle3;
} // namespace reg::esc

// PWM Decoder BAR Register Offsets (abstractx-fpga/cores/pwm_decoder.json)
namespace reg::pwm {
    constexpr uint32_t Ctrl          = 0x00;
    constexpr uint32_t Ch1           = 0x04;
    constexpr uint32_t Ch2           = 0x08;
    constexpr uint32_t Ch3           = 0x0C;
    constexpr uint32_t Ch4           = 0x10;
} // namespace reg::pwm

// NeoPixel BAR Register Offsets (abstractx-fpga/cores/neopixel.json)
namespace reg::neopixel {
    constexpr uint32_t Ctrl          = 0x00;
} // namespace reg::neopixel

} // namespace abstractx

#endif // PCIE_BAR_MAP_HPP
