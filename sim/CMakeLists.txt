# sim/CMakeLists.txt

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
add_cocotb_test(sim-test-top          asp_top                test_asp_top_cocotb)

# Run all defined simulation targets
add_custom_target(sim-test-all
    DEPENDS sim-test-reg-bank sim-test-spi-frontend sim-test-axis-fifo sim-test-router sim-test-sys-regs sim-test-wb-master sim-test-top
    COMMENT "All simulation regression suites complete."
)

# Attach to the top level test-all target
add_dependencies(test-all sim-test-all)
