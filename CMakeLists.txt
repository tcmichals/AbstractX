cmake_minimum_required(VERSION 3.20)
project(AbstractX VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# Core AbstractX Interface Library
add_library(abstractx_core INTERFACE)
target_include_directories(abstractx_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(abstractx_core INTERFACE cxx_std_20)
target_link_libraries(abstractx_core INTERFACE Threads::Threads)

enable_testing()

# Top level custom target to run all tests
add_custom_target(test-all
    COMMENT "Running the complete simulation regression suite"
    VERBATIM
)

# Custom helper to describe our targets
add_custom_target(info
    COMMAND ${CMAKE_COMMAND} -E echo "======================================================================="
    COMMAND ${CMAKE_COMMAND} -E echo "                   AbstractX Modern CMake Build System                 "
    COMMAND ${CMAKE_COMMAND} -E echo "======================================================================="
    COMMAND ${CMAKE_COMMAND} -E echo " cmake -B build                      : Configure build tree"
    COMMAND ${CMAKE_COMMAND} -E echo " cmake --build build                 : Build all simulation & example targets"
    COMMAND ${CMAKE_COMMAND} -E echo " ctest --test-dir build --output-on-failure : Run all verification tests"
    COMMAND ${CMAKE_COMMAND} -E echo " cmake --build build --target simple_proof_benchmark : Build multi-target benchmark"
    COMMAND ${CMAKE_COMMAND} -E echo " cmake --build build --target info   : Print this help message"
    COMMAND ${CMAKE_COMMAND} -E echo "======================================================================="
    VERBATIM
)

add_subdirectory(sim)
add_subdirectory(examples)
