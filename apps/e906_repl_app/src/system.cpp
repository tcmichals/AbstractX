/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * system.cpp - REPL wait, interrupt statistics, sleep and the DMA bring-up measurement.
 *
 * @impl [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
 * @impl [SPEC-E906-IRQ-02] targets/allwinner_e906/SPECIFICATION.md
 */

#include "abstractx/abstractx.hpp"
#include "abstractx/hal/platform.hpp"
#include "hal/clic.hpp"
#include "hal/dma.hpp"
#include "hal/rpmsg.hpp"
#include "hal/timer.hpp"
#include "repl_wait.hpp"

#include <cstddef>
#include <cstdint>

extern "C" void abstractx_repl_vm_hook(void);

// ---------------------------------------------------------------------------
// Common wait: run the VM hook (Ctrl-C, RPMsg), then sleep until any interrupt
// or `deadline` (mtime ticks). Never reads a peripheral register.
// ---------------------------------------------------------------------------
void repl_wait_step(uint64_t deadline) noexcept {
    hal::Timer::arm_wake(deadline);
    abstractx_repl_vm_hook();
    abstractx::hal::platform_idle_wait();
    abstractx::status_service();  // reports keep coming while a script waits [SPEC-E906-REPL-18]
}

extern "C" {

// ---------------------------------------------------------------------------
// Interrupt statistics ([SPEC-E906-REPL-16])
// ---------------------------------------------------------------------------
size_t e906_irq_stats(uint32_t* lines, uint32_t* counts, size_t max) {
    size_t n = 0;
    for (uint32_t line = 0; line < hal::clic::kNumLines && n < max; ++line) {
        const uint32_t c = hal::irq::count(line);
        if (c) {
            lines[n] = line;
            counts[n] = c;
            ++n;
        }
    }
    return n;
}

uint32_t e906_irq_spurious(void) { return hal::irq::spurious(); }
uint32_t e906_rpmsg_doorbells(void) { return hal::rpmsg_doorbell_irqs(); }

uint32_t e906_mtvec(void) {
    uint32_t v = 0;
#if defined(__riscv)
    asm volatile("csrr %0, mtvec" : "=r"(v));
#endif
    return v;
}

// Sleep `ms` with wfi; the timer interrupt ends it, the VM hook runs on every wake.
void e906_sleep_ms(uint32_t ms) {
    const uint64_t deadline = hal::Timer::get_ticks() + static_cast<uint64_t>(ms) * hal::Timer::TICKS_PER_MS;
    while (hal::Timer::get_ticks() < deadline) {
        repl_wait_step(deadline);
    }
}

// ---------------------------------------------------------------------------
// DMA bring-up measurement ([SPEC-E906-HAL-01]): SRAM Space 0 -> Space 1 copy on an E906
// channel, completion on CLIC line 88, timed against a CPU copy.
// @impl [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md
// ---------------------------------------------------------------------------
namespace {
uint8_t s_dma_src[1024];                                       // .bss, SRAM Space 0
__attribute__((section(".sram_c"))) uint8_t s_dma_dst[1024];   // SRAM Space 1
__attribute__((section(".sram_c"))) hal::DmaLli s_dma_lli;
volatile bool s_dma_done = false;  // shared: main clears, DMA ISR sets last; main polls with a deadline; single hart
volatile bool s_dma_ok = false;  // shared: DMA ISR writes before s_dma_done, main reads after it; single hart, volatile order
volatile uint32_t s_dma_end = 0;  // shared: as s_dma_ok

uint32_t mcycle32() noexcept {
    uint32_t v = 0;
#if defined(__riscv)
    asm volatile("csrr %0, mcycle" : "=r"(v));
#endif
    return v;
}

void dma_bench_done(uint8_t, bool ok) noexcept {
    s_dma_end = mcycle32();
    s_dma_ok = ok;
    s_dma_done = true;
}
} // namespace

// Returns 1 when the DMA copy completed and the data matches, 0 on mismatch, -1 on timeout,
// -2 when no channel is free. Releases the allocated DMA channel before returning.
int e906_dma_bench(uint32_t n, uint32_t port, uint32_t* dma_cycles, uint32_t* cpu_cycles) {
    if (n == 0 || n > sizeof(s_dma_src)) return 0;
    static int s_bench_ch = -1;
    if (s_bench_ch >= 0) {
        hal::DmaController::release_channel(static_cast<uint8_t>(s_bench_ch));
        s_bench_ch = -1;
    }
    hal::DmaController::init();
    const int ch = hal::DmaController::allocate_channel();
    if (ch < 0) return -2;
    s_bench_ch = ch;

    for (uint32_t i = 0; i < n; ++i) {
        s_dma_src[i] = static_cast<uint8_t>(i * 7U + 3U);
        s_dma_dst[i] = 0;
    }
    const uint8_t mem = static_cast<uint8_t>(port & 0x3FU);
    s_dma_lli.cfg = hal::DmaController::build_cfg(mem, hal::DMA_BURST_8, hal::DMA_WIDTH_32, hal::DMA_MODE_LINEAR,
                                                  mem, hal::DMA_BURST_8, hal::DMA_WIDTH_32, hal::DMA_MODE_LINEAR);
    s_dma_lli.src = hal::DmaController::pa(s_dma_src);
    s_dma_lli.dst = hal::DmaController::pa(s_dma_dst);
    s_dma_lli.len = n;
    s_dma_lli.para = 0;
    s_dma_lli.p_lli_next = hal::LLI_LAST_ITEM;
    s_dma_done = false;
    s_dma_ok = false;
    const uint32_t start = mcycle32();
    hal::DmaController::start_transfer(static_cast<uint8_t>(ch), &s_dma_lli,
                                       hal::DmaController::CompletionCallback::create<dma_bench_done>());
    const uint64_t deadline = hal::Timer::get_ticks() + 20ULL * hal::Timer::TICKS_PER_MS;
    while (!s_dma_done && hal::Timer::get_ticks() < deadline) {
        repl_wait_step(deadline);
    }
    s_bench_ch = -1;
    if (!s_dma_done) {
        hal::DmaController::release_channel(static_cast<uint8_t>(ch));
        return -1;
    }
    hal::DmaController::release_channel(static_cast<uint8_t>(ch));
    *dma_cycles = s_dma_end - start;
    int ok = s_dma_ok ? 1 : 0;
    for (uint32_t i = 0; i < n && ok; ++i) {
        if (s_dma_dst[i] != s_dma_src[i]) ok = 0;
    }
    const uint32_t c0 = mcycle32();
    __builtin_memcpy(s_dma_dst, s_dma_src, n);
    *cpu_cycles = mcycle32() - c0;
    return ok;
}

} // extern "C"
