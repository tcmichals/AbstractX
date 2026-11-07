# targets/zynq7000/CMakeLists.txt
# ------------------------------
# AbstractX Target HAL & BSP for Xilinx Zynq-7000 (Topology 3: FPGA Hardware Offload)
# Bridges Linux user-space flight loops to Artix-7 PL via /dev/uio0 (AXI-Lite & IRQ_F2P)

add_library(abstractx_target_zynq7000 STATIC
    src/io_processor.cpp
    src/hal_platform.cpp
    ${AbstractX_SOURCE_DIR}/targets/linux/src/hal_timer.cpp
    ${AbstractX_SOURCE_DIR}/targets/linux/src/hal_spi.cpp
    ${AbstractX_SOURCE_DIR}/targets/linux/src/hal_i2c.cpp
    ${AbstractX_SOURCE_DIR}/targets/linux/src/hal_uart.cpp
    ${AbstractX_SOURCE_DIR}/targets/linux/src/hal_gpio.cpp
)

target_include_directories(abstractx_target_zynq7000 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${AbstractX_SOURCE_DIR}/include
    ${AbstractX_SOURCE_DIR}/targets/linux/include
)

target_link_libraries(abstractx_target_zynq7000 PUBLIC
    abstractx_core
    etl
    pthread
    rt
)

# libgpiod v2, as on the Linux target ([SPEC-LINUX-GPIO-01] detected by the build)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBGPIOD QUIET libgpiod)
endif()
if(LIBGPIOD_FOUND AND LIBGPIOD_VERSION VERSION_GREATER_EQUAL 2)
    target_include_directories(abstractx_target_zynq7000 PUBLIC ${LIBGPIOD_INCLUDE_DIRS})
    target_link_directories(abstractx_target_zynq7000 PUBLIC ${LIBGPIOD_LIBRARY_DIRS})
    target_link_libraries(abstractx_target_zynq7000 PUBLIC ${LIBGPIOD_LIBRARIES})
    target_compile_definitions(abstractx_target_zynq7000 PUBLIC ABSTRACTX_HAS_LIBGPIOD_V2=1)
endif()

# Alias for target selector
add_library(abstractx_target ALIAS abstractx_target_zynq7000)
