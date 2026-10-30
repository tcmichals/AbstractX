# targets/allwinner_e906/CMakeLists.txt
# ------------------------------------
# Target BSP Library for Allwinner XuanTie E906 (T527 / A523)

add_library(abstractx_target_e906 STATIC
    bsp/startup.S
    bsp/resource_table.c
    src/ccu.cpp
    src/uart.cpp
    src/timer.cpp
    src/msgbox.cpp
    src/trace.cpp
    src/pmp.cpp
    src/crash.cpp
    src/irq_dispatcher.cpp
    src/dma.cpp
    src/spi.cpp
    src/i2c.cpp
    src/driver_wait.cpp
    src/platform.cpp
    src/gpio_irq.cpp
    src/io_processor.cpp
    src/rpmsg.cpp
    src/rpmsg_endpoints.cpp
)

target_include_directories(abstractx_target_e906 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/hal>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bsp>
)

target_link_libraries(abstractx_target_e906 PUBLIC
    abstractx_core
    etl
)

# Export default linker script (512K SRAM A3 + 1M DRAM carveout)
set(E906_LDSCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/bsp/e906_ddr.ld" CACHE FILEPATH "E906 Linker Script")
# Coroutine frame pool: right-sized for E906 from measured gps_imu_app high-water mark
# (3,136 B + 25% headroom = 3,920 B -> 4,096 B / 4 KB) [SPEC-ARCH-02] [SPEC-E906-MEM-01]
# Zero-copy RPMsg trace pool elision: 0 static fallback buffers [SPEC-TRACE-07] [SPEC-E906-IPC-04]
# SPSC TLP ring capacity: right-sized to 8 entries to match 7-TLP RPMsg batch payload [SPEC-E906-MEM-01] [SPEC-E906-IPC-04]
# @impl [SPEC-E906-MEM-01] [SPEC-E906-IPC-04] [SPEC-TRACE-07] targets/allwinner_e906/SPECIFICATION.md#spec-e906-mem-01
if(NOT DEFINED ABSTRACTX_CORO_POOL_SIZE)
    set(ABSTRACTX_CORO_POOL_SIZE 4096)
endif()
if(NOT DEFINED ABSTRACTX_TLP_RING_CAPACITY)
    set(ABSTRACTX_TLP_RING_CAPACITY 8)
endif()
target_compile_definitions(abstractx_core PUBLIC
    ABSTRACTX_CORO_POOL_SIZE=${ABSTRACTX_CORO_POOL_SIZE}
    ABSTRACTX_STATIC_TRACE_PACKETS=0
    ABSTRACTX_TLP_RING_CAPACITY=${ABSTRACTX_TLP_RING_CAPACITY}
)
target_compile_definitions(abstractx_target_e906 PUBLIC
    TARGET_ALLWINNER_E906=1
    ETL_NO_CHECKS=1
    CONFIG_RPROC_RPMSG=1
    ABSTRACTX_CORO_POOL_SIZE=${ABSTRACTX_CORO_POOL_SIZE}
    ABSTRACTX_STATIC_TRACE_PACKETS=0
    ABSTRACTX_TLP_RING_CAPACITY=${ABSTRACTX_TLP_RING_CAPACITY}
)

# Backward compatibility alias
add_library(abstractx_target_e907 ALIAS abstractx_target_e906)

# Fail the build if a firmware ELF has a loadable segment that sunxi_rproc
# cannot place (outside SRAM Space 0/1 or the DRAM carveout).
# @impl [SPEC-E906-MEM-01] targets/allwinner_e906/SPECIFICATION.md
# Cache entries, not plain variables: the function runs in the caller's scope.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
string(REGEX REPLACE "objcopy$" "readelf" _e906_readelf "${CMAKE_OBJCOPY}")
set(E906_READELF "${_e906_readelf}" CACHE INTERNAL "readelf for E906 layout check")
set(E906_PYTHON3 "${Python3_EXECUTABLE}" CACHE INTERNAL "python3 for E906 layout check")
function(abstractx_e906_check_layout target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${E906_PYTHON3} ${PROJECT_SOURCE_DIR}/tools/check_e906_layout.py
                --readelf ${E906_READELF} $<TARGET_FILE:${target}>
        COMMENT "Checking ${target} load segments against remoteproc windows"
        VERBATIM
    )
endfunction()
set(E907_LDSCRIPT "${E906_LDSCRIPT}" CACHE FILEPATH "Legacy E907 Linker Script Alias")
