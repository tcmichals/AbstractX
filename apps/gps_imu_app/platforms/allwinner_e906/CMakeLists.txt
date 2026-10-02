# apps/gps_imu_app/platforms/allwinner_e906/CMakeLists.txt
# ---------------------------------------------------
# Deployable ELF/BIN for XuanTie E906 via Linux remoteproc

add_executable(e906_coprocessor
    main.cpp
)

target_link_libraries(e906_coprocessor PRIVATE
    abstractx_target_e906
    abstractx_core
    etl
    m
)

target_link_options(e906_coprocessor PRIVATE
    -T "${E906_LDSCRIPT}"
    -Wl,-Map=e906_coprocessor.map
    -Wl,--gc-sections
    -nostartfiles
)

# Custom command to generate binary (.bin)
add_custom_command(TARGET e906_coprocessor POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:e906_coprocessor> e906_coprocessor.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:e906_coprocessor>
    COMMENT "Generated e906_coprocessor.bin and displaying memory footprint"
)

# Automated compilation of Device Tree Overlay if dtc is available
find_program(DTC_EXECUTABLE dtc HINTS 
    /home/tcmichals/tools/Xilinx/2025.2/Vivado/bin
    /home/tcmichals/ssdData/projects/home/CubieA5E/bld.a7a/host/bin
)
if(DTC_EXECUTABLE)
    add_custom_target(e906_dtbo ALL
        COMMAND ${DTC_EXECUTABLE} -@ -I dts -O dtb -o ${CMAKE_CURRENT_BINARY_DIR}/cubie_a5e_e906_overlay.dtbo ${CMAKE_CURRENT_SOURCE_DIR}/cubie_a5e_e906_overlay.dtso
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/cubie_a5e_e906_overlay.dtbo
        COMMENT "Compiled cubie_a5e_e906_overlay.dtbo using ${DTC_EXECUTABLE}"
    )
    add_custom_target(e907_dtbo DEPENDS e906_dtbo)
endif()

# Backward compatibility alias
add_custom_target(e907_coprocessor ALL DEPENDS e906_coprocessor)
