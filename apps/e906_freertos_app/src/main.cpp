/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * main.cpp - AbstractX as one FreeRTOS task on the XuanTie E906.
 * @impl [SPEC-E906-RTOS-03] [SPEC-E906-RTOS-04] [SPEC-E906-RTOS-05] [SPEC-E906-RTOS-06] apps/e906_freertos_app/SPECIFICATION.md
 */
#include <atomic>
#include <cstdint>
#include "abstractx/abstractx.hpp"
#include "abstractx/host/freertos_host.hpp"
#include "abstractx/trace/event_trace.hpp"
#include "bench.hpp"
#include "hal/clic.hpp"
#include "hal/msgbox.hpp"
#include "hal/timer.hpp"
#include "hal/trace.hpp"

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
extern "C" {
extern uint64_t ullNextTime;                    // FreeRTOS RISC-V port.c: next tick compare value
extern const size_t uxTimerIncrementsForOneTick;  // port.c: mtime ticks per kernel tick
}
}

namespace abstractx::hal {
extern ::hal::Rpmsg g_rpmsg;
}

extern "C" void fc_msgbox_doorbell_isr() noexcept;    // targets/allwinner_e906/src/rpmsg.cpp
extern "C" void freertos_risc_v_trap_handler(void);  // portASM_e906.S

namespace {

// [SPEC-E906-RTOS-03] T-Head CLIC: control word per line, byte +1 = interrupt enable
constexpr uintptr_t kClicIntBase = 0xE0801000u;
constexpr uint32_t kLineSoft = 3;
constexpr uint32_t kLineTimer = 7;
constexpr uint32_t kLineMsgbox = 17;

inline void clic_enable(uint32_t line, bool on) noexcept {
    *reinterpret_cast<volatile uint8_t *>(kClicIntBase + 4u * line + 1u) = on ? 1u : 0u;
}

constexpr uint32_t kAbstractxStackWords = 2048;  // 8 KB
StaticTask_t s_ax_tcb;
StackType_t s_ax_stack[kAbstractxStackWords];
TaskHandle_t s_ax_task = nullptr;

volatile uint32_t s_unexpected_irqs = 0;  // shared: interrupt handler only writes (statistics); single hart
volatile uint32_t s_idle_wakes = 0;       // shared: idle hook writes, abx_bench_idle_stats reads; both inside taskENTER_CRITICAL
volatile uint64_t s_idle_cycles = 0;      // shared: as s_idle_wakes (64-bit, so only inside taskENTER_CRITICAL)

// [SPEC-E906-BENCH-06] native cyclic task state.
// shared: abstractx task writes the parameters before xTaskNotifyGive, cyclic task reads after the
// notify; results the other way, published with s_cyc_done (release / acquire); single hart
StaticTask_t s_cyc_tcb;
StackType_t s_cyc_stack[512];
TaskHandle_t s_cyc_task = nullptr;
uint32_t s_cyc_count = 0;
uint32_t *s_cyc_hist = nullptr;
uint32_t s_cyc_buckets = 0;
AbxNativeCyclic s_cyc_res{};
std::atomic<bool> s_cyc_done{false};

void cyclic_task(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        AbxNativeCyclic r{0, 0, UINT64_MAX, 0, 0};
        TickType_t last = xTaskGetTickCount();
        for (uint32_t i = 0; i < s_cyc_count; ++i) {
            vTaskDelayUntil(&last, 1);
            const uint64_t now = hal::Timer::get_ticks();
            const uint64_t tick = ullNextTime - uxTimerIncrementsForOneTick;  // the tick that woke us
            const uint64_t late = now > tick ? now - tick : 0;
            if (late < r.min_ticks) r.min_ticks = late;
            if (late > r.max_ticks) r.max_ticks = late;
            r.sum_ticks += late;
            ++r.done;
            if (late > 900ULL * hal::Timer::TICKS_PER_US) ++r.over_tick;
            const uint64_t us = late / hal::Timer::TICKS_PER_US;
            if (us < s_cyc_buckets) ++s_cyc_hist[us];
        }
        s_cyc_res = r;
        s_cyc_done.store(true, std::memory_order_release);
    }
}

abstractx::coro::Task<void> bench_main() {
    abstractx::spawn(e906_bench::rx_task(abstractx::hal::g_rpmsg));
    co_await e906_bench::worker_task(abstractx::hal::g_rpmsg);
}

// [SPEC-E906-RTOS-05] the only context that calls AbstractX; the shared host owns the loop
// @impl [SPEC-E906-RTOS-03] [SPEC-E906-RTOS-05] apps/e906_freertos_app/SPECIFICATION.md
void abstractx_task(void *) {
    abstractx::spawn(bench_main());
    abstractx::freertos_host::run();
}

} // namespace

extern "C" {

const char *abx_bench_platform_name(void) { return "freertos"; }

// [SPEC-E906-BENCH-06] native FreeRTOS cyclic run: a plain task on vTaskDelayUntil, lateness
// against the tick that woke it (the RISC-V port's ullNextTime minus one tick).

// @impl [SPEC-E906-BENCH-06] apps/e906_bench/SPECIFICATION.md (native FreeRTOS task)
bool abx_bench_native_cyclic_start(uint32_t count, uint32_t *hist, uint32_t buckets) {
    if (s_cyc_task == nullptr || hist == nullptr) return false;
    s_cyc_count = count;
    s_cyc_hist = hist;
    s_cyc_buckets = buckets;
    s_cyc_done.store(false, std::memory_order_release);
    xTaskNotifyGive(s_cyc_task);
    return true;
}

bool abx_bench_native_cyclic_poll(AbxNativeCyclic *out) {
    if (!s_cyc_done.load(std::memory_order_acquire)) return false;
    *out = s_cyc_res;
    return true;
}

// @impl [SPEC-E906-RTOS-07] apps/e906_freertos_app/SPECIFICATION.md

#if ABX_FREERTOS_DEBUG
static uint64_t s_task_in_cycles = 0;

void abx_trace_task_switched_in(void) {
    s_task_in_cycles = abx_bench_mcycle();
}

void abx_trace_task_switched_out(void) {
    const uint64_t now = abx_bench_mcycle();
    if (s_task_in_cycles != 0 && now >= s_task_in_cycles) {
        const uint64_t duration = now - s_task_in_cycles;
        TaskHandle_t cur = xTaskGetCurrentTaskHandle();
        const uint32_t taskId = cur ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cur)) : 0;
        abstractx::events::span(abstractx::events::kResume, taskId, s_task_in_cycles, duration);
    }
}
#endif

// @impl [SPEC-E906-BENCH-03] apps/e906_bench/SPECIFICATION.md (same dispatch path as bare metal)
bool abx_bench_soft_irq_enable(void) {
    return hal::irq::attach(kLineSoft, [](void*) { abx_bench_soft_isr(); }, nullptr);
}

bool abx_bench_idle_stats(uint32_t *wakes, uint64_t *idle_cycles) {
#if ABX_FREERTOS_DEBUG
    taskENTER_CRITICAL();
    *wakes = s_idle_wakes;
    *idle_cycles = s_idle_cycles;
    taskEXIT_CRITICAL();
    return true;
#else
    (void)wakes;
    (void)idle_cycles;
    return false;  // [SPEC-E906-BENCH-06] R reports idle=not-instrumented
#endif
}

// [SPEC-E906-RTOS-04] [SPEC-E906-RTOS-06] integer only; mcause already masked to 0x80000FFF
void freertos_risc_v_application_interrupt_handler(uint32_t mcause) {
    const uint32_t line = mcause & 0xFFFu;
    if (line == kLineMsgbox) {
        // Level triggered: mask until the RPMsg awaiter has drained and re-enables it.
        hal::MsgBox::enable_rx_irq(hal::MsgBox::Channel::Channel1, false);
        (void)hal::irq::dispatch(line);  // fc_msgbox_doorbell_isr() with the bare-metal accounting
        abstractx::wake_from_isr();  // [SPEC-ARCH-15] one wake route
        return;
    }
    // The soft line (3) is attached (abx_bench_soft_irq_enable) and taken below like any other.
    // @impl [SPEC-E906-BENCH-03] apps/e906_bench/SPECIFICATION.md
    // DMA (88) and attached lines (PI8 DRDY on 100): the target's table, which disables a line
    // it does not own. A DMA completion may have posted the consumer, so wake the task.
    if (line != kLineTimer && hal::irq::dispatch(line)) {
        abstractx::wake_from_isr();  // [SPEC-ARCH-15] one wake route
        return;
    }
    s_unexpected_irqs = s_unexpected_irqs + 1u;
    clic_enable(line, false);  // a level line must not storm
}

void freertos_risc_v_application_exception_handler(uint32_t mcause) {
    (void)mcause;
    hal::Trace::puts("[E906] FATAL exception (freertos)\n");
    taskDISABLE_INTERRUPTS();
    for (;;) {
        __asm__ volatile("wfi");
    }
}

#if ABX_FREERTOS_TIMER_MODE == 2
// @impl [SPEC-E906-RTOS-03] apps/e906_freertos_app/SPECIFICATION.md (opt-in TICK_HOOK mode)
void vApplicationTickHook(void) { abstractx::freertos_host::tick_from_isr(); }
#endif

void vApplicationIdleHook(void) {
#if ABX_FREERTOS_DEBUG
    const uint64_t t0 = abx_bench_mcycle();
    __asm__ volatile("wfi");
    const uint64_t t1 = abx_bench_mcycle();
    taskENTER_CRITICAL();
    s_idle_wakes = s_idle_wakes + 1u;
    s_idle_cycles = s_idle_cycles + (t1 - t0);
    taskEXIT_CRITICAL();
#else
    __asm__ volatile("wfi");  // [SPEC-E906-BENCH-06] timing build: sleep only
#endif
}

void vApplicationStackOverflowHook(TaskHandle_t, char *) {
    hal::Trace::puts("[E906] FATAL stack overflow (freertos)\n");
    taskDISABLE_INTERRUPTS();
    for (;;) {
        __asm__ volatile("wfi");
    }
}

} // extern "C"

int main() {
    hal::Trace::init(false);
    hal::Trace::puts("[E906] Online (freertos)\n");
    hal::Timer::use_external_tick();  // [SPEC-E906-RTOS-03] the kernel owns mtimecmp
    abstractx::init();
    e906_bench::init(abstractx::hal::g_rpmsg);

    // [SPEC-E906-RTOS-03] masked until the scheduler starts the first task
    portDISABLE_INTERRUPTS();
    const uintptr_t vec = reinterpret_cast<uintptr_t>(&freertos_risc_v_trap_handler);
    __asm__ volatile("csrw mtvec, %0" ::"r"(vec | 0x3u));
    clic_enable(kLineTimer, true);
    clic_enable(kLineMsgbox, true);

    s_cyc_task = xTaskCreateStatic(cyclic_task, "cyclic", 512, nullptr, configMAX_PRIORITIES - 1,
                                   s_cyc_stack, &s_cyc_tcb);  // [SPEC-E906-BENCH-06]
    s_ax_task = xTaskCreateStatic(abstractx_task, "abstractx", kAbstractxStackWords, nullptr,
                                  configMAX_PRIORITIES - 1, s_ax_stack, &s_ax_tcb);
    vTaskStartScheduler();
    for (;;) {
        __asm__ volatile("wfi");
    }
}
