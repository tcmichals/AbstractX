# sim/CMakeLists.txt

function(add_abstractx_sim TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})
    target_link_libraries(${TARGET_NAME} PRIVATE abstractx_core)
    add_test(NAME sim_${TARGET_NAME} COMMAND ${TARGET_NAME})
endfunction()

add_abstractx_sim(sitl_coro_sim             sitl_coro_sim.cpp)
add_abstractx_sim(test_baremetal_isr_spsc   test_baremetal_isr_spsc.cpp)
add_abstractx_sim(test_spsc_channel_array   test_spsc_channel_array.cpp)
add_abstractx_sim(test_halo_worker_pool     test_halo_worker_pool.cpp)
add_abstractx_sim(test_tlp_msg              test_tlp_msg.cpp)

# Common macro to generate a test target for a specific module
macro(add_cocotb_test TARGET_NAME TOPLEVEL_MODULE TEST_FILE)
    add_custom_target(${TARGET_NAME}
        COMMAND $(MAKE) clean
        COMMAND $(MAKE) TOPLEVEL=${TOPLEVEL_MODULE} MODULE=${TEST_FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/cocotb
        COMMENT "Running Cocotb test: ${TEST_FILE} on ${TOPLEVEL_MODULE}"
        VERBATIM
    )
endmacro()

add_cocotb_test(sim-test-reg-bank     asp_spi_reg_bank       test_abstractx_axis_cocotb)
add_cocotb_test(sim-test-spi-frontend asp_spi_frontend       test_asp_spi_frontend_cocotb)
add_cocotb_test(sim-test-axis-fifo    asp_axis_fifo          test_asp_axis_fifo_cocotb)
add_cocotb_test(sim-test-router       asp_router             test_asp_router_cocotb)
add_cocotb_test(sim-test-sys-regs     asp_sys_regs           test_asp_sys_regs_cocotb)
add_cocotb_test(sim-test-wb-master    asp_wishbone_master    test_asp_wishbone_master_cocotb)
add_cocotb_test(sim-test-tlp64-top    asp_tlp_64b_top        test_asp_tlp_64b_cocotb)

# Run all defined simulation targets
add_custom_target(sim-test-all
    DEPENDS sim-test-reg-bank sim-test-spi-frontend sim-test-axis-fifo sim-test-router sim-test-sys-regs sim-test-wb-master sim-test-tlp64-top
    COMMENT "All simulation regression suites complete."
)

# Attach to the top level test-all target
add_dependencies(test-all sim-test-all)
