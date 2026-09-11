# targets/allwinner_e907/CMakeLists.txt
# ------------------------------------
# Target BSP Library for Allwinner XuanTie E907 (T527 / A733)

add_library(abstractx_target_e907 STATIC
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
)

target_include_directories(abstractx_target_e907 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/hal>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bsp>
)

target_link_libraries(abstractx_target_e907 PUBLIC
    abstractx_core
    etl
)

# Export default linker script
set(E907_LDSCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/bsp/e907_sram.ld" CACHE FILEPATH "E907 Linker Script")
target_compile_definitions(abstractx_target_e907 PUBLIC
    TARGET_ALLWINNER_E907=1
    ETL_NO_STL=1
    ETL_NO_CHECKS=1
)
