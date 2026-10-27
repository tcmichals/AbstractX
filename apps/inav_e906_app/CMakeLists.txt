# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
#
# inav_e906_app - Dedicated XuanTie E906 Flight Co-Processor Firmware
#
# Hardware Sensor Ingestion:
#   - SPI1 @ 10 MHz: ICM-42688-P 6-DoF IMU burst streaming
#   - TWI1 @ 400 kHz: HMC5883L / QMC5883L 3D Magnetometer
#   - UART3 @ 115200: u-blox NEO-M8N GPS NMEA/UBX DMA ingestion
#   - SPI1 Dual-I/O: Companion FPGA Hardware Interface
#   - VirtIO RPMsg / TLP64: Lock-free IPC with Linux Host (inav_linux_sbc)

if(NOT (ABSTRACTX_TARGET STREQUAL "e906" OR ABSTRACTX_TARGET STREQUAL "allwinner_e906"))
    return()
endif()

add_executable(inav_e906_app
    src/main.cpp
)

target_include_directories(inav_e906_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/targets/allwinner_e906/include
    ${PROJECT_SOURCE_DIR}/targets/allwinner_e906/include/hal
)

target_link_libraries(inav_e906_app PRIVATE
    abstractx_target_e906
    abstractx_core
    abstractx_applib
    etl
    m
)

target_link_options(inav_e906_app PRIVATE
    "-T${E906_LDSCRIPT}"
    -Wl,-Map=inav_e906_app.map
    -Wl,--gc-sections
    -nostartfiles
)

abstractx_e906_check_layout(inav_e906_app)

add_custom_command(TARGET inav_e906_app POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:inav_e906_app> inav_e906_app.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:inav_e906_app>
    COMMENT "Generated inav_e906_app.bin and displaying memory footprint"
)
