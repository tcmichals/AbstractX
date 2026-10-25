/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * freertos_driver_wrapper.hpp - Synchronous FreeRTOS task wrappers around AbstractX HAL drivers.
 * @impl [SPEC-E906-RTOS-08] apps/e906_freertos_app/SPECIFICATION.md
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}

namespace abstractx::freertos {

/**
 * @brief Helper for wrapping asynchronous AbstractX drivers into synchronous,
 *        blocking FreeRTOS calls without busy-waiting or thread-polling.
 *
 * The calling FreeRTOS task yields execution via ulTaskNotifyTake() while the
 * hardware DMA or ISR runs. On completion, the ISR wakes the task with
 * vTaskNotifyGiveFromISR().
 */
class TaskNotifier {
public:
    TaskNotifier() noexcept = default;

    void prepare() noexcept {
        waiting_task_ = xTaskGetCurrentTaskHandle();
    }

    bool wait(TickType_t timeout_ticks) noexcept {
        if (!waiting_task_) return false;
        const uint32_t count = ulTaskNotifyTake(pdTRUE, timeout_ticks);
        waiting_task_ = nullptr;
        return count > 0;
    }

    void notify_from_isr() noexcept {
        if (waiting_task_) {
            BaseType_t woken = pdFALSE;
            vTaskNotifyGiveFromISR(waiting_task_, &woken);
            portYIELD_FROM_ISR(woken);
        }
    }

private:
    TaskHandle_t waiting_task_{nullptr};
};

} // namespace abstractx::freertos
