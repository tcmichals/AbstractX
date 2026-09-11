# targets/esp32p4/CMakeLists.txt
# -----------------------------
# ESP-IDF Component Definition for AbstractX on ESP32-P4

if(DEFINED IDF_TARGET)
    idf_component_register(
        SRCS
            "src/hal_timer.cpp"
            "src/hal_spi.cpp"
            "src/hal_mailbox.cpp"
        INCLUDE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
        REQUIRES
            driver
            esp_timer
            freertos
    )

    target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
    target_link_libraries(${COMPONENT_LIB} PUBLIC etl)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC TARGET_ESP32P4=1)
else()
    # Standalone CMake stub when outside ESP-IDF
    add_library(abstractx_target_esp32p4 INTERFACE)
    target_include_directories(abstractx_target_esp32p4 INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
endif()
