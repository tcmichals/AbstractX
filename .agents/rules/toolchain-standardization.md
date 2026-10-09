# Rule: Toolchain Standardization (~/.tools, xPack GNU GCC & newlib-nano)

This rule defines the mandatory cross-compilation environment and C-library standards for all bare-metal silicon targets in AbstractX.

---

## 1. Toolchain Directory Layout (`~/.tools/`)

All cross-compiler toolchains and hardware SDKs are standardized under `~/.tools/`:

| Target Architecture | Toolchain Package | Expected Root Path | Binary Prefix |
| :--- | :--- | :--- | :--- |
| **ARM Cortex-M (Pico 2 / RP2350)** | xPack GNU Arm Embedded GCC | `~/.tools/xpack-arm-none-eabi-gcc-*/` | `arm-none-eabi-` |
| **RISC-V (Allwinner E906 / ESP32-P4)** | xPack GNU RISC-V Embedded GCC | `~/.tools/xpack-riscv-none-elf-gcc-*/` | `riscv-none-elf-` |
| **Xtensa (ESP32 / ESP32-S3 / HiFi4 DSP)** | Espressif / Cadence Xtensa GCC | `~/.tools/xtensa-esp-elf/` or `~/.tools/xtensa-hifi4-gcc/` | `xtensa-esp32-elf-`, `xtensa-esp-elf-` |
| **Raspberry Pi Pico SDK** | Official Pico SDK | `~/.tools/pico/pico-sdk/` | `PICO_SDK_PATH` |
| **FPGA Synthesis & PnR** | Yosys / NextPNR / OpenFPGALoader | `~/.tools/oss-cad-suite/` | Native CAD PATH |

### CMake Detection Precedence
Toolchain files (`cmake/toolchains/arm-none-eabi.cmake`, `cmake/toolchains/riscv-none-elf.cmake`, and `cmake/toolchains/xtensa-esp-elf.cmake`) MUST follow this search order:
1. Exact match in `~/.tools/` (`xpack-*`, `xtensa-*`)
2. Environment variables (`ARM_TOOLCHAIN_PREFIX`, `RISCV_TOOLCHAIN_PREFIX`, `XTENSA_TOOLCHAIN_PREFIX`, `PICO_SDK_PATH`)
3. System `PATH` fallback

---

## 2. The Nano Libc Mandate (`--specs=nano.specs`)

Bare-metal firmware MUST NOT link standard full-size `glibc` or default `newlib` libraries, which inject bloated floating-point `printf`, thread-local storage overhead, and dynamic `malloc` stubs.

### Invariant Rules:
1. **Always Pass `--specs=nano.specs`**:
   Linker flags for bare-metal ARM and RISC-V binaries MUST include `--specs=nano.specs`.
2. **Eliminate Dynamic Stubs (`-nostartfiles` / `--specs=nosys.specs`)**:
   Target binaries provide their own startup assembly (`startup.S`) and vector tables.
3. **Freestanding C++20 Flags**:
   All embedded targets compile with:
   `-fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics -ffunction-sections -fdata-sections -Wl,--gc-sections`
4. **Symbol Level Zero-Heap Audit**:
   Every ELF binary is verified via `tools/track_memory_footprint.py` (`nm -u`) to guarantee `0 B` dynamic heap allocation.
