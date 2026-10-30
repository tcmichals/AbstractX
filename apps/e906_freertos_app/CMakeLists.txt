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

# [SPEC-E906-RTOS-02] no heap_*.c
add_library(freertos_e906 STATIC
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/queue.c
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

add_executable(e906_freertos_app
    src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../e906_bench/bench.cpp
)
target_include_directories(e906_freertos_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../e906_bench)
target_link_libraries(e906_freertos_app PRIVATE freertos_e906 abstractx_core abstractx_applib abstractx_target_e906 etl)
target_link_options(e906_freertos_app PRIVATE
    "-T${E906_LDSCRIPT}"
    -Wl,-Map=e906_freertos_app.map
    -Wl,--gc-sections
    -nostartfiles
)
abstractx_e906_check_layout(e906_freertos_app)
