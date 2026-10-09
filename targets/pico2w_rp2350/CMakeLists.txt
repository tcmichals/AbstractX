# targets/pico2w_rp2350/CMakeLists.txt
# ------------------------------------
# Target BSP Library for Raspberry Pi Pico 2 / Pico 2 W (RP2350)

include(pico_sdk_import.cmake)

if(COMMAND pico_sdk_init)
    pico_sdk_init()

    # GCC 15 / binutils 2.44 --gc-sections requires explicit entry point for boot stage 2
    if(TARGET bs2_default)
        target_link_options(bs2_default PRIVATE -Wl,-e,_stage2_boot)
    endif()

    add_library(abstractx_target_pico2w STATIC
        src/hal_timer.cpp
        src/hal_gpio.cpp
        src/hal_uart.cpp
        src/hal_spi.cpp
        src/hal_i2c.cpp
        src/hal_platform.cpp
        src/io_processor.cpp
    )

    target_include_directories(abstractx_target_pico2w PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )

    target_link_libraries(abstractx_target_pico2w PUBLIC
        abstractx_core
        etl
        pico_stdlib
        pico_multicore
        pico_cyw43_arch_lwip_poll
        hardware_spi
        hardware_i2c
        hardware_pio
        hardware_dma
        hardware_timer
        hardware_sync
    )

    target_compile_definitions(abstractx_target_pico2w PUBLIC
        TARGET_PICO2W_RP2350=1
    )

    if(DEFINED WIFI_SSID)
        target_compile_definitions(abstractx_target_pico2w PUBLIC
            WIFI_SSID="${WIFI_SSID}"
        )
    endif()
    if(DEFINED WIFI_PASSWORD)
        target_compile_definitions(abstractx_target_pico2w PUBLIC
            WIFI_PASSWORD="${WIFI_PASSWORD}"
        )
    endif()
else()
    message(STATUS "Pico SDK not found; abstractx_target_pico2w will be disabled.")
endif()
