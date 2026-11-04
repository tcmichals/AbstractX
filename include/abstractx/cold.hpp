/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ABSTRACTX_COLD: code that is not on a real-time path (cancellation, the status report). On the
 * E906 the linker places .text.abx_cold in .text_cold (SRAM Space 1), not the fast SRAM
 * ([SPEC-E906-MEM-01]); elsewhere it is ordinary code marked cold.
 * @impl [SPEC-E906-MEM-01] targets/allwinner_e906/SPECIFICATION.md (second code region)
 */
#ifndef ABSTRACTX_COLD_HPP
#define ABSTRACTX_COLD_HPP

#ifndef ABSTRACTX_COLD
#define ABSTRACTX_COLD __attribute__((cold, noinline, section(".text.abx_cold")))
#endif

#endif // ABSTRACTX_COLD_HPP
