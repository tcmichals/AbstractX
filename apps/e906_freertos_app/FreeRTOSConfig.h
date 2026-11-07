/*
 * FreeRTOSConfig.h - FreeRTOS V11.1.0 for AbstractX on the XuanTie E906.
 * @impl [SPEC-E906-RTOS-02] [SPEC-E906-RTOS-03] apps/e906_freertos_app/SPECIFICATION.md
 * @impl [SPEC-E906-BENCH-06] apps/e906_bench/SPECIFICATION.md (ABX_FREERTOS_DEBUG)
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/* [SPEC-E906-BENCH-06] 0 for timing runs (set by CMake) */
#ifndef ABX_FREERTOS_DEBUG
#define ABX_FREERTOS_DEBUG                      1
#endif

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0
/* mtime rate (24 MHz oscillator), not the 200 MHz core clock: the RISC-V port
 * derives the tick from it. */
#define configCPU_CLOCK_HZ                      ( 24000000UL )
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                    ( 5 )
#define configMINIMAL_STACK_SIZE                ( ( uint16_t ) 256 )
#define configMAX_TASK_NAME_LEN                 ( 12 )
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configUSE_MUTEXES                       0
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0

/* [SPEC-E906-RTOS-02] static allocation only, no heap_*.c */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        0
#define configKERNEL_PROVIDED_STATIC_MEMORY     1

#define configCHECK_FOR_STACK_OVERFLOW          ( ABX_FREERTOS_DEBUG ? 2 : 0 )
#define configUSE_MALLOC_FAILED_HOOK            0
/* [SPEC-E906-RTOS-05] idle task sleeps in wfi and counts idle time */
#define configUSE_IDLE_HOOK                     1
/* [SPEC-E906-RTOS-03] how AbstractX sleeps are served: 0 WAIT (default), 1 XTIMER, 2 TICK_HOOK
 * (set by CMake from ABX_FREERTOS_TIMER) */
#ifndef ABX_FREERTOS_TIMER_MODE
#define ABX_FREERTOS_TIMER_MODE                 0
#endif
#define configUSE_TICK_HOOK                     ( ABX_FREERTOS_TIMER_MODE == 2 )
#define configUSE_TIMERS                        ( ABX_FREERTOS_TIMER_MODE == 1 )
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                4
#define configTIMER_TASK_STACK_DEPTH            configMINIMAL_STACK_SIZE /* V11.1.0 references it even with timers off */

/* [SPEC-E906-RTOS-03] E906 CLINT timer (measured; 0x14004000 does not count) */
#define configMTIME_BASE_ADDRESS                ( 0xE000BFF8UL )
#define configMTIMECMP_BASE_ADDRESS             ( 0xE0004000UL )
#define configISR_STACK_SIZE_WORDS              ( 512 )

#if ABX_FREERTOS_DEBUG
#define configASSERT( x ) if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ) { __asm__ volatile( "wfi" ); } }
#endif

#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

#define INCLUDE_vTaskPrioritySet                0
#define INCLUDE_uxTaskPriorityGet               0
#define INCLUDE_vTaskDelete                     0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1  /* [SPEC-E906-BENCH-06] native cyclic task */
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskGetStackHighWaterMark     1

#if ABX_FREERTOS_DEBUG
/* [SPEC-E906-RTOS-07] BareCTF / Studio Timeline Trace Integration */
#ifdef __cplusplus
extern "C" {
#endif
void abx_trace_task_switched_in(void);
void abx_trace_task_switched_out(void);
#ifdef __cplusplus
}
#endif

#define traceTASK_SWITCHED_IN()  abx_trace_task_switched_in()
#define traceTASK_SWITCHED_OUT() abx_trace_task_switched_out()
#endif

#endif /* FREERTOS_CONFIG_H */
