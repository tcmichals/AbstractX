# cmake/toolchains/riscv-none-elf.cmake
# ------------------------------------
# CMake Toolchain configuration for XuanTie E907 / RISC-V Bare-metal Targets
# (Allwinner T527 / A733 Co-Processor)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

# 1. Toolchain path detection
if(NOT DEFINED RISCV_TOOLCHAIN_PREFIX)
    if(EXISTS "/home/tcmichals/.tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-gcc")
        set(RISCV_TOOLCHAIN_PREFIX "/home/tcmichals/.tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-")
    elseif(EXISTS "/home/tcmichals/.tools/gcc-riscv-none-eabi/bin/riscv-none-embed-")
        set(RISCV_TOOLCHAIN_PREFIX "/home/tcmichals/.tools/gcc-riscv-none-eabi/bin/riscv-none-embed-")
    else()
        find_program(RISCV_GCC_BIN riscv64-unknown-elf-gcc)
        if(RISCV_GCC_BIN)
            get_filename_component(RISCV_BIN_DIR ${RISCV_GCC_BIN} DIRECTORY)
            set(RISCV_TOOLCHAIN_PREFIX "${RISCV_BIN_DIR}/riscv64-unknown-elf-")
        else()
            set(RISCV_TOOLCHAIN_PREFIX "riscv-none-elf-")
        endif()
    endif()
endif()

set(CMAKE_C_COMPILER   "${RISCV_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${RISCV_TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${RISCV_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_OBJCOPY      "${RISCV_TOOLCHAIN_PREFIX}objcopy" CACHE INTERNAL "objcopy")
set(CMAKE_OBJDUMP      "${RISCV_TOOLCHAIN_PREFIX}objdump" CACHE INTERNAL "objdump")
set(CMAKE_SIZE         "${RISCV_TOOLCHAIN_PREFIX}size"    CACHE INTERNAL "size")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 2. Architecture & ABI flags
# RV32IMAFDC: XuanTie E907 with FPU, Atomics, Compressed instructions
set(ARCH_FLAGS "-march=rv32imafdc_zicsr_zifencei_zihintpause -mabi=ilp32d -mcmodel=medany")

set(CMAKE_C_FLAGS_INIT   "${ARCH_FLAGS} -ffunction-sections -fdata-sections -Wall -Wextra")
set(CMAKE_CXX_FLAGS_INIT "${ARCH_FLAGS} -ffunction-sections -fdata-sections -Wall -Wextra -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics -Wno-template-body")
set(CMAKE_ASM_FLAGS_INIT "${ARCH_FLAGS} -x assembler-with-cpp")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
