# targets/host_posix/CMakeLists.txt
# ---------------------------------
# Host Workstation BSP for Simulation, Testing, and SITL

add_library(abstractx_target_host STATIC
    src/hal_timer.cpp
    src/hal_uart.cpp
    src/hal_spi.cpp
)

target_include_directories(abstractx_target_host PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(abstractx_target_host PUBLIC
    abstractx_core
    etl
    Threads::Threads
)

target_compile_definitions(abstractx_target_host PUBLIC
    TARGET_HOST_POSIX=1
)
