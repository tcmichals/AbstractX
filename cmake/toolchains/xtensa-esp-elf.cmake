# cmake/toolchains/xtensa-esp-elf.cmake
# --------------------------------------
# CMake Toolchain configuration for Xtensa targets (ESP32, ESP32-S3, HiFi4 DSP)
# Standardized location: ~/.tools/xtensa-esp-elf or ~/.tools/xtensa-hifi4-gcc

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR xtensa)

if(NOT DEFINED XTENSA_TOOLCHAIN_PREFIX)
    if(EXISTS "/home/tcmichals/.tools/xtensa-esp-elf/bin/xtensa-esp32-elf-gcc")
        set(XTENSA_TOOLCHAIN_PREFIX "/home/tcmichals/.tools/xtensa-esp-elf/bin/xtensa-esp32-elf-")
    elseif(EXISTS "/home/tcmichals/.tools/xtensa-esp-elf/bin/xtensa-esp-elf-gcc")
        set(XTENSA_TOOLCHAIN_PREFIX "/home/tcmichals/.tools/xtensa-esp-elf/bin/xtensa-esp-elf-")
    elseif(EXISTS "/home/tcmichals/.tools/xtensa-hifi4-gcc/bin/xtensa-hifi4-elf-gcc")
        set(XTENSA_TOOLCHAIN_PREFIX "/home/tcmichals/.tools/xtensa-hifi4-gcc/bin/xtensa-hifi4-elf-")
    else()
        set(XTENSA_TOOLCHAIN_PREFIX "xtensa-esp32-elf-")
    endif()
endif()

set(CMAKE_C_COMPILER   "${XTENSA_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${XTENSA_TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${XTENSA_TOOLCHAIN_PREFIX}gcc")

set(CMAKE_C_FLAGS_INIT   "-mlongcalls -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "-mlongcalls -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti")
set(CMAKE_ASM_FLAGS_INIT "-mlongcalls")

set(CMAKE_OBJCOPY      "${XTENSA_TOOLCHAIN_PREFIX}objcopy" CACHE INTERNAL "objcopy")
set(CMAKE_OBJDUMP      "${XTENSA_TOOLCHAIN_PREFIX}objdump" CACHE INTERNAL "objdump")
set(CMAKE_SIZE         "${XTENSA_TOOLCHAIN_PREFIX}size"    CACHE INTERNAL "size")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
