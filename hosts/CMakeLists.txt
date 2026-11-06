# hosts/: the OS loops that drive AbstractX from outside the core ([SPEC-ARCH-15], [SPEC-ARCH-16]).
# @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (POSIX host library)
# hosts/freertos is compiled by the FreeRTOS application that uses it.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND (ABSTRACTX_TARGET STREQUAL "host" OR ABSTRACTX_TARGET STREQUAL "linux"))
    add_library(abstractx_host_posix STATIC posix/src/posix_host.cpp)
    target_include_directories(abstractx_host_posix PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/posix/include)
    target_link_libraries(abstractx_host_posix PUBLIC abstractx_core)
    # [SPEC-ARCH-15] Boost.Asio host (header-only Asio); only when Boost is installed
    find_path(ABSTRACTX_BOOST_ASIO_INCLUDE boost/asio.hpp)
    if(ABSTRACTX_BOOST_ASIO_INCLUDE)
        add_library(abstractx_host_asio STATIC asio/src/asio_host.cpp)
        target_include_directories(abstractx_host_asio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/asio/include
                                   PRIVATE ${ABSTRACTX_BOOST_ASIO_INCLUDE})
        target_link_libraries(abstractx_host_asio PUBLIC abstractx_core pthread)
    else()
        message(STATUS "abstractx_host_asio skipped: boost/asio.hpp not found")
    endif()
endif()
