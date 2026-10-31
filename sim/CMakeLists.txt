# sim/CMakeLists.txt
# Tests check with assert(): keep it live in Release (CMake adds -DNDEBUG there). Until
# 2026-10-28 eleven sim tests (~276 asserts) checked nothing in the default host build.
add_compile_options(-UNDEBUG)


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
target_link_libraries(test_tlp_msg PRIVATE abstractx_drivers)
add_abstractx_sim(test_io_processor        test_io_processor.cpp)
add_abstractx_sim(test_io_engine           test_io_engine.cpp)
# [SPEC-E906-HAL-03] real E906 spi.cpp against fake registers (sim/e906_shim) and a fake DMAC
add_executable(test_e906_spi1_ownership test_e906_spi1_ownership.cpp
               ${CMAKE_SOURCE_DIR}/targets/allwinner_e906/src/spi.cpp)
target_include_directories(test_e906_spi1_ownership BEFORE PRIVATE
               ${CMAKE_CURRENT_SOURCE_DIR}/e906_shim ${CMAKE_SOURCE_DIR}/targets/allwinner_e906/include)
target_link_libraries(test_e906_spi1_ownership PRIVATE abstractx_core)
add_test(NAME sim_test_e906_spi1_ownership COMMAND test_e906_spi1_ownership)
# [SPEC-E906-HAL-01] real E906 dma.cpp against a fake DMAC: completion acts on PEND & EN only
add_executable(test_e906_dma_irq test_e906_dma_irq.cpp
               ${CMAKE_SOURCE_DIR}/targets/allwinner_e906/src/dma.cpp)
target_include_directories(test_e906_dma_irq BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/targets/allwinner_e906/include)
target_compile_options(test_e906_dma_irq PRIVATE
               -include ${CMAKE_CURRENT_SOURCE_DIR}/e906_shim/fake_dmac.h)
target_link_libraries(test_e906_dma_irq PRIVATE abstractx_core)
add_test(NAME sim_test_e906_dma_irq COMMAND test_e906_dma_irq)
# [SPEC-LINUX-GPIO-01] GPIO-edge auto channel on SITL
add_abstractx_sim(test_linux_drdy_auto     test_linux_drdy_auto.cpp)
# [SPEC-LINUX-HAL-01] real Linux SPI/I2C drivers deliver their own results
add_abstractx_sim(test_linux_hal_completion test_linux_hal_completion.cpp)
add_abstractx_sim(test_abstractx_runtime   test_abstractx_runtime.cpp)
add_abstractx_sim(test_trace_zero_copy     test_trace_zero_copy.cpp)
add_abstractx_sim(test_coro_failures       test_coro_failures.cpp)
add_abstractx_sim(test_async_queue_deferred_resume test_async_queue_deferred_resume.cpp)
add_abstractx_sim(test_runtime_stats      test_runtime_stats.cpp)
add_abstractx_sim(test_event_trace        test_event_trace.cpp)
add_abstractx_sim(test_gps_imu_coro_pool   test_gps_imu_coro_pool.cpp)
target_include_directories(test_gps_imu_coro_pool PRIVATE ${PROJECT_SOURCE_DIR}/apps/gps_imu_app/include)
target_link_libraries(test_gps_imu_coro_pool PRIVATE abstractx_drivers)

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

# E906 TWI driver against a register model of the controller and one slave ([SPEC-E906-HAL-04])
add_executable(test_e906_twi
    test_e906_twi.cpp
    ${E906_DIR}/src/i2c.cpp
    ${E906_DIR}/src/driver_wait.cpp
)
target_include_directories(test_e906_twi PRIVATE ${E906_DIR}/include)
target_link_libraries(test_e906_twi PRIVATE abstractx_core)
add_test(NAME sim_test_e906_twi COMMAND test_e906_twi)

# E906 UART driver against a register model of the 8250 and a fake DMAC ([SPEC-E906-HAL-04])
add_executable(test_e906_uart
    test_e906_uart.cpp
    ${E906_DIR}/src/uart.cpp
    ${E906_DIR}/src/driver_wait.cpp
)
target_include_directories(test_e906_uart PRIVATE ${E906_DIR}/include)
target_link_libraries(test_e906_uart PRIVATE abstractx_core)
add_test(NAME sim_test_e906_uart COMMAND test_e906_uart)

# Pico 2 W zero-copy lwIP PBUF_POOL trace transport, run on host with mock lwIP pbufs ([SPEC-PICO-TRACE-01])
set(PICO_DIR ${PROJECT_SOURCE_DIR}/targets/pico2w_rp2350)
add_executable(test_pico_zero_copy_trace
    test_pico_zero_copy_trace.cpp
)
target_include_directories(test_pico_zero_copy_trace PRIVATE ${PICO_DIR}/include)
target_link_libraries(test_pico_zero_copy_trace PRIVATE abstractx_core)
add_test(NAME sim_test_pico_zero_copy_trace COMMAND test_pico_zero_copy_trace)

# Zynq-7000 UIO Bridge & Target IoProcessor data plane test ([SPEC-ZYNQ-01..03])
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_zynq_target.cpp)
    set(ZYNQ_DIR ${PROJECT_SOURCE_DIR}/targets/zynq7000)
    add_executable(test_zynq_target
        test_zynq_target.cpp
        ${ZYNQ_DIR}/src/io_processor.cpp
    )
    target_include_directories(test_zynq_target PRIVATE ${ZYNQ_DIR}/include)
    target_link_libraries(test_zynq_target PRIVATE abstractx_core)
    add_test(NAME sim_test_zynq_target COMMAND test_zynq_target)
endif()
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
add_cocotb_test(sim-test-axi-lite     asp_axi_lite_bridge    test_asp_axi_lite_bridge_cocotb)
# Protocol timing at each board clock ([SPEC-FPGA-CLK-01]): 27 MHz Tang, 12 MHz CYC1000, 50 MHz default
add_cocotb_test(sim-test-dshot-27m    asp_dshot_core    test_asp_dshot_core_cocotb    "SIM_PARAMS=-GNUM_CHANNELS=1 -GCLK_FREQ_HZ=27000000")
add_cocotb_test(sim-test-dshot-12m    asp_dshot_core    test_asp_dshot_core_cocotb    "SIM_PARAMS=-GNUM_CHANNELS=1 -GCLK_FREQ_HZ=12000000")
add_cocotb_test(sim-test-dshot-50m    asp_dshot_core    test_asp_dshot_core_cocotb    "SIM_PARAMS=-GNUM_CHANNELS=1")
add_cocotb_test(sim-test-neopixel-27m asp_neopixel_core test_asp_neopixel_core_cocotb SIM_PARAMS=-GCLK_FREQ_HZ=27000000)
add_cocotb_test(sim-test-neopixel-12m asp_neopixel_core test_asp_neopixel_core_cocotb SIM_PARAMS=-GCLK_FREQ_HZ=12000000)
add_cocotb_test(sim-test-neopixel-50m asp_neopixel_core test_asp_neopixel_core_cocotb)
add_cocotb_test(sim-test-pio-wrap     asp_pio_wrap      test_asp_pio_wrap_cocotb)
# The PIO tile's own regression (tile, conformance, DShot, ESC serial, TLP path) lives in the
# rtl/pio submodule (pio2): cd rtl/pio/tests/cocotb && uv run make MODULE=<test>. asp_top above
# still builds the PIO RTL from the submodule.

# Run all defined simulation targets
add_custom_target(sim-test-all
    DEPENDS sim-test-reg-bank sim-test-spi-frontend sim-test-spi-frontend-single sim-test-axis-fifo sim-test-router sim-test-sys-regs sim-test-wb-master sim-test-tlp64-top sim-test-imu-auto-dma sim-test-axi-lite sim-test-dshot-27m sim-test-dshot-12m sim-test-dshot-50m sim-test-neopixel-27m sim-test-neopixel-12m sim-test-neopixel-50m sim-test-pio-wrap
    COMMENT "All simulation regression suites complete."
)

# Attach to the top level test-all target
add_dependencies(test-all sim-test-all)
add_abstractx_sim(test_uart_config        test_uart_config.cpp)
add_abstractx_sim(test_spi_transactions   test_spi_transactions.cpp)
add_abstractx_sim(test_i2c_messages       test_i2c_messages.cpp)
add_abstractx_sim(test_hal_cancel         test_hal_cancel.cpp)
add_abstractx_sim(test_uart_rx_ring       test_uart_rx_ring.cpp)
add_abstractx_sim(test_wake_handshake    test_wake_handshake.cpp)
add_abstractx_sim(test_asserts_live      test_asserts_live.cpp)
add_abstractx_sim(test_smp_lock          test_smp_lock.cpp)
add_abstractx_sim(test_coroutine_io_engine test_coroutine_io_engine.cpp)
add_abstractx_sim(test_esp32p4_stubs     test_esp32p4_stubs.cpp)
add_abstractx_sim(test_bus_protocols      test_bus_protocols.cpp)
add_abstractx_sim(test_coro_combinators   test_coro_combinators.cpp)
