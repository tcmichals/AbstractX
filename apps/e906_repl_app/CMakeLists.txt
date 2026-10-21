# apps/e906_repl_app/CMakeLists.txt
# --------------------------------
# AbstractX XuanTie E906 MicroPython Scripting & REPL Reference Application
# Exclusively for Allwinner Radxa Cubie A5E / A7A (E906 + HiFi4 DSP SRAM)
#
# @impl [SPEC-E906-REPL-01] apps/e906_repl_app/SPECIFICATION.md

if(NOT (ABSTRACTX_TARGET STREQUAL "e906" OR ABSTRACTX_TARGET STREQUAL "allwinner_e906"))
    return()
endif()

set(MICROPY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/micropython")
set(MICROPY_PY_DIR "${MICROPY_DIR}/py")
set(MICROPY_PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(MICROPY_TARGET e906_repl_app)

include(${MICROPY_DIR}/py/py.cmake)

set(REPL_APP_SOURCES
    src/main.cpp
    src/rpmsg_console.cpp
    src/mphalport.cpp
    src/modabstractx.c
    ${MICROPY_SOURCE_PY}
    ${MICROPY_DIR}/shared/runtime/pyexec.c
    ${MICROPY_DIR}/shared/readline/readline.c
    ${MICROPY_DIR}/shared/runtime/stdout_helpers.c
    ${MICROPY_DIR}/shared/runtime/interrupt_char.c
)

add_executable(e906_repl_app
    ${REPL_APP_SOURCES}
)

target_include_directories(e906_repl_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${MICROPY_PORT_DIR}/include
    ${MICROPY_DIR}
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../targets/allwinner_e906/include
)

target_compile_definitions(e906_repl_app PRIVATE
    E906_REPL_APP=1
    ABSTRACTX_CORO_POOL_SIZE=4096
)

# Set up MicroPython code generation rules
set(MICROPY_GENHDR_DIR "${CMAKE_CURRENT_BINARY_DIR}/genhdr")
set(MICROPY_SOURCE_QSTR
    ${MICROPY_SOURCE_PY}
    ${MICROPY_PORT_DIR}/include/qstrdefsport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modabstractx.c
    ${MICROPY_DIR}/shared/runtime/pyexec.c
    ${MICROPY_DIR}/shared/readline/readline.c
)
include(${MICROPY_DIR}/py/mkrules.cmake)

add_dependencies(e906_repl_app BUILD_VERSION_HEADER)

target_link_libraries(e906_repl_app PRIVATE
    abstractx_core
    abstractx_target_e906
    etl
    m
)

target_link_options(e906_repl_app PRIVATE
    "-T${E906_LDSCRIPT}"
    -Wl,-Map=e906_repl_app.map
    -Wl,--gc-sections
    -nostartfiles
)

abstractx_e906_check_layout(e906_repl_app)

add_custom_command(TARGET e906_repl_app POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:e906_repl_app> e906_repl_app.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:e906_repl_app>
    COMMENT "Generated e906_repl_app.bin and displaying memory footprint"
)

# Size-optimise the MicroPython VM + glue: SRAM_FAST (192 KB) is nearly full and the
# TLP/trace bindings ([SPEC-E906-REPL-07] [SPEC-E906-REPL-10]) need headroom.
target_compile_options(e906_repl_app PRIVATE -Os)
