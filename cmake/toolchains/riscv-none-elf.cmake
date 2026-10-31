# cmake/toolchains/riscv-none-elf.cmake
# ------------------------------------
# CMake Toolchain configuration for XuanTie E907 / RISC-V Bare-metal Targets
# (Allwinner T527 / A733 Co-Processor)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

# 1. Toolchain path detection
# @impl [SPEC-BUILD-01] docs/DESIGN_SPECIFICATION.md (-D value, then $RISCV_TOOLCHAIN_PREFIX env, then ~/.tools, then PATH)
if(NOT DEFINED RISCV_TOOLCHAIN_PREFIX AND DEFINED ENV{RISCV_TOOLCHAIN_PREFIX})
    set(RISCV_TOOLCHAIN_PREFIX "$ENV{RISCV_TOOLCHAIN_PREFIX}")
endif()
if(NOT DEFINED RISCV_TOOLCHAIN_PREFIX)
    file(GLOB _tc_dirs LIST_DIRECTORIES true "$ENV{HOME}/.tools/xpack-riscv-none-elf-gcc-*")
    list(SORT _tc_dirs ORDER DESCENDING)
    foreach(_d IN LISTS _tc_dirs)
        if(NOT DEFINED RISCV_TOOLCHAIN_PREFIX AND EXISTS "${_d}/bin/riscv-none-elf-gcc")
            set(RISCV_TOOLCHAIN_PREFIX "${_d}/bin/riscv-none-elf-")
        endif()
    endforeach()
    file(GLOB _tc_dirs LIST_DIRECTORIES true "$ENV{HOME}/.tools/gcc-riscv-none-eabi")
    list(SORT _tc_dirs ORDER DESCENDING)
    foreach(_d IN LISTS _tc_dirs)
        if(NOT DEFINED RISCV_TOOLCHAIN_PREFIX AND EXISTS "${_d}/bin/riscv-none-embed-gcc")
            set(RISCV_TOOLCHAIN_PREFIX "${_d}/bin/riscv-none-embed-")
        endif()
    endforeach()
    if(NOT DEFINED RISCV_TOOLCHAIN_PREFIX)
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
# XuanTie E906 on the T527 is RV32IMAFCX: single-precision FPU, no D (misa 0x40901125,
# [SPEC-E906-ARCH-01]). With D enabled every double instruction is illegal on the core.
set(ARCH_FLAGS "-march=rv32imafc_zicsr_zifencei_zihintpause -mabi=ilp32f -mcmodel=medany")

set(CMAKE_C_FLAGS_INIT   "${ARCH_FLAGS} -ffunction-sections -fdata-sections -Wall -Wextra")
set(CMAKE_CXX_FLAGS_INIT "${ARCH_FLAGS} -ffunction-sections -fdata-sections -Wall -Wextra -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics -Wno-template-body")
set(CMAKE_ASM_FLAGS_INIT "${ARCH_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "--specs=nano.specs -Wl,--gc-sections")

# Build types. Debug uses -Og, not CMake's default -O0: at -O0 the E906 image
# overflows its 192 KB SRAM_FAST window. -g costs no target memory.
set(CMAKE_C_FLAGS_DEBUG_INIT     "-Og -g3")
set(CMAKE_CXX_FLAGS_DEBUG_INIT   "-Og -g3")
set(CMAKE_C_FLAGS_RELEASE_INIT   "-O2 -g -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "-O2 -g -DNDEBUG")
# CMake appends its own "-O3 -DNDEBUG" after *_INIT, and the last -O wins: Release was really
# -O3, and e906_repl_app overflowed SRAM_FAST (2026-10-28). Seed the cache so -O2 holds.
set(CMAKE_C_FLAGS_RELEASE   "-O2 -g -DNDEBUG" CACHE STRING "E906 release flags")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g -DNDEBUG" CACHE STRING "E906 release flags")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
