#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-ZYNQ-01] [SPEC-ZYNQ-02] [SPEC-ZYNQ-03] [SPEC-ZYNQ-06] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md
# @impl [SPEC-TLP-01] docs/DESIGN_SPECIFICATION.md (TLP layout: RTL package <-> C header <-> Python formats)
# @impl [SPEC-HAL-15] docs/DESIGN_SPECIFICATION.md (rule facts against the headers, no register addresses in apps/)
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


C_SIZES = {"uint8_t": 1, "uint16_t": 2, "uint32_t": 4, "uint64_t": 8}
TLP_FIELD_FN = {"type": "tlp_type", "flags": "tlp_flags", "tag": "tlp_tag", "channel": "tlp_channel",
                "target_address": "tlp_addr", "length_dw": "tlp_len", "sequence": "tlp_seq",
                "timestamp_ns": "tlp_ts"}


def parse_c_tlp_struct(header: Path) -> dict[str, tuple[int, int]]:
    """asp_tlp64_t members -> (byte offset, size); packed struct, a union counts as its first member."""
    text = header.read_text()
    end = text.find("} asp_tlp64_t;")
    start = text.rfind("typedef struct", 0, end)
    if end < 0 or start < 0 or "packed" not in text[start:text.find("{", start)]:
        return {}
    fields, off, in_union, union_size = {}, 0, False, 0
    for line in text[text.find("{", start) + 1:end].splitlines():
        line = line.split("/*")[0].strip()
        if line.startswith("union"):
            in_union, union_size = True, 0
            continue
        if line.startswith("}"):
            in_union, off = False, off + union_size
            continue
        m = re.match(r"(uint\d+_t)\s+(\w+)(?:\[(\d+)\])?;", line)
        if not m:
            continue
        size = C_SIZES[m.group(1)] * int(m.group(3) or 1)
        if in_union:
            fields.setdefault(m.group(2), (off, size))
            union_size = max(union_size, size)
        else:
            fields[m.group(2)] = (off, size)
            off += size
    return fields


def parse_sv_tlp_pkg(pkg: Path) -> dict[str, tuple[int, int]]:
    """asp_tlp_pkg.sv accessor slices -> (byte offset, size), plus 'payload' from tlp_word."""
    text = pkg.read_text()
    out = {}
    for fn in TLP_FIELD_FN.values():
        m = re.search(rf"function automatic logic \[\d+:0\] {fn}\(.*?{fn} = t\[(\d+):(\d+)\];", text)
        if m:
            hi, lo = int(m.group(1)), int(m.group(2))
            out[fn] = (lo // 8, (hi - lo + 1) // 8) if lo % 8 == 0 and (hi - lo + 1) % 8 == 0 else (-1, -1)
    m = re.search(r"tlp_word = t\[(\d+) \+ 32 \* k \+: 32\]", text)
    if m:
        out["payload"] = (int(m.group(1)) // 8, 4)
    return out


def check_tlp_layout(root: Path) -> list[str]:
    """[SPEC-TLP-01] one little-endian layout: RTL package slices = C struct offsets; every Python
    struct format of the TLP header is little-endian (would have caught EL-1027-16)."""
    errors = []
    c = parse_c_tlp_struct(root / "include/asp_tlp64.h")
    sv = parse_sv_tlp_pkg(root / "rtl/asp_tlp_pkg.sv")
    if not c:
        return ["asp_tlp64_t not found in include/asp_tlp64.h"]
    if "crc32" not in c or c["crc32"] != (60, 4) or c.get("payload") != (20, 40):
        errors.append(f"asp_tlp64_t payload / crc32 at {c.get('payload')} / {c.get('crc32')}, expected (20, 40) / (60, 4)")
    for field, fn in TLP_FIELD_FN.items():
        if fn not in sv:
            errors.append(f"asp_tlp_pkg.sv: no little-endian slice for {fn} (expected t[hi:lo])")
        elif sv[fn] != c.get(field):
            errors.append(f"{field}: asp_tlp_pkg.sv {fn} at byte {sv[fn][0]} size {sv[fn][1]}, "
                          f"asp_tlp64_t at byte {c.get(field, ('?',))[0]} size {c.get(field, (0, '?'))[1]}")
    if sv.get("payload", (None,))[0] != 20:
        errors.append(f"asp_tlp_pkg.sv tlp_word: payload word 0 at byte {sv.get('payload', ('?',))[0]}, expected 20")
    fmt = re.compile(r"""["']([<>!=@]?)BBBBIHHQ""")
    for py in sorted(list(root.glob("tools/**/*.py")) + list(root.glob("hw/**/*.py")) + list(root.glob("sim/cocotb/*.py"))
                     + list(root.glob("apps/**/*.py")) + list(root.glob("applib/**/*.py")) + list(root.glob("tests/*.py"))):
        if "/third_party/" in str(py) or "/build" in str(py):
            continue
        for n, line in enumerate(py.read_text(errors="replace").splitlines(), 1):
            for m in fmt.finditer(line):
                if m.group(1) != "<":
                    errors.append(f"{py.relative_to(root)}:{n}: TLP header format '{m.group(1)}BBBBIHHQ' is not little-endian")
    return errors


def _hex_defines(path: Path) -> dict[str, int]:
    out = {}
    for m in re.finditer(r"#define\s+(\w+)\s+(0x[0-9A-Fa-f]+)", path.read_text()):
        out[m.group(1)] = int(m.group(2), 16)
    return out


def _constexpr_ints(path: Path) -> dict[str, int]:
    out = {}
    for m in re.finditer(r"constexpr\s+\w+\s+(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)U?\s*;", path.read_text()):
        out[m.group(1)] = int(m.group(2), 0)
    return out


# Atlas table row label -> (memory_map.h define, clic.hpp GIC constant, dma.hpp DRQ constant)
ATLAS_ROWS = {
    "UART3": ("UART3_BASE", "kGicUart3", "DRQ_UART3"),
    "TWI1": ("TWI1_BASE", "kGicTwi1", None),
    "SPI1": ("SPI1_BASE", None, None),
    "MSGBOX": ("MSGBOX_BASE", None, None),
}


def check_rule_facts(root: Path) -> list[str]:
    """[SPEC-HAL-15] copies of the facts: the silicon atlas rule against the E906 headers (EL-1027-27)."""
    errors = []
    atlas = root / ".agents/rules/silicon-api-atlas.md"
    inc = root / "targets/allwinner_e906/include"
    if not atlas.exists():
        return [f"{atlas} not found"]
    defs = _hex_defines(inc / "memory_map.h")
    clic = _constexpr_ints(inc / "hal/clic.hpp")
    dma = _constexpr_ints(inc / "hal/dma.hpp")
    text = atlas.read_text()
    for label, (base, gic, drq) in ATLAS_ROWS.items():
        row = re.search(rf"^\|\s*\*\*{label}\b[^|]*\|([^|]*)\|", text, re.M)
        if not row:
            errors.append(f"silicon atlas: no row for {label}")
            continue
        cell = row.group(1)
        addr = re.search(r"`(0x[0-9A-Fa-f]+)`", cell)
        if not addr or int(addr.group(1), 16) != defs.get(base):
            errors.append(f"silicon atlas {label}: base {addr.group(1) if addr else '?'}, memory_map.h {base} = "
                          f"{hex(defs[base]) if base in defs else 'missing'}")
        if gic:
            m = re.search(r"GIC (\d+)", cell)
            if not m or int(m.group(1)) != clic.get(gic):
                errors.append(f"silicon atlas {label}: GIC {m.group(1) if m else '?'}, clic.hpp {gic} = {clic.get(gic)}")
        if drq:
            m = re.search(r"DRQ (\d+)", cell)
            if not m or int(m.group(1)) != dma.get(drq):
                errors.append(f"silicon atlas {label}: DRQ {m.group(1) if m else '?'}, dma.hpp {drq} = {dma.get(drq)}")
    m = re.search(r"CLIC at \*\*`(0x[0-9A-Fa-f]+)`\*\*", text)
    if not m or int(m.group(1), 16) != clic.get("kBase"):
        errors.append(f"silicon atlas: CLIC base {m.group(1) if m else '?'}, clic.hpp kBase = {clic.get('kBase')}")
    return errors


APP_REG_LITERAL = re.compile(r"\b0x(?:0[2-7][0-9A-Fa-f]{6}|[Ee]0[0-9A-Fa-f]{6})\b")


def check_app_register_literals(root: Path) -> list[str]:
    """[SPEC-HAL-15]: no SoC register address in C/C++ under apps/ (lines marked regs-ok: are allowed)."""
    errors = []
    for f in sorted(root.glob("apps/**/*")):
        if f.suffix not in {".c", ".cpp", ".h", ".hpp"} or "third_party" in f.parts or "micropython" in f.parts:
            continue
        for n, line in enumerate(f.read_text(errors="replace").splitlines(), 1):
            if APP_REG_LITERAL.search(line) and "regs-ok:" not in line:
                errors.append(f"{f.relative_to(root)}:{n}: register address in an app: {line.strip()[:90]}")
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

    # TLP layout ([SPEC-TLP-01], EL-1027-21)
    tlp_errs = check_tlp_layout(root)
    if tlp_errs:
        all_errors.extend(tlp_errs)
        print(f"[{RED}FAIL{RESET}] TLP Layout Parity ({len(tlp_errs)} errors)")
        for e in tlp_errs:
            print(f"  {RED}* {e}{RESET}")
    else:
        print(f"[{GREEN}PASS{RESET}] TLP Layout Parity (asp_tlp_pkg.sv <-> asp_tlp64_t <-> Python '<BBBBIHHQ')")

    # Agent rule facts and app register literals ([SPEC-HAL-15], EL-1027-27)
    rule_errs = check_rule_facts(root) + check_app_register_literals(root)
    if rule_errs:
        all_errors.extend(rule_errs)
        print(f"[{RED}FAIL{RESET}] Rule Facts / App Register Literals ({len(rule_errs)} errors)")
        for e in rule_errs:
            print(f"  {RED}* {e}{RESET}")
    else:
        print(f"[{GREEN}PASS{RESET}] Rule Facts (silicon atlas <-> E906 headers), no register addresses in apps/")

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
