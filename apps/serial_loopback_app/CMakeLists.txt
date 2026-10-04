# apps/serial_loopback_app/CMakeLists.txt
# -------------------------------------
# AbstractX Serial DMA & Coroutine Loopback Reference Application
# Universal across Allwinner E906, RP2350 (Pico 2 W), ESP32-P4, and Linux.

add_executable(serial_loopback_app
    src/main.cpp
)

target_link_libraries(serial_loopback_app PRIVATE
    abstractx_core
    etl
)

if(ABSTRACTX_TARGET STREQUAL "pico2w")
    target_link_libraries(serial_loopback_app PRIVATE abstractx_target_pico2w)
    set_target_properties(serial_loopback_app PROPERTIES SUFFIX ".elf")
    pico_add_extra_outputs(serial_loopback_app)

elseif(ABSTRACTX_TARGET STREQUAL "e906" OR ABSTRACTX_TARGET STREQUAL "e907")
    target_link_libraries(serial_loopback_app PRIVATE abstractx_target_e906 m)
    target_link_options(serial_loopback_app PRIVATE
        "-T${E906_LDSCRIPT}"
        -Wl,-Map=serial_loopback_app.map
        -Wl,--gc-sections
        -nostartfiles
    )
    add_custom_command(TARGET serial_loopback_app POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:serial_loopback_app> serial_loopback_app.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:serial_loopback_app>
        COMMENT "Generated serial_loopback_app.bin and displaying memory footprint"
    )

elseif(ABSTRACTX_TARGET STREQUAL "zynq7000")
    target_link_libraries(serial_loopback_app PRIVATE abstractx_target_zynq7000)

else()
    target_link_libraries(serial_loopback_app PRIVATE abstractx_target_linux)
endif()

# Companion co-processor firmware for Allwinner T527 / Radxa Cubie A5E
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/platforms/allwinner_e906/CMakeLists.txt" AND (ABSTRACTX_TARGET STREQUAL "e906" OR ABSTRACTX_TARGET STREQUAL "e907"))
    add_subdirectory(platforms/allwinner_e906)
endif()

# Linux SITL 2-queue memory loopback runner
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/platforms/linux/CMakeLists.txt" AND (ABSTRACTX_TARGET STREQUAL "host" OR ABSTRACTX_TARGET STREQUAL "linux"))
    add_subdirectory(platforms/linux)
endif()
