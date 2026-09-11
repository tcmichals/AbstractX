# apps/gps_imu_app/CMakeLists.txt
# ------------------------------
# Single Unified Sensor Benchmark & Testing Application for ALL Targets:
# - Raspberry Pi Pico 2 W (RP2350)
# - Allwinner XuanTie E907 RISC-V Coprocessor
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

elseif(ABSTRACTX_TARGET STREQUAL "e907")
    target_link_libraries(gps_imu_app PRIVATE abstractx_target_e907 m)
    target_link_options(gps_imu_app PRIVATE
        -T "${E907_LDSCRIPT}"
        -Wl,-Map=gps_imu_app.map
        -Wl,--gc-sections
        -nostartfiles
    )
    add_custom_command(TARGET gps_imu_app POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:gps_imu_app> gps_imu_app.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:gps_imu_app>
        COMMENT "Generated gps_imu_app.bin and displaying memory footprint"
    )

else()
    # Linux (Desktop workstation, SITL, or Allwinner Cubie A5E Linux)
    target_link_libraries(gps_imu_app PRIVATE abstractx_target_linux)
endif()
