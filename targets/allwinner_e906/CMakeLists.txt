# targets/allwinner_e907/CMakeLists.txt
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
set(E906_LDSCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/bsp/e906_ddr.ld" CACHE FILEPATH "E907 Linker Script")
target_compile_definitions(abstractx_target_e906 PUBLIC
    TARGET_ALLWINNER_E906=1
    TARGET_ALLWINNER_E907=1
    ETL_NO_CHECKS=1
)

# Backward compatibility alias
add_library(abstractx_target_e907 ALIAS abstractx_target_e906)
set(E907_LDSCRIPT "${E906_LDSCRIPT}" CACHE FILEPATH "Legacy E907 Linker Script Alias")
