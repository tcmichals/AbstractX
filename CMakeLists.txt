cmake_minimum_required(VERSION 3.20)
project(AbstractX NONE)

# Top level custom target to run all tests
add_custom_target(test-all
    COMMENT "Running the complete simulation regression suite"
    VERBATIM
)

# Custom helper to describe our targets (replaces default make help ambiguity)
add_custom_target(info
    COMMAND ${CMAKE_COMMAND} -E echo "======================================================="
    COMMAND ${CMAKE_COMMAND} -E echo "               AbstractX Build Commands                "
    COMMAND ${CMAKE_COMMAND} -E echo "======================================================="
    COMMAND ${CMAKE_COMMAND} -E echo " make test-all             : Run the complete simulation regression suite."
    COMMAND ${CMAKE_COMMAND} -E echo " make sim-test-reg-bank    : Run isolated Cocotb test on asp_spi_reg_bank."
    COMMAND ${CMAKE_COMMAND} -E echo " make sim-test-spi-frontend: Run isolated Cocotb test on asp_spi_frontend."
    COMMAND ${CMAKE_COMMAND} -E echo " make info                 : Print this help message."
    COMMAND ${CMAKE_COMMAND} -E echo "======================================================="
    VERBATIM
)

# Traverse into simulation directory
add_subdirectory(sim)
