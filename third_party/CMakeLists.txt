# third_party/CMakeLists.txt
# -------------------------
# Orchestrates embedded third-party dependencies (ETL, Pico-SDK, ESP32 components)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/etl/CMakeLists.txt")
    add_subdirectory(etl EXCLUDE_FROM_ALL)
else()
    # Fallback to direct header include if submodule not initialized
    add_library(etl INTERFACE)
    target_include_directories(etl INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/etl/include>
    )
endif()
