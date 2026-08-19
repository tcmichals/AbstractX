# examples/CMakeLists.txt

function(add_abstractx_example TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})
    target_link_libraries(${TARGET_NAME} PRIVATE abstractx_core)
    add_test(NAME example_${TARGET_NAME} COMMAND ${TARGET_NAME})
endfunction()

add_abstractx_example(simple_proof_benchmark            simple_proof_benchmark.cpp)
add_abstractx_example(generic_io_messaging_demo         generic_io_messaging_demo.cpp)
add_abstractx_example(redundant_imu_failover            redundant_imu_failover.cpp)
add_abstractx_example(robotics_multi_axis_motion        robotics_multi_axis_motion.cpp)
add_abstractx_example(protothreads_evolution_comparison protothreads_evolution_comparison.cpp)
add_abstractx_example(protothreads_canonical_suite      protothreads_canonical_suite.cpp)
