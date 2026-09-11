# targets/CMakeLists.txt
# ---------------------
# Select and build appropriate Target HAL & BSP

if(ABSTRACTX_TARGET STREQUAL "e907")
    add_subdirectory(allwinner_e907)
elseif(ABSTRACTX_TARGET STREQUAL "pico2w")
    add_subdirectory(pico2w_rp2350)
elseif(ABSTRACTX_TARGET STREQUAL "esp32p4")
    add_subdirectory(esp32p4)
else()
    # Default to unified Linux target for workstation builds, tests, and SBCs
    add_subdirectory(linux)
endif()
