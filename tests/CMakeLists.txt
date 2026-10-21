# tests/CMakeLists.txt
# CppUTest-based SITL Unit Testing & Hardware Mock Suite

set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "Allow CppUTest legacy cmake_minimum_required")
include(FetchContent)
FetchContent_Declare(
    cpputest
    GIT_REPOSITORY https://github.com/cpputest/cpputest.git
    GIT_TAG v4.0
)
set(TESTS OFF CACHE BOOL "Disable CppUTest internal tests" FORCE)
set(EXTENSIONS ON CACHE BOOL "Enable CppUTest MockSupport extensions" FORCE)
FetchContent_MakeAvailable(cpputest)

# Disable memory leak macro redefinitions when including ETL in CppUTest
add_compile_definitions(CPPUTEST_MEM_LEAK_DETECTION_DISABLED=0)

function(add_abstractx_cpputest TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/targets/linux/include
    )
    target_link_libraries(${TARGET_NAME} PRIVATE
        abstractx_core
        abstractx_target_linux
        CppUTest
        CppUTestExt
    )
    add_test(NAME cpputest_${TARGET_NAME} COMMAND ${TARGET_NAME} -v)
    add_dependencies(test-all ${TARGET_NAME})
endfunction()

add_abstractx_cpputest(test_sitl_imu
    test_main.cpp
    test_sitl_imu.cpp
)

add_abstractx_cpputest(test_sitl_gps
    test_main.cpp
    test_sitl_gps.cpp
)

add_abstractx_cpputest(test_sitl_fusion
    test_main.cpp
    test_sitl_fusion.cpp
)

add_abstractx_cpputest(test_sitl_serial_loopback
    test_main.cpp
    test_sitl_serial_loopback.cpp
)

# SpecTrace tooling ([SPEC-AUDIT-06..08]): plain python3, no pytest required.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME py_test_spectrace_matrix
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_spectrace_matrix.py)
    # [SPEC-E906-IPC-04] Linux-side RPMsg <-> UDP bridge (no hardware needed)
    # [SPEC-AUDIT-09] Trac tools and evidence ledger
    add_test(NAME py_test_trac
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_trac.py)
    add_test(NAME py_test_e906_rpmsg_bridge
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_e906_rpmsg_bridge.py)
    # [SPEC-E906-REPL-15] board suite decoders, raw-REPL parsing and runner (off-board)
    add_test(NAME py_test_board_suite_local
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_board_suite_local.py)
    # [SPEC-STUDIO-*] Studio state, panels and live-vs---sim behaviour; needs pytest + imgui_bundle
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import pytest, imgui_bundle"
                    RESULT_VARIABLE _studio_deps OUTPUT_QUIET ERROR_QUIET)
    # [SPEC-AUDIT-12] pytest-based Python tests that used to run nowhere
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import pytest"
                    RESULT_VARIABLE _pytest_dep OUTPUT_QUIET ERROR_QUIET)
    if(_pytest_dep EQUAL 0)
        foreach(_t test_e906_sensor_protocol.py test_membrowse_tracking.py
                   test_spec_traceability.py test_tlp_and_ctf_schema.py)
            get_filename_component(_n ${_t} NAME_WE)
            add_test(NAME py_${_n}
                     COMMAND ${Python3_EXECUTABLE} -m pytest -q ${CMAKE_CURRENT_SOURCE_DIR}/${_t}
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        endforeach()
    else()
        message(STATUS "pytest-based Python tests skipped: pytest not installed")
    endif()
    # not-in-ctest: tests/test_board_checkout.py needs a board; opt-in with ABSTRACTX_BOARD_IP ([SPEC-AUDIT-13])
    # not-in-ctest: tests/board/test_e906_planes.py runs on the board via run_board_tests.py ([SPEC-E906-REPL-15])
    if(_studio_deps EQUAL 0)
        add_test(NAME py_test_visualizer_sdk
                 COMMAND ${Python3_EXECUTABLE} -m pytest -q ${CMAKE_CURRENT_SOURCE_DIR}/test_visualizer_sdk.py
                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    else()
        message(STATUS "py_test_visualizer_sdk skipped: pytest or imgui_bundle not installed")
    endif()
    # [SPEC-AUDIT-01..05] Adversarial invariant auditor checks
    add_test(NAME py_test_adversarial_invariants
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_adversarial_invariants.py)
    # [SPEC-ZYNQ-PLATFORM-07..11] Zynq Buildroot configuration verification
    add_test(NAME py_test_zynq_buildroot_config
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_zynq_buildroot_config.py)
endif()
