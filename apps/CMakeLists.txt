# apps/CMakeLists.txt
# ------------------
# Unified cross-platform sensor application & companion firmware
add_subdirectory(gps_imu_app)


add_subdirectory(serial_loopback_app)
add_subdirectory(e906_repl_app)
add_subdirectory(e906_bench_app)
add_subdirectory(e906_freertos_app)
if(ABSTRACTX_TARGET STREQUAL "e906" OR ABSTRACTX_TARGET STREQUAL "e907")
    add_subdirectory(e906_unit_tests)   # [SPEC-AUDIT-18]
endif()
if(ABSTRACTX_TARGET STREQUAL "pico2w")
    add_subdirectory(pico_unit_tests)   # [SPEC-AUDIT-18]
endif()
