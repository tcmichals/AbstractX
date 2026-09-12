# targets/pico2w_rp2350/pico_sdk_import.cmake
# --------------------------------------------
# Automatic Pico SDK discovery for AbstractX

if(NOT DEFINED PICO_SDK_PATH)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/pico-sdk/pico_sdk_init.cmake")
        set(PICO_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/pico-sdk" CACHE PATH "Path to the Pico SDK")
    elseif(EXISTS "/home/tcmichals/.tools/pico/pico-sdk/pico_sdk_init.cmake")
        set(PICO_SDK_PATH "/home/tcmichals/.tools/pico/pico-sdk" CACHE PATH "Path to the Pico SDK")
    elseif(EXISTS "/home/tcmichals/.tools/pico-sdk/pico_sdk_init.cmake")
        set(PICO_SDK_PATH "/home/tcmichals/.tools/pico-sdk" CACHE PATH "Path to the Pico SDK")
    elseif(DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}" CACHE PATH "Path to the Pico SDK")
    endif()
endif()

if(ABSTRACTX_TARGET STREQUAL "pico2w" AND EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
endif()

