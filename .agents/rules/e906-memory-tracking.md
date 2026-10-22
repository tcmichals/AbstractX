# Rule: XuanTie E906 ELF Memory Tracking & Region Analysis

This rule mandates memory footprint tracking and linker symbol analysis for all firmware targeting the **XuanTie E906 RISC-V co-processor** on the Radxa Cubie A5E (Allwinner T527 / A527).

---

## 1. Hardware Memory Allocation Invariants

1. **Pure On-Chip SRAM (Zero DDR)**:
   - Primary firmware execution pool is **SRAM Space 0 (`0x3FFC0000`, 192 KB `SRAM_FAST`)**.
   - Firmwares MUST NOT allocate `.text`, `.data`, `.bss`, or `.stack` in external DDR DRAM (`0x48000000+`).
   - Secondary SRAM Space 1 (`0x40000000`, 256 KB): the upper half (`0x40020000`) is the Linux RPMsg pool (vrings, message buffers) and firmware places nothing there; the lower half holds the resource table, trace0, cold code (`.text_cold`: the MicroPython compiler) and NOLOAD buffers (`.sram_c`) ([SPEC-E906-MEM-01]).
   - MicroPython GC heap is quarantined in dormant Cadence HiFi4 DSP Local RAM (`0x00020000`, 128 KB).

2. **Zero Dynamic Heap (`0 B malloc / new`)**:
   - Firmware must not invoke `malloc`, `free`, `calloc`, `realloc`, or global `new`/`delete`.
   - Verified via `nm -u` / `check_e906_layout.py`.

---

## 2. Mandatory Memory Analysis Tools

Whenever modifying or building E906 firmware, the AI agent MUST run:

1. **ELF Memory & Segment Breakdown**:
   ```bash
   ./tools/analyze_elf_size.sh <path_to_elf> <path_to_ld>
   ```
   Parses linker memory regions (`SRAM_FAST`, `SRAM_A3_2`, `IPC`, `DRAM`), reports used/capacity/free bytes and usage %, and lists top symbol contributors per region.

2. **Map & Symbol Utilization Audit**:
   ```bash
   ./tools/analyze_map.py <path_to_map> <path_to_elf>
   ```
   Cross-references `.map` allocations with `nm --size-sort` to report exact per-symbol memory consumption.

3. **RemoteProc Window Conformance**:
   ```bash
   python3 tools/check_e906_layout.py --readelf riscv-none-elf-readelf <path_to_elf>
   ```
   Ensures all `LOAD` segments fall strictly within remoteproc-mappable SRAM Space 0/1 windows.

---

## 3. Reporting Requirement in Changes & Commit Logs

Every PR, commit message, and agent progress summary touching E906 firmware MUST quote the output from `./tools/analyze_elf_size.sh`:
- Region breakdown (`SRAM_FAST` used / capacity / %).
- Zero DDR allocation proof (`DRAM 0.00 MB used`).
- Top memory contributors.
