# cmake/toolchain-e907.cmake
# -------------------------
# Toolchain definition for Allwinner XuanTie E907 RISC-V Co-Processor (T527 / A527)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

set(TOOLCHAIN_PREFIX "/home/tcmichals/.tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-")

set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_PREFIX}objcopy" CACHE FILEPATH "objcopy")
set(CMAKE_OBJDUMP      "${TOOLCHAIN_PREFIX}objdump" CACHE FILEPATH "objdump")
set(CMAKE_SIZE         "${TOOLCHAIN_PREFIX}size"    CACHE FILEPATH "size")

set(RISCV_ARCH_FLAGS "-march=rv32imafc_zicsr_zifencei_zihintpause -mabi=ilp32 -mcmodel=medany")
set(RISCV_COMMON_FLAGS "${RISCV_ARCH_FLAGS} -O2 -g -ffunction-sections -fdata-sections -Wall -Wextra -DCONFIG_CPU_FREQ_MHZ=200 -DTARGET_ALLWINNER_E907=1")

set(CMAKE_C_FLAGS_INIT   "${RISCV_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${RISCV_COMMON_FLAGS} -fno-rtti -fno-exceptions")
set(CMAKE_ASM_FLAGS_INIT "${RISCV_ARCH_FLAGS} -x assembler-with-cpp")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
