# apps/gps_imu_app/platforms/allwinner_e907/CMakeLists.txt
# ---------------------------------------------------
# Deployable ELF/BIN for XuanTie E907 via Linux remoteproc

add_executable(e907_coprocessor
    main.cpp
)

target_link_libraries(e907_coprocessor PRIVATE
    abstractx_target_e907
    abstractx_core
    etl
    m
)

target_link_options(e907_coprocessor PRIVATE
    -T "${E907_LDSCRIPT}"
    -Wl,-Map=e907_coprocessor.map
    -Wl,--gc-sections
    -nostartfiles
)

# Custom command to generate binary (.bin)
add_custom_command(TARGET e907_coprocessor POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:e907_coprocessor> e907_coprocessor.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:e907_coprocessor>
    COMMENT "Generated e907_coprocessor.bin and displaying memory footprint"
)

# Automated compilation of Device Tree Overlay if dtc is available
find_program(DTC_EXECUTABLE dtc HINTS 
    /home/tcmichals/tools/Xilinx/2025.2/Vivado/bin
    /home/tcmichals/ssdData/projects/home/CubieA5E/bld.a7a/host/bin
)
if(DTC_EXECUTABLE)
    add_custom_target(e907_dtbo ALL
        COMMAND ${DTC_EXECUTABLE} -@ -I dts -O dtb -o ${CMAKE_CURRENT_BINARY_DIR}/cubie_a5e_e907_overlay.dtbo ${CMAKE_CURRENT_SOURCE_DIR}/cubie_a5e_e907_overlay.dtso
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/cubie_a5e_e907_overlay.dtbo
        COMMENT "Compiled cubie_a5e_e907_overlay.dtbo using ${DTC_EXECUTABLE}"
    )
endif()

