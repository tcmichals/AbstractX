# apps/CMakeLists.txt
# ------------------
# Build target applications

if(ABSTRACTX_TARGET STREQUAL "e907")
    add_subdirectory(e907_coprocessor)
elseif(ABSTRACTX_TARGET STREQUAL "pico2w")
    add_subdirectory(pico2w_companion)
elseif(ABSTRACTX_TARGET STREQUAL "esp32p4")
    add_subdirectory(esp32p4_hub)
endif()
