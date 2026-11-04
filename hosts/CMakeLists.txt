# hosts/: the OS loops that drive AbstractX from outside the core ([SPEC-ARCH-15], [SPEC-ARCH-16]).
# @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (POSIX host library)
# hosts/freertos is compiled by the FreeRTOS application that uses it.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND (ABSTRACTX_TARGET STREQUAL "host" OR ABSTRACTX_TARGET STREQUAL "linux"))
    add_library(abstractx_host_posix STATIC posix/src/posix_host.cpp)
    target_include_directories(abstractx_host_posix PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/posix/include)
    target_link_libraries(abstractx_host_posix PUBLIC abstractx_core)
endif()
