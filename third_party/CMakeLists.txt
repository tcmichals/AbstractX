# third_party/CMakeLists.txt
# -------------------------
# Orchestrates embedded third-party dependencies (ETL, Pico-SDK, ESP32 components)

# @impl [SPEC-BUILD-01] docs/DESIGN_SPECIFICATION.md (stop at configure when the ETL submodule is missing)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/etl/CMakeLists.txt")
    add_subdirectory(etl EXCLUDE_FROM_ALL)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/etl/include/etl/memory_model.h")
    add_library(etl INTERFACE)
    target_include_directories(etl INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/etl/include>
    )
else()
    message(FATAL_ERROR
        "third_party/etl is not initialised. Run:\n"
        "  git submodule update --init third_party/etl")
endif()
