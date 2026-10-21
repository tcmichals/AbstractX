/*
 * FreeRTOSConfig.h - FreeRTOS V11.1.0 for AbstractX on the XuanTie E906.
 * @impl [SPEC-E906-RTOS-02] [SPEC-E906-RTOS-03] apps/e906_freertos_app/SPECIFICATION.md
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
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

#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            0
/* [SPEC-E906-RTOS-05] idle task sleeps in wfi and counts idle time */
#define configUSE_IDLE_HOOK                     1
#define configUSE_TICK_HOOK                     0
#define configUSE_TIMERS                        0
#define configTIMER_TASK_STACK_DEPTH            configMINIMAL_STACK_SIZE /* V11.1.0 references it even with timers off */

/* [SPEC-E906-RTOS-03] E906 CLINT timer (measured; 0x14004000 does not count) */
#define configMTIME_BASE_ADDRESS                ( 0xE000BFF8UL )
#define configMTIMECMP_BASE_ADDRESS             ( 0xE0004000UL )
#define configISR_STACK_SIZE_WORDS              ( 512 )

#define configASSERT( x ) if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ) { __asm__ volatile( "wfi" ); } }

#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

#define INCLUDE_vTaskPrioritySet                0
#define INCLUDE_uxTaskPriorityGet               0
#define INCLUDE_vTaskDelete                     0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 0
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskGetStackHighWaterMark     1

#endif /* FREERTOS_CONFIG_H */
