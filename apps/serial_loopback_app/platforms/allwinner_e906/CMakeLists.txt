# apps/serial_loopback_app/platforms/allwinner_e906/CMakeLists.txt

add_executable(e906_serial_loopback
    main.cpp
)

target_link_libraries(e906_serial_loopback PRIVATE
    abstractx_target_e906
    abstractx_core
    etl
    m
)

target_link_options(e906_serial_loopback PRIVATE
    "-T${E906_LDSCRIPT}"
    -Wl,-Map=e906_serial_loopback.map
    -Wl,--gc-sections
    -nostartfiles
)

add_custom_command(TARGET e906_serial_loopback POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:e906_serial_loopback> e906_serial_loopback.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:e906_serial_loopback>
    COMMENT "Generated e906_serial_loopback.bin and displaying memory footprint"
)
