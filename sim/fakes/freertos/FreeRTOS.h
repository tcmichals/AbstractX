// @verifies [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md (fake FreeRTOS types, constants, configASSERT for run_start / run_pass, sim/test_freertos_host_xtimer.cpp)
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fake FreeRTOS API for the host test of hosts/freertos (sim/test_freertos_host_xtimer.cpp):
 * only what freertos_host.cpp uses. The test defines the functions and controls their results.
 */
#ifndef ABX_FAKE_FREERTOS_H
#define ABX_FAKE_FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

#define pdFALSE ((BaseType_t)0)
#define pdTRUE ((BaseType_t)1)
#define pdPASS pdTRUE
#define pdFAIL pdFALSE
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define configTICK_RATE_HZ 1000u

extern int fake_config_assert_failures;
#define configASSERT(x) do { if (!(x)) ++fake_config_assert_failures; } while (0)
#define portYIELD_FROM_ISR(x) ((void)(x))

#endif
