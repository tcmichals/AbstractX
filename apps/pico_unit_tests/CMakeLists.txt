# apps/pico_unit_tests: the portable unit tests on the RP2350 ([SPEC-AUDIT-18]); no Wi-Fi, no target lib
add_executable(pico_unit_tests
    main.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/all_tests.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/unit_tlp.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/unit_ring.cpp
)
target_include_directories(pico_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
target_link_libraries(pico_unit_tests PRIVATE abstractx_core pico_stdlib)
target_compile_definitions(pico_unit_tests PRIVATE PICO_DEFAULT_UART=1 PICO_DEFAULT_UART_TX_PIN=8 PICO_DEFAULT_UART_RX_PIN=9)
set_target_properties(pico_unit_tests PROPERTIES SUFFIX ".elf")
pico_add_extra_outputs(pico_unit_tests)
