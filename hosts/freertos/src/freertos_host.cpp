/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * @impl [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md (FreeRTOS host, one copy)
 */
#include "abstractx/host/freertos_host.hpp"

#include <atomic>
#include "abstractx/abstractx.hpp"

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
}

#ifndef ABX_FREERTOS_TIMER_MODE
#define ABX_FREERTOS_TIMER_MODE 0
#endif

namespace abstractx::freertos_host {
namespace {

TaskHandle_t s_task = nullptr;  // shared: run() writes before set_wake(); wake hooks read after

void wake_task() noexcept {
    // A post from the AbstractX task itself needs no notification: step() sees it.
    if (s_task && xTaskGetCurrentTaskHandle() != s_task) xTaskNotifyGive(s_task);
}

void wake_isr() noexcept {
    if (s_task) {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_task, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

// Kernel ticks until deadline_us, rounded up; 0 when due, portMAX_DELAY when kNever.
TickType_t ticks_until(uint64_t deadline_us) noexcept {
    if (deadline_us == kNever) return portMAX_DELAY;
    const uint64_t now = hal::get_timer_driver().get_time_us();
    if (deadline_us <= now) return 0;
    const uint64_t n = ((deadline_us - now) * configTICK_RATE_HZ + 999'999u) / 1'000'000u;
    return n >= portMAX_DELAY ? static_cast<TickType_t>(portMAX_DELAY - 1u) : static_cast<TickType_t>(n);
}

std::atomic<uint32_t> s_cmd_failures{0};  // shared: AbstractX task writes, anyone reads; atomic

#if ABX_FREERTOS_TIMER_MODE == 1
// shared: AbstractX task arms / stops it; the timer service task runs the callback; kernel-owned
StaticTimer_t s_timer_buf;
TimerHandle_t s_timer = nullptr;
uint64_t s_armed_us = kNever;  // AbstractX task only

void timer_cb(TimerHandle_t) { wake_task(); }

void arm(uint64_t deadline_us, TickType_t n) noexcept {
    if (deadline_us == kNever) {
        if (s_armed_us != kNever && xTimerStop(s_timer, 0) != pdPASS) {
            s_cmd_failures.fetch_add(1, std::memory_order_relaxed);
        }
        s_armed_us = kNever;
        return;
    }
    if (deadline_us == s_armed_us && xTimerIsTimerActive(s_timer) != pdFALSE) return;
    if (xTimerChangePeriod(s_timer, n, 0) == pdPASS) {
        s_armed_us = deadline_us;
    } else {
        s_cmd_failures.fetch_add(1, std::memory_order_relaxed);
        s_armed_us = kNever;  // retried on the next pass
    }
}
#elif ABX_FREERTOS_TIMER_MODE == 2
// shared: AbstractX task writes the deadline then sets armed (release); tick hook reads (acquire)
std::atomic<TickType_t> s_deadline_tick{0};
std::atomic<bool> s_armed{false};

void arm(uint64_t deadline_us, TickType_t n) noexcept {
    if (deadline_us == kNever) {
        s_armed.store(false, std::memory_order_release);
        return;
    }
    s_deadline_tick.store(xTaskGetTickCount() + n, std::memory_order_relaxed);
    s_armed.store(true, std::memory_order_release);
}
#endif

} // namespace

[[noreturn]] void run() noexcept {
    s_task = xTaskGetCurrentTaskHandle();
#if ABX_FREERTOS_TIMER_MODE == 1
    s_timer = xTimerCreateStatic("abx", 1, pdFALSE, nullptr, timer_cb, &s_timer_buf);
    if (s_timer == nullptr) s_cmd_failures.fetch_add(1, std::memory_order_relaxed);
#endif
    set_wake({&wake_task, &wake_isr});
    for (;;) {
        const uint64_t next = step();
        if (next == kNow) {
            // A coroutine is ready: let equal-priority tasks run, then continue (no fixed block,
            // which would add a tick to every wake). longest_ready_run() exposes a hog.
            taskYIELD();
            continue;
        }
        const TickType_t n = ticks_until(next);
        if (n == 0) continue;  // already due
#if ABX_FREERTOS_TIMER_MODE == 0
        (void)ulTaskNotifyTake(pdTRUE, n);  // an early wake just loops: step() decides what is due
#else
        arm(next, n);
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#endif
    }
}

void tick_from_isr() noexcept {
#if ABX_FREERTOS_TIMER_MODE == 2
    if (!s_armed.load(std::memory_order_acquire)) return;
    const TickType_t now = xTaskGetTickCountFromISR();
    const TickType_t due = s_deadline_tick.load(std::memory_order_relaxed);
    if (static_cast<TickType_t>(now - due) < (portMAX_DELAY >> 1)) {  // now >= due, wrap-safe
        s_armed.store(false, std::memory_order_relaxed);
        if (s_task) vTaskNotifyGiveFromISR(s_task, nullptr);  // the kernel switches at tick end
    }
#endif
}

uint32_t timer_cmd_failures() noexcept { return s_cmd_failures.load(std::memory_order_relaxed); }

} // namespace abstractx::freertos_host
