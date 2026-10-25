/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * main.cpp - Bare-metal AbstractX runtime benchmark for the XuanTie E906.
 * @impl [SPEC-E906-BENCH-01] apps/e906_bench/SPECIFICATION.md
 */
#include "abstractx/abstractx.hpp"
#include "bench.hpp"
#include "hal/clic.hpp"
#include "hal/trace.hpp"

namespace abstractx::hal {
extern ::hal::Rpmsg g_rpmsg;
}

// @impl [SPEC-E906-BENCH-03] apps/e906_bench/SPECIFICATION.md
extern "C" bool abx_bench_soft_irq_enable(void) {
    return hal::irq::attach(3, [](void*) { abx_bench_soft_isr(); }, nullptr);
}

static abstractx::coro::Task<void> bench_main() {
    abstractx::spawn(e906_bench::rx_task(abstractx::hal::g_rpmsg));
    co_await e906_bench::worker_task(abstractx::hal::g_rpmsg);
}

int main() {
    hal::Trace::init(false);
    hal::Trace::puts("[E906] Online (bench, bare metal)\n");
    abstractx::init();
    e906_bench::init(abstractx::hal::g_rpmsg);
    // Interrupt driven: run() sleeps in wfi when no coroutine is ready.
    abstractx::run(bench_main());
    return 0;
}

