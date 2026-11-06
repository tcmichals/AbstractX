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
# [SPEC-ARCH-15] the host drives AbstractX: step() deadline, wake route, steady state
add_abstractx_sim(test_host_drive          test_host_drive.cpp)
# [SPEC-HAL-05] sleeps never wait for each other (review 2026-11-04 T-1, T-2)
add_abstractx_sim(test_timer_concurrency   test_timer_concurrency.cpp)
# [SPEC-ARCH-15] every enqueue from another context wakes the host (review T-5)
add_abstractx_sim(test_wake_on_enqueue     test_wake_on_enqueue.cpp)
# [SPEC-ARCH-16] host conformance checks, POSIX host
add_abstractx_sim(test_host_conformance_posix test_host_conformance_posix.cpp)
# [SPEC-ARCH-16] requirement 5: every post goes through the host's executor
add_abstractx_sim(test_executor            test_executor.cpp)
# [SPEC-HAL-09] Linux I2C never reports Ok without a transfer (review Q-1)
add_abstractx_sim(test_linux_i2c_no_false_ok test_linux_i2c_no_false_ok.cpp)
# [SPEC-ARCH-09] a refused TLP request is an error, never stale data (review Q-2, Q-12)
add_abstractx_sim(test_tlp_refused_request test_tlp_refused_request.cpp)
add_abstractx_sim(test_tlp_wire_order      test_tlp_wire_order.cpp)
add_abstractx_sim(test_platform_ports      test_platform_ports.cpp)
# [SPEC-HAL-10] UART write never reports Ok for bytes not sent (review Q-8)
add_abstractx_sim(test_linux_uart_short_write test_linux_uart_short_write.cpp)
# [SPEC-ARCH-13] auto channel trigger vs completion on another thread (review Q-3)
add_abstractx_sim(test_io_engine_trigger_race test_io_engine_trigger_race.cpp)
# [SPEC-ARCH-10] AsyncQueue: waiter lists, hand-off, counted rejects (review Q-4)
add_abstractx_sim(test_async_queue_waiters test_async_queue_waiters.cpp)
# [SPEC-ARCH-16] 8 cancellation: destroyed tasks, when_any losers, driver requests (review T-3, Q-5..Q-9)
add_abstractx_sim(test_cancellation        test_cancellation.cpp)
# [SPEC-ARCH-16] 4 and 9: stream sequence numbers; unmapped stream data counted per channel
add_abstractx_sim(test_stream_accounting   test_stream_accounting.cpp)
target_include_directories(test_cancellation PRIVATE ${CMAKE_SOURCE_DIR}/targets/linux/include)
target_include_directories(test_linux_uart_short_write PRIVATE ${CMAKE_SOURCE_DIR}/targets/linux/include)
target_include_directories(test_linux_i2c_no_false_ok PRIVATE ${CMAKE_SOURCE_DIR}/targets/linux/include)
target_link_libraries(test_host_conformance_posix PRIVATE abstractx_host_posix)
target_include_directories(test_wake_on_enqueue PRIVATE ${CMAKE_SOURCE_DIR}/targets/linux/include)
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

# E906 timer: the line-7 handler only wakes; step() posts ([SPEC-E906-IRQ-03], [SPEC-ARCH-16] 3)
add_executable(test_e906_timer_irq test_e906_timer_irq.cpp ${E906_DIR}/src/timer.cpp)
target_include_directories(test_e906_timer_irq BEFORE PRIVATE ${E906_DIR}/include ${E906_DIR}/include/hal)
target_compile_options(test_e906_timer_irq PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/e906_shim/fake_mtime.h)
target_link_libraries(test_e906_timer_irq PRIVATE abstractx_core)
add_test(NAME sim_test_e906_timer_irq COMMAND test_e906_timer_irq)

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

# Pico 2 W non-blocking Wi-Fi join state machine, pure C++ on host ([SPEC-PICO-NET-01])
add_executable(test_pico_wifi_join test_pico_wifi_join.cpp)
target_include_directories(test_pico_wifi_join PRIVATE ${PICO_DIR}/include)
add_test(NAME sim_test_pico_wifi_join COMMAND test_pico_wifi_join)

# Pico 2 W Core 1 -> Core 0 frame ring, two host threads ([SPEC-PICO-CORE-01])
find_package(Threads REQUIRED)
add_executable(test_pico_frame_ring test_pico_frame_ring.cpp)
target_include_directories(test_pico_frame_ring PRIVATE ${PICO_DIR}/include)
target_link_libraries(test_pico_frame_ring PRIVATE Threads::Threads)
add_test(NAME sim_test_pico_frame_ring COMMAND test_pico_frame_ring)

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
# [SPEC-ARCH-06] hosted idle wait sleeps, never spins (EL-1027-18): one binary per hosted target file
foreach(_idle_target linux zynq7000)
    add_executable(test_hosted_idle_wait_${_idle_target} test_hosted_idle_wait.cpp
                   ${PROJECT_SOURCE_DIR}/targets/${_idle_target}/src/hal_platform.cpp)
    target_include_directories(test_hosted_idle_wait_${_idle_target} PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_link_libraries(test_hosted_idle_wait_${_idle_target} PRIVATE etl pthread)
    add_test(NAME sim_test_hosted_idle_wait_${_idle_target} COMMAND test_hosted_idle_wait_${_idle_target})
endforeach()
# Each cocotb suite is a make target (sim-test-*) and a ctest (rtl_sim-test-*, label rtl).
# @impl [SPEC-AUDIT-12] docs/SPECTRACE.md (cocotb suites run by ctest; visible skips)
option(ABSTRACTX_RTL_TESTS "Register the cocotb RTL suites with ctest" ON)
find_program(ABX_VERILATOR verilator)
find_program(ABX_COCOTB_CONFIG cocotb-config)
find_program(ABX_MAKE NAMES make gmake)
set(_cocotb_dir ${CMAKE_CURRENT_SOURCE_DIR}/cocotb)
set(_pio_rtl ${PROJECT_SOURCE_DIR}/rtl/pio/third_party/verilog-axi/rtl/axil_reg_if.v)  # submodule files the RTL needs
set(_pio_toplevels asp_top asp_pio_wrap asp_sys_regs asp_ctrl_plane)                 # suites that read rtl/pio

macro(add_cocotb_test TARGET_NAME TOPLEVEL_MODULE TEST_FILE)
    add_custom_target(${TARGET_NAME}
        COMMAND $(MAKE) clean
        COMMAND $(MAKE) TOPLEVEL=${TOPLEVEL_MODULE} MODULE=${TEST_FILE} ${ARGN}
        WORKING_DIRECTORY ${_cocotb_dir}
        COMMENT "Running Cocotb test: ${TEST_FILE} on ${TOPLEVEL_MODULE}"
        VERBATIM
    )
    list(APPEND _all_cocotb_targets ${TARGET_NAME})   # sim-test-all depends on every registered suite
    if(ABSTRACTX_RTL_TESTS)
        set(_skip "")
        if(NOT ABX_VERILATOR OR NOT ABX_COCOTB_CONFIG OR NOT ABX_MAKE)
            set(_skip "verilator, cocotb-config or make not found")
        elseif(TOPLEVEL_MODULE IN_LIST _pio_toplevels AND NOT EXISTS ${_pio_rtl})
            set(_skip "rtl/pio submodule not checked out: git submodule update --init --recursive rtl/pio")
        endif()
        if(_skip)
            add_test(NAME rtl_${TARGET_NAME} COMMAND sh -c "echo 'skip: ${_skip}'; exit 77")
        else()
            set(_out ${CMAKE_CURRENT_BINARY_DIR}/cocotb/${TARGET_NAME})
            # PWD: the cocotb Makefile finds the RTL through $(PWD), which a chdir does not update.
            add_test(NAME rtl_${TARGET_NAME}
                     COMMAND ${CMAKE_COMMAND} -E env PWD=${_cocotb_dir}
                             ${ABX_MAKE} -s TOPLEVEL=${TOPLEVEL_MODULE} MODULE=${TEST_FILE}
                             SIM_BUILD=${_out} COCOTB_RESULTS_FILE=${_out}/results.xml ${ARGN}
                     WORKING_DIRECTORY ${_cocotb_dir})
        endif()
        set_tests_properties(rtl_${TARGET_NAME} PROPERTIES LABELS rtl SKIP_RETURN_CODE 77 TIMEOUT 60)  # runaway guard: slowest 11.7 s (2026-11-05)
    endif()
endmacro()

add_cocotb_test(sim-test-reg-bank     asp_spi_reg_bank       test_abstractx_axis_cocotb)
add_cocotb_test(sim-test-spi-frontend asp_spi_frontend       test_asp_spi_frontend_cocotb)
add_cocotb_test(sim-test-spi-frontend-single asp_spi_frontend test_asp_spi_frontend_single_cocotb SIM_PARAMS=-GDUAL_SPI_ENABLE=0)
add_cocotb_test(sim-test-axis-fifo    asp_axis_fifo          test_asp_axis_fifo_cocotb)
add_cocotb_test(sim-test-router       asp_router             test_asp_router_cocotb)
add_cocotb_test(sim-test-sys-regs     asp_sys_regs           test_asp_sys_regs_cocotb)
# [SPEC-TLP-04] control TLPs -> AXI4-Lite (replaces the Wishbone master, 2026-11-05)
add_cocotb_test(sim-test-axil-master  asp_ctrl_plane         test_asp_tlp_axil_master_cocotb)
add_cocotb_test(sim-test-tlp64-top    asp_top                test_asp_top_pio_cocotb)
add_cocotb_test(sim-test-axi-lite     asp_axi_lite_bridge    test_asp_axi_lite_bridge_cocotb)
add_cocotb_test(sim-test-pio-wrap     asp_pio_wrap      test_asp_pio_wrap_cocotb)
# [SPEC-TLP-06] PIO TLP data path through the wrapper (moved from pio2)
add_cocotb_test(sim-test-pio-tlp-path asp_pio_wrap      test_asp_pio_tlp_path_cocotb)
# The PIO tile's own regression (tile, conformance, DShot, ESC serial, TLP path) lives in the
# rtl/pio submodule (pio2): cd rtl/pio/tests/cocotb && uv run make MODULE=<test>. asp_top above
# still builds the PIO RTL from the submodule.

# Run all defined simulation targets
add_custom_target(sim-test-all
    DEPENDS ${_all_cocotb_targets}
    COMMENT "All simulation regression suites complete."
)

# Attach to the top level test-all target
add_dependencies(test-all sim-test-all)
add_abstractx_sim(test_uart_config        test_uart_config.cpp)
add_abstractx_sim(test_spi_transactions   test_spi_transactions.cpp)
add_abstractx_sim(test_i2c_messages       test_i2c_messages.cpp)
# [SPEC-MAG-01] applib HMC5883L / QMC5883L drivers on a fake I2C bus (EL-1027-23)
add_abstractx_sim(test_mag_drivers        test_mag_drivers.cpp)
target_link_libraries(test_mag_drivers PRIVATE abstractx_applib)
add_abstractx_sim(test_hal_cancel         test_hal_cancel.cpp)
add_abstractx_sim(test_uart_rx_ring       test_uart_rx_ring.cpp)
add_abstractx_sim(test_wake_handshake    test_wake_handshake.cpp)
add_abstractx_sim(test_asserts_live      test_asserts_live.cpp)
add_abstractx_sim(test_smp_lock          test_smp_lock.cpp)
add_abstractx_sim(test_coroutine_io_engine test_coroutine_io_engine.cpp)
add_abstractx_sim(test_esp32p4_stubs     test_esp32p4_stubs.cpp)
add_abstractx_sim(test_bus_protocols      test_bus_protocols.cpp)
add_abstractx_sim(test_coro_combinators   test_coro_combinators.cpp)

# [SPEC-AUDIT-17] every test here needs the build_tree fixture: ctest builds first (review G-1)
get_property(_abx_tests DIRECTORY PROPERTY TESTS)
if(_abx_tests)
    set_tests_properties(${_abx_tests} PROPERTIES FIXTURES_REQUIRED abx_build)
endif()
