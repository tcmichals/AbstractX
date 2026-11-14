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

    # @impl [SPEC-PICO-CFG-01] targets/pico2w_rp2350/SPECIFICATION.md (board and Wi-Fi are build options)
    if(PICO_CYW43_SUPPORTED)
        set(_abx_board_has_cyw43 ON)
    else()
        set(_abx_board_has_cyw43 OFF)
    endif()
    option(ABSTRACTX_PICO_WIFI "Pico 2 W: run CYW43 Wi-Fi on Core 0 (default: the board has a CYW43)" ${_abx_board_has_cyw43})
    if(ABSTRACTX_PICO_WIFI AND NOT _abx_board_has_cyw43)
        message(FATAL_ERROR "ABSTRACTX_PICO_WIFI=ON, but PICO_BOARD '${PICO_BOARD}' has no CYW43 (PICO_CYW43_SUPPORTED is not set). "
                            "Use PICO_BOARD=pico2_w or -DABSTRACTX_PICO_WIFI=OFF.")
    endif()
    message(STATUS "AbstractX Pico target: board ${PICO_BOARD}, Wi-Fi ${ABSTRACTX_PICO_WIFI}")

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
        hardware_spi
        hardware_i2c
        hardware_pio
        hardware_dma
        hardware_timer
        hardware_sync
    )

    # Two cores share domain queues: InterruptLock also excludes the other core ([SPEC-ARCH-01]).
    target_compile_definitions(abstractx_core PUBLIC
        ABSTRACTX_STATIC_TRACE_PACKETS=0
        ABSTRACTX_SMP_CORES=2
    )
    target_compile_definitions(abstractx_target_pico2w PUBLIC
        TARGET_PICO2W_RP2350=1
        ABSTRACTX_STATIC_TRACE_PACKETS=0
    )

    if(ABSTRACTX_PICO_WIFI)
        target_link_libraries(abstractx_target_pico2w PUBLIC pico_cyw43_arch_lwip_poll)
        target_compile_definitions(abstractx_target_pico2w PUBLIC ABSTRACTX_PICO_WIFI=1)
        # @impl [SPEC-PICO-CORE-01] targets/pico2w_rp2350/SPECIFICATION.md (__wrap_main: Wi-Fi on Core 0, app on Core 1)
        target_link_options(abstractx_target_pico2w PUBLIC -Wl,--wrap=main)
    else()
        # @impl [SPEC-PICO-CFG-01] targets/pico2w_rp2350/SPECIFICATION.md (no CYW43 / lwIP, no main wrap)
        target_compile_definitions(abstractx_target_pico2w PUBLIC ABSTRACTX_PICO_WIFI=0)
    endif()

    # @impl [SPEC-PICO-HAL-03] targets/pico2w_rp2350/SPECIFICATION.md (console on UART1; GPS owns UART0)
    target_compile_definitions(abstractx_target_pico2w PUBLIC
        PICO_DEFAULT_UART=1
        PICO_DEFAULT_UART_TX_PIN=8
        PICO_DEFAULT_UART_RX_PIN=9
    )

    if(NOT ABSTRACTX_PICO_WIFI AND (DEFINED WIFI_SSID OR DEFINED WIFI_PASSWORD))
        message(STATUS "AbstractX Pico target: Wi-Fi off, WIFI_SSID / WIFI_PASSWORD ignored")
    elseif(ABSTRACTX_PICO_WIFI)
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
    endif()
else()
    message(STATUS "Pico SDK not found; abstractx_target_pico2w will be disabled.")
endif()
