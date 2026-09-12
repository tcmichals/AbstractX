# targets/linux/CMakeLists.txt
# ----------------------------
# Unified Linux Target HAL & BSP (spidev, i2c-dev, termios2, libgpiod v2, epoll, eventfd)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBGPIOD QUIET libgpiod)
endif()

add_library(abstractx_target_linux STATIC
    src/hal_spi.cpp
    src/hal_i2c.cpp
    src/hal_uart.cpp
    src/hal_gpio.cpp
    src/hal_timer.cpp
    src/hal_platform.cpp
    src/io_processor.cpp
)

target_include_directories(abstractx_target_linux PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include
)

if(LIBGPIOD_FOUND)
    target_include_directories(abstractx_target_linux PRIVATE ${LIBGPIOD_INCLUDE_DIRS})
    target_link_libraries(abstractx_target_linux PUBLIC ${LIBGPIOD_LIBRARIES})
endif()


target_link_libraries(abstractx_target_linux PUBLIC
    abstractx_core
    etl
    pthread
    rt
)


# Backward-compatible alias for existing host targets & tests
add_library(abstractx_target_host ALIAS abstractx_target_linux)
