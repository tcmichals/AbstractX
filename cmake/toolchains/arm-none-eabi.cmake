# cmake/toolchains/arm-none-eabi.cmake
# -----------------------------------
# CMake Toolchain configuration for ARM Cortex-M targets (RP2350 / Pico 2)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT DEFINED ARM_TOOLCHAIN_PREFIX)
    if(EXISTS "/home/tcmichals/.tools/gcc-arm-none-eabi/bin/arm-none-eabi-gcc")
        set(ARM_TOOLCHAIN_PREFIX "/home/tcmichals/.tools/gcc-arm-none-eabi/bin/arm-none-eabi-")
    elseif(EXISTS "/home/tcmichals/.tools/gcc-arm-none/bin/arm-none-eabi-gcc")
        set(ARM_TOOLCHAIN_PREFIX "/home/tcmichals/.tools/gcc-arm-none/bin/arm-none-eabi-")
    else()
        set(ARM_TOOLCHAIN_PREFIX "arm-none-eabi-")
    endif()
endif()

set(CMAKE_C_COMPILER   "${ARM_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${ARM_TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${ARM_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_OBJCOPY      "${ARM_TOOLCHAIN_PREFIX}objcopy" CACHE INTERNAL "objcopy")
set(CMAKE_OBJDUMP      "${ARM_TOOLCHAIN_PREFIX}objdump" CACHE INTERNAL "objdump")
set(CMAKE_SIZE         "${ARM_TOOLCHAIN_PREFIX}size"    CACHE INTERNAL "size")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
