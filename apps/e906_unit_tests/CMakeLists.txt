# apps/e906_unit_tests: the portable unit tests on the E906 ([SPEC-AUDIT-18])
add_executable(e906_unit_tests
    main.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/all_tests.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/unit_tlp.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/unit_ring.cpp
)
target_include_directories(e906_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
target_link_libraries(e906_unit_tests PRIVATE abstractx_target_e906 abstractx_core etl m)
target_link_options(e906_unit_tests PRIVATE
    "-T${E906_LDSCRIPT}"
    -Wl,-Map=e906_unit_tests.map
    -Wl,--gc-sections
    -nostartfiles
)
abstractx_e906_check_layout(e906_unit_tests)
add_custom_command(TARGET e906_unit_tests POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:e906_unit_tests>
    COMMENT "e906_unit_tests memory footprint"
)
