# drivers/CMakeLists.txt
# Chip drivers (GPS, IMU, compass) built on the HAL interfaces in include/abstractx/hal.
# Kept out of the generic core; link abstractx_drivers only where a driver is used.
# @impl [SPEC-ARCH-12] docs/DESIGN_SPECIFICATION.md
add_library(abstractx_drivers INTERFACE)
target_include_directories(abstractx_drivers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(abstractx_drivers INTERFACE abstractx_core)
