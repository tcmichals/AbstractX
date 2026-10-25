# applib/CMakeLists.txt
# AbstractX Application Library (applib)
# Reusable peripheral drivers, mathematical utilities, and sensor components
# built ON TOP of the AbstractX core framework.
# @impl [SPEC-ARCH-12] docs/DESIGN_SPECIFICATION.md

add_library(abstractx_applib INTERFACE)
target_include_directories(abstractx_applib INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(abstractx_applib INTERFACE abstractx_core)

# Backwards compatibility alias for app targets transitioning to applib
add_library(abstractx_drivers ALIAS abstractx_applib)
