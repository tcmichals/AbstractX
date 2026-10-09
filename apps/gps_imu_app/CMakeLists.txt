# apps/gps_imu_app/CMakeLists.txt
# ------------------------------
# Single Unified Sensor Benchmark & Testing Application for ALL Targets:
# - Raspberry Pi Pico 2 W (RP2350)
# - Allwinner XuanTie E906 RISC-V Coprocessor
# - Linux (Desktop Workstation, SITL, and Allwinner Cubie A5E)

add_executable(gps_imu_app
    src/main.cpp
)

target_link_libraries(gps_imu_app PRIVATE
    abstractx_core
    etl
)

if(ABSTRACTX_TARGET STREQUAL "pico2w")
    target_link_libraries(gps_imu_app PRIVATE abstractx_target_pico2w)
    set_target_properties(gps_imu_app PROPERTIES SUFFIX ".elf")
    pico_add_extra_outputs(gps_imu_app)

elseif(ABSTRACTX_TARGET STREQUAL "e906" OR ABSTRACTX_TARGET STREQUAL "e907")
    target_link_libraries(gps_imu_app PRIVATE abstractx_target_e906 m)
    target_link_options(gps_imu_app PRIVATE
        -T "${E906_LDSCRIPT}"
        -Wl,-Map=gps_imu_app.map
        -Wl,--gc-sections
        -nostartfiles
    )
    add_custom_command(TARGET gps_imu_app POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:gps_imu_app> gps_imu_app.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:gps_imu_app>
        COMMENT "Generated gps_imu_app.bin and displaying memory footprint"
    )

elseif(ABSTRACTX_TARGET STREQUAL "zynq7000")
    target_link_libraries(gps_imu_app PRIVATE abstractx_target_zynq7000)

else()
    # Linux (Desktop workstation, SITL, or Allwinner Cubie A5E Linux)
    target_link_libraries(gps_imu_app PRIVATE abstractx_target_linux)
endif()

# Companion XuanTie E906 coprocessor firmware for Allwinner A5E
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/platforms/allwinner_e906/CMakeLists.txt" AND (ABSTRACTX_TARGET STREQUAL "e906" OR ABSTRACTX_TARGET STREQUAL "e907"))
    add_subdirectory(platforms/allwinner_e906)
endif()
