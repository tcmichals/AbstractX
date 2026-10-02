# apps/serial_loopback_app/platforms/linux/CMakeLists.txt

add_executable(linux_serial_loopback_sitl
    main.cpp
)

target_link_libraries(linux_serial_loopback_sitl PRIVATE
    abstractx_core
    abstractx_target_linux
    etl
)
