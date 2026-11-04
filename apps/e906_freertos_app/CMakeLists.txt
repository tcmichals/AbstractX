# apps/e906_freertos_app/CMakeLists.txt
# AbstractX as one FreeRTOS task on the XuanTie E906.
# @impl [SPEC-E906-RTOS-01] apps/e906_freertos_app/SPECIFICATION.md

if(NOT (ABSTRACTX_TARGET STREQUAL "e906" OR ABSTRACTX_TARGET STREQUAL "allwinner_e906"))
    return()
endif()

set(FREERTOS_DIR "${PROJECT_SOURCE_DIR}/third_party/FreeRTOS-Kernel")
if(NOT EXISTS "${FREERTOS_DIR}/tasks.c")
    message(WARNING "e906_freertos_app skipped: run 'git submodule update --init third_party/FreeRTOS-Kernel'")
    return()
endif()

# [SPEC-E906-RTOS-03] how AbstractX sleeps are served
# @impl [SPEC-E906-RTOS-03] apps/e906_freertos_app/SPECIFICATION.md
set(ABX_FREERTOS_TIMER "WAIT" CACHE STRING "AbstractX timer under FreeRTOS: WAIT, XTIMER or TICK_HOOK")
set_property(CACHE ABX_FREERTOS_TIMER PROPERTY STRINGS WAIT XTIMER TICK_HOOK)
if(ABX_FREERTOS_TIMER STREQUAL "WAIT")
    set(ABX_FREERTOS_TIMER_MODE 0)
elseif(ABX_FREERTOS_TIMER STREQUAL "XTIMER")
    set(ABX_FREERTOS_TIMER_MODE 1)
elseif(ABX_FREERTOS_TIMER STREQUAL "TICK_HOOK")
    set(ABX_FREERTOS_TIMER_MODE 2)
else()
    message(FATAL_ERROR "ABX_FREERTOS_TIMER must be WAIT, XTIMER or TICK_HOOK (got '${ABX_FREERTOS_TIMER}')")
endif()

# [SPEC-E906-RTOS-02] no heap_*.c
add_library(freertos_e906 STATIC
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/timers.c
    ${FREERTOS_DIR}/portable/GCC/RISC-V/port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/portASM_e906.S
)
target_include_directories(freertos_e906 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/RISC-V
    ${FREERTOS_DIR}/portable/GCC/RISC-V/chip_specific_extensions/RISCV_MTIME_CLINT_no_extensions
)
target_compile_options(freertos_e906 PRIVATE -Wno-unused-parameter)
target_compile_definitions(freertos_e906 PUBLIC ABX_FREERTOS_TIMER_MODE=${ABX_FREERTOS_TIMER_MODE})

add_executable(e906_freertos_app
    src/main.cpp
    ${PROJECT_SOURCE_DIR}/hosts/freertos/src/freertos_host.cpp  # [SPEC-ARCH-15] shared FreeRTOS host
    ${CMAKE_CURRENT_SOURCE_DIR}/../e906_bench/bench.cpp
)
target_include_directories(e906_freertos_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../e906_bench
                                                     ${PROJECT_SOURCE_DIR}/hosts/freertos/include)
target_link_libraries(e906_freertos_app PRIVATE freertos_e906 abstractx_core abstractx_applib abstractx_target_e906 etl)
target_link_options(e906_freertos_app PRIVATE
    "-T${E906_LDSCRIPT}"
    -Wl,-Map=e906_freertos_app.map
    -Wl,--gc-sections
    -nostartfiles
)
abstractx_e906_check_layout(e906_freertos_app)
