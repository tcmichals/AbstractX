cmake_minimum_required(VERSION 3.20)

include(targets/pico2w_rp2350/pico_sdk_import.cmake)

project(AbstractX VERSION 1.0.0 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Determine Target Platform (host, e907, pico2w, esp32p4)
if(NOT DEFINED ABSTRACTX_TARGET)
    if(CMAKE_SYSTEM_NAME STREQUAL "Generic" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv")
        set(ABSTRACTX_TARGET "e907")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Generic" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
        set(ABSTRACTX_TARGET "pico2w")
    else()
        set(ABSTRACTX_TARGET "host")
    endif()
endif()

message(STATUS "AbstractX Build Target: ${ABSTRACTX_TARGET}")

# Core AbstractX Interface Library
add_library(abstractx_core INTERFACE)
target_include_directories(abstractx_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(abstractx_core INTERFACE cxx_std_20)

if(NOT CMAKE_CROSSCOMPILING)
    find_package(Threads)
    if(TARGET Threads::Threads)
        target_link_libraries(abstractx_core INTERFACE Threads::Threads)
    endif()
endif()

# 1. Third-Party Dependencies (ETL, etc.)
add_subdirectory(third_party)

target_link_libraries(abstractx_core INTERFACE etl)

# 2. Hardware Abstraction Layer & Target BSPs
add_subdirectory(targets)

# 3. Applications & Deployable Binaries
add_subdirectory(apps)

# 4. Host Simulation & Examples (Only when building for host)
if(ABSTRACTX_TARGET STREQUAL "host")
    enable_testing()

    add_custom_target(test-all
        COMMENT "Running the complete simulation regression suite"
        VERBATIM
    )

    add_subdirectory(sim)
    add_subdirectory(examples)
endif()

# Custom helper to describe our targets
add_custom_target(info
    COMMAND ${CMAKE_COMMAND} -E echo "======================================================================="
    COMMAND ${CMAKE_COMMAND} -E echo "                   AbstractX Modern CMake Build System                 "
    COMMAND ${CMAKE_COMMAND} -E echo "======================================================================="
    COMMAND ${CMAKE_COMMAND} -E echo " cmake -B build_host -DABSTRACTX_TARGET=host : Configure Host Build & Simulation"
    COMMAND ${CMAKE_COMMAND} -E echo " cmake -B build_e907 --preset e907           : Configure XuanTie E907 Firmware"
    COMMAND ${CMAKE_COMMAND} -E echo " cmake -B build_pico2w --preset pico2w       : Configure Pico 2 W Firmware"
    COMMAND ${CMAKE_COMMAND} -E echo " cmake --build <dir>                         : Build Target Binaries"
    COMMAND ${CMAKE_COMMAND} -E echo "======================================================================="
    VERBATIM
)

