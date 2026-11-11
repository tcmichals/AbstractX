// @verifies [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md (fake FreeRTOS task notification API for run_start / run_pass, sim/test_freertos_host_xtimer.cpp)
/* SPDX-License-Identifier: GPL-3.0-or-later. Fake FreeRTOS task API, see FreeRTOS.h here. */
#ifndef ABX_FAKE_TASK_H
#define ABX_FAKE_TASK_H

#include "FreeRTOS.h"

typedef struct fake_task* TaskHandle_t;

TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t* woken);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks_to_wait);
TickType_t xTaskGetTickCount(void);
TickType_t xTaskGetTickCountFromISR(void);
void fake_task_yield(void);
#define taskYIELD() fake_task_yield()

#endif
