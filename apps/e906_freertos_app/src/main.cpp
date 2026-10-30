/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * main.cpp - AbstractX as one FreeRTOS task on the XuanTie E906.
 * @impl [SPEC-E906-RTOS-03] [SPEC-E906-RTOS-04] [SPEC-E906-RTOS-05] [SPEC-E906-RTOS-06] apps/e906_freertos_app/SPECIFICATION.md
 */
#include "abstractx/abstractx.hpp"
#include "abstractx/trace/event_trace.hpp"
#include "bench.hpp"
#include "hal/clic.hpp"
#include "hal/msgbox.hpp"
#include "hal/trace.hpp"

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
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
volatile bool s_soft_irq_on = false;      // shared: set before the soft line is enabled, interrupt handler reads; single hart

void notify_from_isr() noexcept {
    if (s_ax_task) {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_ax_task, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

abstractx::coro::Task<void> bench_main() {
    abstractx::spawn(e906_bench::rx_task(abstractx::hal::g_rpmsg));
    co_await e906_bench::worker_task(abstractx::hal::g_rpmsg);
}

// [SPEC-E906-RTOS-05] the only context that calls AbstractX
void abstractx_task(void *) {
    abstractx::spawn(bench_main());
    for (;;) {
        abstractx::step();
        if (!abstractx::Dispatcher::has_pending_resumes()) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
    }
}

} // namespace

extern "C" {

const char *abx_bench_platform_name(void) { return "freertos"; }

// @impl [SPEC-E906-RTOS-07] apps/e906_freertos_app/SPECIFICATION.md
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

bool abx_bench_soft_irq_enable(void) {
    s_soft_irq_on = true;            // before the line: a pending soft IRQ must not count as unexpected
    clic_enable(kLineSoft, true);
    return true;
}

bool abx_bench_idle_stats(uint32_t *wakes, uint64_t *idle_cycles) {
    taskENTER_CRITICAL();
    *wakes = s_idle_wakes;
    *idle_cycles = s_idle_cycles;
    taskEXIT_CRITICAL();
    return true;
}

// [SPEC-E906-RTOS-04] [SPEC-E906-RTOS-06] integer only; mcause already masked to 0x80000FFF
void freertos_risc_v_application_interrupt_handler(uint32_t mcause) {
    const uint32_t line = mcause & 0xFFFu;
    if (line == kLineMsgbox) {
        // Level triggered: mask until the RPMsg awaiter has drained and re-enables it.
        hal::MsgBox::enable_rx_irq(hal::MsgBox::Channel::Channel1, false);
        fc_msgbox_doorbell_isr();
        notify_from_isr();
        return;
    }
    if (line == kLineSoft && s_soft_irq_on) {
        abx_bench_soft_isr();
        notify_from_isr();
        return;
    }
    // DMA (88) and attached lines (PI8 DRDY on 100): the target's table, which disables a line
    // it does not own. A DMA completion may have posted the consumer, so wake the task.
    if (line != kLineTimer && hal::irq::dispatch(line)) {
        notify_from_isr();
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

void vApplicationIdleHook(void) {
    const uint64_t t0 = abx_bench_mcycle();
    __asm__ volatile("wfi");
    const uint64_t t1 = abx_bench_mcycle();
    taskENTER_CRITICAL();
    s_idle_wakes = s_idle_wakes + 1u;
    s_idle_cycles = s_idle_cycles + (t1 - t0);
    taskEXIT_CRITICAL();
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
    abstractx::init();
    e906_bench::init(abstractx::hal::g_rpmsg);

    // [SPEC-E906-RTOS-03] masked until the scheduler starts the first task
    portDISABLE_INTERRUPTS();
    const uintptr_t vec = reinterpret_cast<uintptr_t>(&freertos_risc_v_trap_handler);
    __asm__ volatile("csrw mtvec, %0" ::"r"(vec | 0x3u));
    clic_enable(kLineTimer, true);
    clic_enable(kLineMsgbox, true);

    s_ax_task = xTaskCreateStatic(abstractx_task, "abstractx", kAbstractxStackWords, nullptr,
                                  configMAX_PRIORITIES - 1, s_ax_stack, &s_ax_tcb);
    vTaskStartScheduler();
    for (;;) {
        __asm__ volatile("wfi");
    }
}
