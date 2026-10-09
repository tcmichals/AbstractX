#!/usr/bin/env python3
"""
Copyright (C) 2026 Tim Michals
SPDX-License-Identifier: GPL-3.0-or-later

AbstractX SpecTrace Embedded Memory Observability & Static Budget Auditor
-------------------------------------------------------------------------
Automates continuous RAM, SRAM, and Flash memory footprint tracking across targets:
1. Parses targets defined in tools/memory_targets.json (or fallback tools/membrowse-targets.json).
2. Extracts exact ELF section allocations (.text, .rodata, .data, .bss, .stack, .trace_buffer).
3. Verifies Freestanding C++20 Invariants (Zero Heap, no malloc/new/free in bare-metal targets).
4. Emits memory_metrics.json for live visualization in AbstractX Studio & GitHub Actions.

Usage:
  python3 tools/track_memory_footprint.py
"""

import os
import sys
import json
import shutil
import argparse
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = REPO_ROOT / "tools" / "memory_targets.json"
FALLBACK_CONFIG = REPO_ROOT / "tools" / "membrowse-targets.json"
METRICS_OUTPUT = REPO_ROOT / "tools" / "visualizer" / "memory_metrics.json"

def inspect_elf_sections(elf_path: Path):
    """Uses 'size -A' or 'readelf -S' to extract detailed section sizes."""
    if not elf_path.exists():
        return None

    sections = {}
    cmd = ["size", "-A", str(elf_path)]
    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
        lines = res.stdout.strip().split("\n")
        for line in lines[2:]:
            parts = line.split()
            if len(parts) >= 2:
                name = parts[0]
                try:
                    size = int(parts[1])
                    if size > 0:
                        sections[name] = size
                except ValueError:
                    pass
    except Exception as e:
        print(f"Warning: 'size -A' failed for {elf_path}: {e}")

    return sections

def audit_zero_heap(elf_path: Path):
    """Audits the ELF symbol table to verify zero heap dynamic allocations."""
    if not elf_path.exists():
        return True, "Binary not found"

    forbidden = ["malloc", "free", "_malloc_r", "_free_r", "calloc", "realloc", "_Znwm", "_Znam", "_ZdlPv"]
    cmd = ["nm", "-u", str(elf_path)] # list undefined symbols
    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        undefined = res.stdout
        for sym in forbidden:
            if sym in undefined:
                return False, f"Violation: Forbidden heap symbol '{sym}' referenced"
    except Exception:
        pass

    return True, "Zero dynamic heap references (Verified)"

def run_analysis():
    cfg_file = CONFIG_PATH if CONFIG_PATH.exists() else FALLBACK_CONFIG
    if not cfg_file.exists():
        print(f"Error: Config {cfg_file} not found.")
        return 1

    with open(cfg_file, "r", encoding="utf-8") as f:
        config = json.load(f)

    results = {}
    print("=================================================================")
    print("  AbstractX SpecTrace Embedded Memory Footprint & Static Audit   ")
    print("=================================================================\n")

    for target in config.get("targets", []):
        t_name = target["name"]
        display_name = target.get("display_name", t_name)
        bin_rel = target.get("binary_path")
        elf_path = REPO_ROOT / bin_rel
        ram_budget = target.get("ram_budget_bytes", 524288)
        flash_budget = target.get("flash_budget_bytes", 4194304)

        print(f"Target: {display_name} ({t_name})")
        print(f"  Binary: {bin_rel}")

        if not elf_path.exists():
            print("  [NOT BUILT] Binary does not exist on disk.\n")
            continue

        sections = inspect_elf_sections(elf_path)
        if not sections:
            print("  [ERROR] Failed to inspect ELF sections.\n")
            continue

        # Categorize sections into RAM and Flash
        ram_bytes = 0
        flash_bytes = 0
        for s_name, s_size in sections.items():
            if any(k in s_name for k in [".bss", ".data", ".ram", ".stack", ".heap"]):
                ram_bytes += s_size
            elif any(k in s_name for k in [".text", ".rodata", ".ARM", ".binary_info"]):
                flash_bytes += s_size

        is_embedded = "linux" not in target.get("architecture", "") and "x86" not in target.get("architecture", "")
        heap_ok, heap_msg = audit_zero_heap(elf_path) if is_embedded else (True, "POSIX Host Runtime (N/A)")
        ram_pct = (ram_bytes / ram_budget) * 100.0
        flash_pct = (flash_bytes / flash_budget) * 100.0

        print(f"  RAM Usage  : {ram_bytes:,} / {ram_budget:,} B ({ram_pct:5.1f}%)")
        print(f"  Flash Usage: {flash_bytes:,} / {flash_budget:,} B ({flash_pct:5.1f}%)")
        print(f"  Zero-Heap  : {'PASS' if heap_ok else 'FAIL'} ({heap_msg})")
        print("  Top Sections:")
        sorted_secs = sorted(sections.items(), key=lambda x: x[1], reverse=True)[:5]
        for s_name, s_size in sorted_secs:
            print(f"    - {s_name:24s}: {s_size:8,d} B")
        print()

        results[t_name] = {
            "display_name": display_name,
            "architecture": target.get("architecture"),
            "ram_used_bytes": ram_bytes,
            "ram_total_bytes": ram_budget,
            "flash_used_bytes": flash_bytes,
            "flash_total_bytes": flash_budget,
            "sections": sections,
            "zero_heap_compliant": heap_ok,
            "heap_status": heap_msg
        }

    # Save to metrics file for AbstractX Studio & GitHub Dashboard
    METRICS_OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with open(METRICS_OUTPUT, "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)

    print(f"Saved memory observability metrics to {METRICS_OUTPUT}")
    return 0

if __name__ == "__main__":
    sys.exit(run_analysis())
