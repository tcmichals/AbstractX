/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host shim: the E906 CLINT mtime / mtimecmp registers moved into plain arrays the test owns
 * ([SPEC-E906-IRQ-03]); force-included before timer.cpp by sim/CMakeLists.txt.
 */
// @verifies [SPEC-E906-IRQ-03] targets/allwinner_e906/SPECIFICATION.md (Timer::handle_irq, sim/test_e906_timer_irq.cpp)
#pragma once
#include <stdint.h>

extern volatile uint32_t g_fake_mtime[2];
extern volatile uint32_t g_fake_mtimecmp[2];
#define E906_MTIME_BASE (reinterpret_cast<uintptr_t>(g_fake_mtime))
#define E906_MTIMECMP_BASE (reinterpret_cast<uintptr_t>(g_fake_mtimecmp))
