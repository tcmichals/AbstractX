# apps/CMakeLists.txt
# ------------------
# Unified cross-platform sensor application
add_subdirectory(gps_imu_app)

# Target-specific / legacy apps
if(ABSTRACTX_TARGET STREQUAL "e907")
    add_subdirectory(e907_coprocessor)
elseif(ABSTRACTX_TARGET STREQUAL "esp32p4")
    add_subdirectory(esp32p4_hub)
endif()

