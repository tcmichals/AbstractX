# targets/zynq7000/CMakeLists.txt
# ------------------------------
# AbstractX Target HAL & BSP for Xilinx Zynq-7000 (Topology 3: FPGA Hardware Offload)
# Bridges Linux user-space flight loops to Artix-7 PL via /dev/uio0 (AXI-Lite & IRQ_F2P)

add_library(abstractx_target_zynq7000 STATIC
    src/io_processor.cpp
    src/hal_platform.cpp
    ${CMAKE_SOURCE_DIR}/targets/linux/src/hal_timer.cpp
    ${CMAKE_SOURCE_DIR}/targets/linux/src/hal_spi.cpp
    ${CMAKE_SOURCE_DIR}/targets/linux/src/hal_i2c.cpp
    ${CMAKE_SOURCE_DIR}/targets/linux/src/hal_uart.cpp
    ${CMAKE_SOURCE_DIR}/targets/linux/src/hal_gpio.cpp
)

target_include_directories(abstractx_target_zynq7000 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/targets/linux/include
)

target_link_libraries(abstractx_target_zynq7000 PUBLIC
    abstractx_core
    etl
    pthread
    rt
)

# Alias for target selector
add_library(abstractx_target ALIAS abstractx_target_zynq7000)
