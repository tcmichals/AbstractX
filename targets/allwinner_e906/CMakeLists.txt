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
    src/platform.cpp
    src/io_processor.cpp
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
target_compile_definitions(abstractx_target_e906 PUBLIC
    TARGET_ALLWINNER_E906=1
    ETL_NO_CHECKS=1
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
