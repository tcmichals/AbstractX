# apps/CMakeLists.txt
# ------------------
# Unified cross-platform sensor application & companion firmware
add_subdirectory(gps_imu_app)


add_subdirectory(serial_loopback_app)
add_subdirectory(e906_repl_app)
add_subdirectory(e906_bench_app)
add_subdirectory(e906_freertos_app)
add_subdirectory(inav_e906_app)
