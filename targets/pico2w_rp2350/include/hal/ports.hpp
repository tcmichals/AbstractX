// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// Pico 2 W (RP2350) port table: the controller instances and pins the drivers use ([SPEC-HAL-15]).
// @impl [SPEC-PICO-HAL-04] targets/pico2w_rp2350/SPECIFICATION.md
#pragma once

#include <cstdint>

namespace hal::ports {

struct PicoUartPort { uint8_t instance; uint8_t tx_pin; uint8_t rx_pin; const char* name; };
struct PicoI2cPort  { uint8_t instance; uint8_t sda_pin; uint8_t scl_pin; const char* name; };
struct PicoSpiPort  { uint8_t instance; uint8_t sck_pin; uint8_t mosi_pin; uint8_t miso_pin; uint8_t cs_pin; const char* name; };

inline constexpr PicoUartPort kUart0{0, 0, 1, "uart0"};          // GPS ([SPEC-PICO-HAL-03]); UART1 GP8/GP9 is stdio
inline constexpr PicoI2cPort  kI2c0{0, 4, 5, "i2c0"};
inline constexpr PicoSpiPort  kSpi1{1, 10, 11, 12, 13, "spi1"};  // CS0 GP13, the only wired chip select ([SPEC-PICO-HAL-02])

} // namespace hal::ports
