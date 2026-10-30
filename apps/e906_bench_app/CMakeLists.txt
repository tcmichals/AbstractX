# apps/e906_bench_app/CMakeLists.txt
# Bare-metal AbstractX runtime benchmark for the XuanTie E906.
# @impl [SPEC-E906-BENCH-01] apps/e906_bench/SPECIFICATION.md

if(NOT (ABSTRACTX_TARGET STREQUAL "e906" OR ABSTRACTX_TARGET STREQUAL "allwinner_e906"))
    return()
endif()

add_executable(e906_bench_app
    src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../e906_bench/bench.cpp
)
target_include_directories(e906_bench_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../e906_bench)
target_link_libraries(e906_bench_app PRIVATE abstractx_core abstractx_applib abstractx_target_e906 etl)
target_link_options(e906_bench_app PRIVATE
    "-T${E906_LDSCRIPT}"
    -Wl,-Map=e906_bench_app.map
    -Wl,--gc-sections
    -nostartfiles
)
abstractx_e906_check_layout(e906_bench_app)
