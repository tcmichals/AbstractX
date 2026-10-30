/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host shim for E906 target code under test: the real memory map, with the SPI1 and PIO
 * register blocks moved into plain arrays the test owns ([SPEC-E906-HAL-03]).
 */
// @verifies [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md
#pragma once
#include "../../targets/allwinner_e906/include/memory_map.h"

extern unsigned char g_fake_spi1_regs[0x400];
extern unsigned char g_fake_pio_regs[0x400];
#undef SPI1_BASE
#undef PIO_BASE
#define SPI1_BASE (reinterpret_cast<uintptr_t>(g_fake_spi1_regs))
#define PIO_BASE (reinterpret_cast<uintptr_t>(g_fake_pio_regs))
