/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * bench.hpp - E906 runtime benchmark shared by bare metal and FreeRTOS builds.
 * @impl [SPEC-E906-BENCH-01] apps/e906_bench/SPECIFICATION.md
 */
#pragma once

#include <cstdint>
#include "asp_coro.hpp"
#include "hal/rpmsg.hpp"

namespace e906_bench {

constexpr uint32_t ENDPOINT_ADDR = 1030;
constexpr const char *SERVICE_NAME = "abstractx-bench";

// Register the endpoint; call once after abstractx::init().
void init(::hal::IRpmsg &rpmsg) noexcept;

// Receives RPMsg (interrupt driven) and announces the service.
abstractx::coro::Task<void> rx_task(::hal::IRpmsg &rpmsg) noexcept;

// Answers echo / run / info requests.
abstractx::coro::Task<void> worker_task(::hal::IRpmsg &rpmsg) noexcept;

} // namespace e906_bench

// Runtime hooks ([SPEC-E906-BENCH-01]); the bench provides weak defaults.
extern "C" {
const char *abx_bench_platform_name(void);
// Route CLIC line 3 to abx_bench_soft_isr() and enable it; false if unsupported.
bool abx_bench_soft_irq_enable(void);
// Idle accounting since boot; false if the runtime is not instrumented.
bool abx_bench_idle_stats(uint32_t *wakes, uint64_t *idle_cycles);
// Line 3 handler ([SPEC-E906-BENCH-03]); called by the runtime's trap dispatch.
void abx_bench_soft_isr(void);
uint64_t abx_bench_mcycle(void);
}
