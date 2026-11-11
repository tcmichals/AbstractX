// @verifies [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md (fake FreeRTOS software timer API for run_start / run_pass, sim/test_freertos_host_xtimer.cpp)
/* SPDX-License-Identifier: GPL-3.0-or-later. Fake FreeRTOS software timer API, see FreeRTOS.h here. */
#ifndef ABX_FAKE_TIMERS_H
#define ABX_FAKE_TIMERS_H

#include "FreeRTOS.h"

typedef struct fake_timer* TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t);
typedef struct { void* pad[8]; } StaticTimer_t;

TimerHandle_t xTimerCreateStatic(const char* name, TickType_t period, UBaseType_t auto_reload,
                                 void* id, TimerCallbackFunction_t cb, StaticTimer_t* buffer);
BaseType_t xTimerChangePeriod(TimerHandle_t timer, TickType_t period, TickType_t block);
BaseType_t xTimerStop(TimerHandle_t timer, TickType_t block);
BaseType_t xTimerIsTimerActive(TimerHandle_t timer);

#endif
