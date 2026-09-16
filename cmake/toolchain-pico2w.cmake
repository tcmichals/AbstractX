# cmake/toolchain-pico2w.cmake
# ----------------------------
# Toolchain definition for Raspberry Pi Pico 2 W (RP2350 ARM Cortex-M33)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m33)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(PICO_SDK_PATH "/home/tcmichals/.tools/pico/pico-sdk")
set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Pico SDK" FORCE)
set(ENV{PICO_SDK_PATH} "/home/tcmichals/.tools/pico/pico-sdk")

set(PICO_TOOLCHAIN_PATH "/home/tcmichals/.tools/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin")
set(PICO_TOOLCHAIN_PATH "${PICO_TOOLCHAIN_PATH}" CACHE PATH "Path to toolchain" FORCE)
set(ENV{PICO_TOOLCHAIN_PATH} "/home/tcmichals/.tools/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin")

set(PICO_PLATFORM "rp2350-arm-s" CACHE STRING "Pico Platform (RP2350 Secure ARM)")
set(PICO_BOARD "pico2_w" CACHE STRING "Target Board")

set(TOOLCHAIN_PREFIX "/home/tcmichals/.tools/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin/arm-none-eabi-")

set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_PREFIX}objcopy" CACHE FILEPATH "objcopy")
set(CMAKE_OBJDUMP      "${TOOLCHAIN_PREFIX}objdump" CACHE FILEPATH "objdump")
set(CMAKE_SIZE         "${TOOLCHAIN_PREFIX}size"    CACHE FILEPATH "size")

set(ARM_ARCH_FLAGS "-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mcmse")
set(CMAKE_C_FLAGS_INIT   "${ARM_ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ARM_ARCH_FLAGS} -fno-rtti -fno-exceptions")
set(CMAKE_ASM_FLAGS_INIT "${ARM_ARCH_FLAGS}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
