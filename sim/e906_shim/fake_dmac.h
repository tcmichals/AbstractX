/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host shim: the E906 DMAC register block moved into a plain array the test owns
 * ([SPEC-E906-HAL-01]); force-included before hal/dma.hpp by sim/CMakeLists.txt.
 */
// @verifies [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md (DmaController::handle_irq, sim/test_e906_dma_irq.cpp)
#pragma once
#include <stdint.h>

extern unsigned char g_fake_dma_regs[0x800];
#define SUNXI_DMA_BASE (reinterpret_cast<uintptr_t>(g_fake_dma_regs))
