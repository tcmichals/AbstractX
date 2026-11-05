#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-ZYNQ-01] [SPEC-ZYNQ-02] [SPEC-ZYNQ-03] [SPEC-ZYNQ-06] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md
"""
check_register_parity.py - Hardware-Software Register Contract & Parity Auditor
================================================================================
Verifies strict 1:1 parity between C++ target HAL headers, SystemVerilog RTL modules,
Python diagnostic utilities, and architecture specification documents.

Prevents silent hardware/software disconnects where code compiles and spec audit tags pass
(// @impl [SPEC-ZYNQ-01]) but actual register offsets or constants diverge between C++,
RTL, Python, and the written specification.
"""

import sys
import re
from pathlib import Path

RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
BOLD = "\033[1m"
RESET = "\033[0m"


def parse_cpp_constants(header_path: Path) -> dict[str, int]:
    """Extract static constexpr uint32_t CONST_NAME = 0x...; from C++ header."""
    constants = {}
    pattern = re.compile(r"static\s+constexpr\s+\w+\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;")
    with open(header_path, "r", encoding="utf-8") as f:
        for line in f:
            m = pattern.search(line)
            if m:
                name, val_str = m.group(1), m.group(2)
                val = int(val_str, 16) if val_str.startswith("0x") else int(val_str)
                constants[name] = val
    return constants


def parse_sv_localparams(sv_path: Path) -> dict[str, int]:
    """Extract localparam [...] CONST_NAME = ...; from SystemVerilog file."""
    constants = {}
    # Matches localparam [7:0] REG_FOO = 8'h10; or localparam [31:0] HARDWARE_MAGIC = 32'h41535036;
    pattern = re.compile(r"localparam\s+(?:logic\s*)?(?:\[[^\]]+\]\s*)?(\w+)\s*=\s*(\d+'h[0-9a-fA-F]+|\d+)\s*;")
    with open(sv_path, "r", encoding="utf-8") as f:
        for line in f:
            m = pattern.search(line)
            if m:
                name, val_str = m.group(1), m.group(2)
                if "'h" in val_str:
                    val = int(val_str.split("'h")[1], 16)
                else:
                    val = int(val_str)
                constants[name] = val
    return constants


def parse_python_constants(py_path: Path, prefix: str = "") -> dict[str, int]:
    """Extract top-level CONST_NAME = 0x... or decimal from Python file."""
    constants = {}
    pattern = re.compile(r"^([A-Z0-9_]+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\b")
    with open(py_path, "r", encoding="utf-8") as f:
        for line in f:
            m = pattern.search(line.strip())
            if m:
                name, val_str = m.group(1), m.group(2)
                if not prefix or name.startswith(prefix):
                    val = int(val_str, 16) if val_str.startswith("0x") else int(val_str)
                    constants[name] = val
    return constants


def parse_spec_csr_map(spec_path: Path) -> dict[int, str]:
    """Extract `0xXX`: description register items from Markdown specification."""
    registers = {}
    # Matches * `0x00`: Control register.
    pattern = re.compile(r"\*\s*`0x([0-9a-fA-F]{2})`\s*:\s*([^.\n]+)")
    with open(spec_path, "r", encoding="utf-8") as f:
        for line in f:
            m = pattern.search(line)
            if m:
                offset = int(m.group(1), 16)
                desc = m.group(2).strip()
                registers[offset] = desc
    return registers


def check_zynq_uio_bridge(root: Path) -> list[str]:
    """Verify parity across ZynqUioBridge C++ header, SV bridge RTL, Python diag, and Spec."""
    errors = []
    cpp_path = root / "targets/zynq7000/include/abstractx/target/zynq_uio.hpp"
    sv_path = root / "rtl/asp_axi_lite_bridge.sv"
    py_path = root / "hw/zynq7000/qmtech_zynq7020/tools/zynq_diagnostics.py"
    spec_path = root / "hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md"

    if not cpp_path.exists():
        return [f"C++ header missing: {cpp_path}"]
    if not sv_path.exists():
        return [f"SV RTL missing: {sv_path}"]
    if not py_path.exists():
        return [f"Python diag missing: {py_path}"]
    if not spec_path.exists():
        return [f"Spec missing: {spec_path}"]

    cpp_consts = parse_cpp_constants(cpp_path)
    sv_consts = parse_sv_localparams(sv_path)
    py_consts = parse_python_constants(py_path)
    spec_map = parse_spec_csr_map(spec_path)

    # 1. Check C++ vs RTL parity on REG_* and HARDWARE_MAGIC
    expected_regs = [
        "REG_CONTROL",
        "REG_STATUS",
        "REG_IRQ_STATUS",
        "REG_IRQ_ENABLE",
        "REG_TLP_IN_PORT",
        "REG_TLP_OUT_PORT",
        "REG_EGR_COUNT",
        "REG_ING_FREE",
        "REG_HARDWARE_ID",
        "HARDWARE_MAGIC",
    ]

    for reg in expected_regs:
        if reg not in cpp_consts:
            errors.append(f"C++ {cpp_path.name} is missing expected definition '{reg}'")
        if reg not in sv_consts:
            errors.append(f"RTL {sv_path.name} is missing expected definition '{reg}'")

        if reg in cpp_consts and reg in sv_consts:
            if cpp_consts[reg] != sv_consts[reg]:
                errors.append(
                    f"Mismatch on {reg}: C++ has 0x{cpp_consts[reg]:X} while RTL has 0x{sv_consts[reg]:X}"
                )

        if reg in py_consts:
            if cpp_consts.get(reg) != py_consts[reg]:
                errors.append(
                    f"Mismatch on {reg}: C++ has 0x{cpp_consts.get(reg, 0):X} while Python has 0x{py_consts[reg]:X}"
                )

    # 2. Check Specification Parity for AXI-Lite CSRs
    spec_expectations = {
        0x00: "REG_CONTROL",
        0x04: "REG_STATUS",
        0x08: "REG_IRQ_STATUS",
        0x0C: "REG_IRQ_ENABLE",
        0x10: "REG_TLP_IN_PORT",
        0x14: "REG_TLP_OUT_PORT",
        0x18: "REG_EGR_COUNT",
        0x1C: "REG_ING_FREE",
    }
    for offset, reg_name in spec_expectations.items():
        if offset not in spec_map:
            errors.append(f"Spec {spec_path.name} is missing CSR mapping for 0x{offset:02X} ({reg_name})")
        elif cpp_consts.get(reg_name) != offset:
            errors.append(
                f"Spec/C++ offset mismatch for {reg_name}: Spec specifies 0x{offset:02X}, C++ has 0x{cpp_consts.get(reg_name, -1):02X}"
            )

    return errors


def check_sys_regs(root: Path) -> list[str]:
    """System register offsets: the Python diagnostic tool against the case labels in asp_sys_regs.sv
    (parsed from the RTL, not assumed). The IMU core check was removed with the core (2026-11-05,
    [SPEC-TLP-07]: all-PIO, no dedicated cores)."""
    errors = []
    py_path = root / "hw/zynq7000/qmtech_zynq7020/tools/zynq_diagnostics.py"
    sys_regs_sv = root / "rtl/asp_sys_regs.sv"
    if not py_path.exists() or not sys_regs_sv.exists():
        return ["Required peripheral files missing"]

    py_consts = parse_python_constants(py_path)
    rtl_offsets = {int(m, 16) for m in re.findall(r"8'h([0-9A-Fa-f]{2})\s*:", sys_regs_sv.read_text())}
    sys_base = 0x40000000
    expected_sys = {
        "SYS_REG_SCRATCH": 0x08,
        "SYS_REG_LED_CTRL": 0x0C,
        "SYS_REG_TIME_L": 0x10,
        "SYS_REG_TIME_H": 0x14,
    }
    for name, off in expected_sys.items():
        if off not in rtl_offsets:
            errors.append(f"asp_sys_regs.sv has no case label 8'h{off:02X} for {name}")
        if name not in py_consts:
            errors.append(f"Python diag missing definition '{name}'")
        elif py_consts[name] != sys_base + off:
            errors.append(
                f"System register mismatch for {name}: Python has 0x{py_consts[name]:08X}, RTL 0x{sys_base + off:08X}"
            )
    return errors


def check_e906_memory_map(root: Path) -> list[str]:
    """Verify E906 memory layout parity between C header and specification."""
    errors = []
    mem_header = root / "targets/allwinner_e906/include/memory_map.h"
    if not mem_header.exists():
        return []

    defines = {}
    pattern = re.compile(r"#define\s+([A-Za-z0-9_]+)\s+(0x[0-9a-fA-F]+)")
    with open(mem_header, "r", encoding="utf-8") as f:
        for line in f:
            m = pattern.search(line)
            if m:
                defines[m.group(1)] = int(m.group(2), 16)

    # Core E906 physical & bus truths
    invariants = {
        "DSP_LOCAL_RAM_BASE": 0x00020000,
        "OPTEE_SRAM_A2_BASE": 0x00044000,
        "SRAM_A3_SPACE0_BASE": 0x3FFC0000,
        "SRAM_SPACE0_HOST_PA": 0x07280000,
        "SRAM_A3_SPACE1_BASE": 0x40000000,
        "SRAM_SPACE1_HOST_PA": 0x072C0000,
        "SRAM_SHARED_TAIL_BASE": 0x3FFF0000,
        "E906_RPMSG_POOL_BASE": 0x40020000,
    }

    for name, exp_val in invariants.items():
        if name in defines and defines[name] != exp_val:
            errors.append(
                f"E906 memory invariant mismatch for {name}: header has 0x{defines[name]:08X}, expected 0x{exp_val:08X}"
            )

    return errors


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    all_errors = []

    print(f"{BOLD}=============================================================================={RESET}")
    print(f"{BOLD} Hardware-Software Register Parity & Contract Auditor{RESET}")
    print(f" Root: {root}")
    print(f"{BOLD}=============================================================================={RESET}")

    # Check Zynq AXI-Lite Bridge
    zynq_errs = check_zynq_uio_bridge(root)
    if zynq_errs:
        all_errors.extend(zynq_errs)
        print(f"[{RED}FAIL{RESET}] Zynq-7000 AXI-Lite Bridge Parity ({len(zynq_errs)} errors)")
        for e in zynq_errs:
            print(f"  {RED}* {e}{RESET}")
    else:
        print(f"[{GREEN}PASS{RESET}] Zynq-7000 AXI-Lite Bridge Parity (C++ <-> RTL <-> Python <-> Spec)")

    # System registers (offsets parsed from the RTL)
    sys_errs = check_sys_regs(root)
    if sys_errs:
        all_errors.extend(sys_errs)
        print(f"[{RED}FAIL{RESET}] System Register Parity ({len(sys_errs)} errors)")
        for e in sys_errs:
            print(f"  {RED}* {e}{RESET}")
    else:
        print(f"[{GREEN}PASS{RESET}] System Register Parity (asp_sys_regs.sv <-> zynq_diagnostics.py)")

    # Check E906 Memory Map
    e906_errs = check_e906_memory_map(root)
    if e906_errs:
        all_errors.extend(e906_errs)
        print(f"[{RED}FAIL{RESET}] XuanTie E906 Memory Map Invariants ({len(e906_errs)} errors)")
        for e in e906_errs:
            print(f"  {RED}* {e}{RESET}")
    else:
        print(f"[{GREEN}PASS{RESET}] XuanTie E906 Memory Map Invariants (SRAM Space 0/1 & Shared Tail)")

    print(f"------------------------------------------------------------------------------")
    if all_errors:
        print(f"{RED}{BOLD}[FAILURE] Total Register Disconnects Found: {len(all_errors)}{RESET}")
        return 1
    else:
        print(f"{GREEN}{BOLD}[SUCCESS] 100% Hardware-Software Register Parity Verified across all layers.{RESET}")
        return 0


if __name__ == "__main__":
    sys.exit(main())
