# sim/CMakeLists.txt

function(add_abstractx_sim TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})
    target_link_libraries(${TARGET_NAME} PRIVATE abstractx_core abstractx_target_host)
    add_test(NAME sim_${TARGET_NAME} COMMAND ${TARGET_NAME})
endfunction()

add_abstractx_sim(sitl_coro_sim             sitl_coro_sim.cpp)
add_abstractx_sim(test_baremetal_isr_spsc   test_baremetal_isr_spsc.cpp)
add_abstractx_sim(test_spsc_channel_array   test_spsc_channel_array.cpp)
add_abstractx_sim(test_halo_worker_pool     test_halo_worker_pool.cpp)
add_abstractx_sim(test_tlp_msg              test_tlp_msg.cpp)
add_abstractx_sim(test_io_processor        test_io_processor.cpp)
add_abstractx_sim(test_abstractx_runtime   test_abstractx_runtime.cpp)
add_abstractx_sim(test_trace_zero_copy     test_trace_zero_copy.cpp)
add_abstractx_sim(test_coro_failures       test_coro_failures.cpp)
add_abstractx_sim(test_async_queue_deferred_resume test_async_queue_deferred_resume.cpp)
add_abstractx_sim(test_runtime_stats      test_runtime_stats.cpp)
add_abstractx_sim(test_gps_imu_coro_pool   test_gps_imu_coro_pool.cpp)
target_include_directories(test_gps_imu_coro_pool PRIVATE ${PROJECT_SOURCE_DIR}/apps/gps_imu_app/include)

# E906 RPMsg core and trace transport, run on the host against fake vrings
# and a fake message box ([SPEC-E906-IPC-04]).
set(E906_DIR ${PROJECT_SOURCE_DIR}/targets/allwinner_e906)
add_executable(test_e906_rpmsg_trace
    test_e906_rpmsg_endpoints.cpp
    fakes/e906_msgbox_fake.cpp
    ${E906_DIR}/src/rpmsg.cpp
    ${E906_DIR}/src/rpmsg_endpoints.cpp
)
target_include_directories(test_e906_rpmsg_trace PRIVATE ${E906_DIR}/include)
target_compile_definitions(test_e906_rpmsg_trace PRIVATE CONFIG_RPROC_RPMSG=1)
target_link_libraries(test_e906_rpmsg_trace PRIVATE abstractx_core)
add_test(NAME sim_test_e906_rpmsg_trace COMMAND test_e906_rpmsg_trace)

# E906 RPMsg flow control, torn index rejection, and kick suppression unit test
add_executable(test_e906_rpmsg_flow_control
    test_e906_rpmsg_flow_control.cpp
    fakes/e906_msgbox_fake.cpp
    ${E906_DIR}/src/rpmsg.cpp
)
target_include_directories(test_e906_rpmsg_flow_control PRIVATE ${E906_DIR}/include)
target_compile_definitions(test_e906_rpmsg_flow_control PRIVATE CONFIG_RPROC_RPMSG=1)
target_link_libraries(test_e906_rpmsg_flow_control PRIVATE abstractx_core)
add_test(NAME sim_test_e906_rpmsg_flow_control COMMAND test_e906_rpmsg_flow_control)

# Pico 2 W zero-copy lwIP PBUF_POOL trace transport, run on host with mock lwIP pbufs ([SPEC-PICO-TRACE-01])
set(PICO_DIR ${PROJECT_SOURCE_DIR}/targets/pico2w_rp2350)
add_executable(test_pico_zero_copy_trace
    test_pico_zero_copy_trace.cpp
)
target_include_directories(test_pico_zero_copy_trace PRIVATE ${PICO_DIR}/include)
target_link_libraries(test_pico_zero_copy_trace PRIVATE abstractx_core)
add_test(NAME sim_test_pico_zero_copy_trace COMMAND test_pico_zero_copy_trace)



# Common macro to generate a test target for a specific module
macro(add_cocotb_test TARGET_NAME TOPLEVEL_MODULE TEST_FILE)
    add_custom_target(${TARGET_NAME}
        COMMAND $(MAKE) clean
        COMMAND $(MAKE) TOPLEVEL=${TOPLEVEL_MODULE} MODULE=${TEST_FILE} ${ARGN}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/cocotb
        COMMENT "Running Cocotb test: ${TEST_FILE} on ${TOPLEVEL_MODULE}"
        VERBATIM
    )
endmacro()

add_cocotb_test(sim-test-reg-bank     asp_spi_reg_bank       test_abstractx_axis_cocotb)
add_cocotb_test(sim-test-spi-frontend asp_spi_frontend       test_asp_spi_frontend_cocotb)
add_cocotb_test(sim-test-spi-frontend-single asp_spi_frontend test_asp_spi_frontend_single_cocotb SIM_PARAMS=-GDUAL_SPI_ENABLE=0)
add_cocotb_test(sim-test-axis-fifo    asp_axis_fifo          test_asp_axis_fifo_cocotb)
add_cocotb_test(sim-test-router       asp_router             test_asp_router_cocotb)
add_cocotb_test(sim-test-sys-regs     asp_sys_regs           test_asp_sys_regs_cocotb)
add_cocotb_test(sim-test-wb-master    asp_wishbone_master    test_asp_wishbone_master_cocotb)
add_cocotb_test(sim-test-tlp64-top    asp_top                test_asp_tlp_64b_cocotb)
add_cocotb_test(sim-test-imu-auto-dma asp_imu_auto_dma       test_asp_imu_auto_dma_cocotb)
# not-in-ctest: sim/cocotb/test_asp_top_cocotb.py FAILS: drives spi_mosi, which asp_top lost with Dual-SPI; superseded by test_asp_tlp_64b_cocotb (engineering_log 2026-10-21)
add_cocotb_test(sim-test-axi-lite     asp_axi_lite_bridge    test_asp_axi_lite_bridge_cocotb)
# Protocol timing at each board clock ([SPEC-FPGA-CLK-01]): 27 MHz Tang, 12 MHz CYC1000, 50 MHz default
add_cocotb_test(sim-test-dshot-27m    asp_dshot_core    test_asp_dshot_core_cocotb    "SIM_PARAMS=-GNUM_CHANNELS=1 -GCLK_FREQ_HZ=27000000")
add_cocotb_test(sim-test-dshot-12m    asp_dshot_core    test_asp_dshot_core_cocotb    "SIM_PARAMS=-GNUM_CHANNELS=1 -GCLK_FREQ_HZ=12000000")
add_cocotb_test(sim-test-dshot-50m    asp_dshot_core    test_asp_dshot_core_cocotb    "SIM_PARAMS=-GNUM_CHANNELS=1")
add_cocotb_test(sim-test-neopixel-27m asp_neopixel_core test_asp_neopixel_core_cocotb SIM_PARAMS=-GCLK_FREQ_HZ=27000000)
add_cocotb_test(sim-test-neopixel-12m asp_neopixel_core test_asp_neopixel_core_cocotb SIM_PARAMS=-GCLK_FREQ_HZ=12000000)
add_cocotb_test(sim-test-neopixel-50m asp_neopixel_core test_asp_neopixel_core_cocotb)

# Run all defined simulation targets
add_custom_target(sim-test-all
    DEPENDS sim-test-reg-bank sim-test-spi-frontend sim-test-spi-frontend-single sim-test-axis-fifo sim-test-router sim-test-sys-regs sim-test-wb-master sim-test-tlp64-top sim-test-imu-auto-dma sim-test-axi-lite sim-test-dshot-27m sim-test-dshot-12m sim-test-dshot-50m sim-test-neopixel-27m sim-test-neopixel-12m sim-test-neopixel-50m
    COMMENT "All simulation regression suites complete."
)

# Attach to the top level test-all target
add_dependencies(test-all sim-test-all)
