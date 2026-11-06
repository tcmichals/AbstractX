# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
sizeTrac (ELF sections vs. budget, FPGA utilisation) and perfTrac (FPGA Fmax)
parsers. Parsers take text so they are unit-tested without toolchains.
"""
# @impl [SPEC-AUDIT-09] docs/SPECTRACE.md

import re

ELF_SECTIONS_RE = re.compile(r"^\s*([.\w]+)\s+(\d+)\s+(\d+)\s*$")
UTIL_RE = re.compile(r"Info:\s+(\w+):\s+(\d+)/\s*(\d+)\s+(\d+)%")
FMAX_RE = re.compile(r"Max frequency for clock '([^']+)': ([\d.]+) MHz \((PASS|FAIL) at ([\d.]+) MHz\)")

# Sections that occupy memory at run time, by where they live.
TEXT_SECTIONS = (".vectors", ".text", ".rodata", ".fastcode", ".resource_table",
                 ".boot2", ".binary_info", ".ARM.exidx", ".init_array", ".fini_array")
DATA_SECTIONS = (".data", ".sdata", ".ram_vector_table", ".tdata")
BSS_SECTIONS = (".bss", ".sbss", ".stack", ".heap", ".trace_buffer", ".sram_c",
                ".scratch_x", ".scratch_y", ".stack1_dummy", ".stack_dummy", ".tbss")


def parse_size_A(text: str) -> dict:
    """`<prefix>size -A <elf>` output -> {section: bytes} (address column ignored)."""
    sections = {}
    for line in text.splitlines():
        m = ELF_SECTIONS_RE.match(line)
        if m and m.group(1) != "Total":
            sections[m.group(1)] = int(m.group(2))
    return sections


def parse_size_A_addr(text: str) -> dict:
    """`size -A` output -> {section: (bytes, address)}; address 0 = not loaded (debug info)."""
    out = {}
    for line in text.splitlines():
        m = ELF_SECTIONS_RE.match(line)
        if m and m.group(1) != "Total":
            out[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    return out


# @impl [SPEC-AUDIT-20] docs/SPECTRACE.md
def region_bytes(addr_sections: dict, region: dict) -> tuple[int, int]:
    """(bytes inside [start, end), loaded bytes outside it). For targets whose image spans more
    than one memory window (E906: SRAM_FAST in Space 0, cold code in Space 1), only the budget
    window counts against ram_budget_bytes."""
    start, end = int(str(region["start"]), 0), int(str(region["end"]), 0)
    inside = outside = 0
    for _name, (size, addr) in addr_sections.items():
        if addr == 0 or size == 0:
            continue
        if start <= addr < end:
            inside += size
        else:
            outside += size
    return inside, outside


def classify(sections: dict) -> dict:
    out = {"text": 0, "data": 0, "bss": 0}
    for name, size in sections.items():
        if name.startswith(TEXT_SECTIONS):
            out["text"] += size
        elif name.startswith(DATA_SECTIONS):
            out["data"] += size
        elif name.startswith(BSS_SECTIONS):
            out["bss"] += size
    return out


def size_metrics(sections: dict, model: str) -> dict:
    """model 'sram_only': everything is loaded into RAM (E906, host).
    model 'flash_ram': text+data in flash, data+bss in RAM (Pico)."""
    c = classify(sections)
    if model == "flash_ram":
        return {**c, "flash_bytes": c["text"] + c["data"], "ram_bytes": c["data"] + c["bss"]}
    return {**c, "flash_bytes": 0, "ram_bytes": c["text"] + c["data"] + c["bss"]}


def judge_size(metrics: dict, target: dict, prev_metrics: dict | None) -> tuple[str, str]:
    notes = []
    ok = True
    if metrics["ram_bytes"] > target.get("ram_budget_bytes", float("inf")):
        ok = False
        notes.append(f"RAM {metrics['ram_bytes']} > budget {target['ram_budget_bytes']}")
    if metrics["flash_bytes"] > target.get("flash_budget_bytes", float("inf")):
        ok = False
        notes.append(f"flash {metrics['flash_bytes']} > budget {target['flash_budget_bytes']}")
    grow = target.get("max_allowed_growth_bytes")
    if prev_metrics and grow is not None:
        for k in ("ram_bytes", "flash_bytes"):
            g = metrics[k] - prev_metrics.get(k, metrics[k])
            if g > grow:
                ok = False
                notes.append(f"{k} grew {g} B > allowed {grow} B")
    return ("pass" if ok else "fail"), "; ".join(notes)


def parse_nextpnr(log: str) -> dict:
    """Last utilisation table and last Fmax line per clock from a nextpnr log."""
    util = {}
    for m in UTIL_RE.finditer(log):
        util[m.group(1)] = {"used": int(m.group(2)), "total": int(m.group(3)), "pct": int(m.group(4))}
    # nextpnr prints Fmax after placement (estimate) and after routing (final);
    # the last line per clock is the routed value.
    fmax = {}
    for m in FMAX_RE.finditer(log):
        fmax[m.group(1)] = {"fmax_mhz": float(m.group(2)), "target_mhz": float(m.group(4)),
                            "pass": m.group(3) == "PASS"}
    return {"util": util, "fmax": fmax}
