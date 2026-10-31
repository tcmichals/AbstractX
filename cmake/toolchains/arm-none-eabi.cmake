# cmake/toolchains/arm-none-eabi.cmake
# -----------------------------------
# CMake Toolchain configuration for ARM Cortex-M targets (RP2350 / Pico 2)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# @impl [SPEC-BUILD-01] docs/DESIGN_SPECIFICATION.md (-D value, then $ARM_TOOLCHAIN_PREFIX env, then ~/.tools, then PATH)
if(NOT DEFINED ARM_TOOLCHAIN_PREFIX AND DEFINED ENV{ARM_TOOLCHAIN_PREFIX})
    set(ARM_TOOLCHAIN_PREFIX "$ENV{ARM_TOOLCHAIN_PREFIX}")
endif()
if(NOT DEFINED ARM_TOOLCHAIN_PREFIX)
    file(GLOB _tc_dirs LIST_DIRECTORIES true "$ENV{HOME}/.tools/xpack-arm-none-eabi-gcc-*")
    list(SORT _tc_dirs ORDER DESCENDING)
    foreach(_d IN LISTS _tc_dirs)
        if(NOT DEFINED ARM_TOOLCHAIN_PREFIX AND EXISTS "${_d}/bin/arm-none-eabi-gcc")
            set(ARM_TOOLCHAIN_PREFIX "${_d}/bin/arm-none-eabi-")
        endif()
    endforeach()
    file(GLOB _tc_dirs LIST_DIRECTORIES true "$ENV{HOME}/.tools/gcc-arm-none-eabi")
    list(SORT _tc_dirs ORDER DESCENDING)
    foreach(_d IN LISTS _tc_dirs)
        if(NOT DEFINED ARM_TOOLCHAIN_PREFIX AND EXISTS "${_d}/bin/arm-none-eabi-gcc")
            set(ARM_TOOLCHAIN_PREFIX "${_d}/bin/arm-none-eabi-")
        endif()
    endforeach()
    file(GLOB _tc_dirs LIST_DIRECTORIES true "$ENV{HOME}/.tools/gcc-arm-none")
    list(SORT _tc_dirs ORDER DESCENDING)
    foreach(_d IN LISTS _tc_dirs)
        if(NOT DEFINED ARM_TOOLCHAIN_PREFIX AND EXISTS "${_d}/bin/arm-none-eabi-gcc")
            set(ARM_TOOLCHAIN_PREFIX "${_d}/bin/arm-none-eabi-")
        endif()
    endforeach()
    if(NOT DEFINED ARM_TOOLCHAIN_PREFIX)
        set(ARM_TOOLCHAIN_PREFIX "arm-none-eabi-")
    endif()
endif()

set(CMAKE_C_COMPILER   "${ARM_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${ARM_TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${ARM_TOOLCHAIN_PREFIX}gcc")

set(CMAKE_C_FLAGS_INIT   "-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -Wall -Wextra")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -Wall -Wextra -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp")
# Note: --specs=nano.specs is added by Pico SDK (pico_set_clib) or per-target linker flags
# to avoid interfering with standalone assembly bootloaders like bs2_default.elf.

set(CMAKE_OBJCOPY      "${ARM_TOOLCHAIN_PREFIX}objcopy" CACHE INTERNAL "objcopy")
set(CMAKE_OBJDUMP      "${ARM_TOOLCHAIN_PREFIX}objdump" CACHE INTERNAL "objdump")
set(CMAKE_SIZE         "${ARM_TOOLCHAIN_PREFIX}size"    CACHE INTERNAL "size")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
