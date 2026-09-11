# targets/pico2w_rp2350/CMakeLists.txt
# ------------------------------------
# Target BSP Library for Raspberry Pi Pico 2 / Pico 2 W (RP2350)

include(pico_sdk_import.cmake)

if(COMMAND pico_sdk_init)
    pico_sdk_init()

    add_library(abstractx_target_pico2w STATIC
        src/hal_timer.cpp
        src/hal_uart.cpp
        src/hal_pio_spi.cpp
    )

    target_include_directories(abstractx_target_pico2w PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )

    target_link_libraries(abstractx_target_pico2w PUBLIC
        abstractx_core
        etl
        pico_stdlib
        pico_multicore
        pico_cyw43_arch_none
        hardware_spi
        hardware_pio
        hardware_dma
        hardware_timer
        hardware_sync
    )

    target_compile_definitions(abstractx_target_pico2w PUBLIC
        TARGET_PICO2W_RP2350=1
    )
else()
    message(STATUS "Pico SDK not found; abstractx_target_pico2w will be disabled.")
endif()
