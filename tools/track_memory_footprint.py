#!/usr/bin/env python3
"""
Copyright (C) 2026 Tim Michals
SPDX-License-Identifier: GPL-3.0-or-later

AbstractX SpecTrace Embedded Memory Observability & Static Budget Auditor
-------------------------------------------------------------------------
Automates continuous RAM, SRAM, and Flash memory footprint tracking across targets:
1. Parses targets defined in tools/memory_targets.json (or fallback tools/membrowse-targets.json).
2. Extracts exact ELF section allocations (.text, .rodata, .data, .bss, .stack, .trace_buffer).
3. Zero heap: no malloc/free/_sbrk/operator new in the symbol table, defined or not ([SPEC-AUDIT-19]).
4. Emits memory_metrics.json for live visualization in AbstractX Studio & GitHub Actions.

Usage:
  python3 tools/track_memory_footprint.py                  # configured targets; exit 1 on any failure
  python3 tools/track_memory_footprint.py --allow-missing  # unbuilt targets SKIPPED, not failed
  python3 tools/track_memory_footprint.py --elf build_e906/apps/*/e906_repl_app
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

HEAP_SYMBOLS = {
    "malloc", "free", "calloc", "realloc",
    "_malloc_r", "_free_r", "_calloc_r", "_realloc_r", "_sbrk", "_sbrk_r",
}
# Global operator new (any size_t width, array and aligned forms). operator delete is not listed:
# libstdc++'s delete calls free (caught above), and a target's own trapping delete cannot allocate
# ([SPEC-E906-MEM-03]).
HEAP_PREFIXES = ("_Znw", "_Zna")


def _nm_tool() -> str:
    """GNU nm reads the symbol table of RISC-V and ARM ELFs too; use the target's if it is on PATH."""
    for name in ("riscv-none-elf-nm", "arm-none-eabi-nm", "nm"):
        tool = shutil.which(name)
        if tool:
            return tool
    return "nm"


def heap_symbols_in(elf_path: Path) -> list:
    """Every forbidden heap symbol in the ELF, defined (T/t/W) or undefined (U)."""
    res = subprocess.run([_nm_tool(), str(elf_path)],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    found = set()
    for line in res.stdout.splitlines():
        parts = line.split()
        if not parts:
            continue
        sym = parts[-1]
        if sym in HEAP_SYMBOLS or sym.startswith(HEAP_PREFIXES):
            found.add(sym)
    return sorted(found)


# @impl [SPEC-AUDIT-19] docs/SPECTRACE.md
def audit_zero_heap(elf_path: Path):
    """(ok, message). A missing or unreadable ELF is not a pass ([SPEC-AUDIT-19])."""
    if not elf_path.exists():
        return False, "Binary not found"
    try:
        found = heap_symbols_in(elf_path)
    except (OSError, subprocess.CalledProcessError) as e:
        return False, f"nm failed: {e}"
    if found:
        return False, "Violation: heap symbols linked: " + ", ".join(found)
    return True, "No heap symbols in the symbol table"


def check_elfs(paths) -> int:
    """--elf mode: zero-heap check only, on the given ELFs. Returns the number of failures."""
    failures = 0
    for p in paths:
        ok, msg = audit_zero_heap(Path(p))
        print(f"{'PASS' if ok else 'FAIL'}  {p}: {msg}")
        failures += 0 if ok else 1
    return failures


def run_analysis(allow_missing: bool = False):
    failures = 0
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
            if allow_missing:
                print("  [SKIPPED] Binary does not exist on disk (--allow-missing).\n")
            else:
                print("  [NOT BUILT] Binary does not exist on disk: FAIL (use --allow-missing to skip).\n")
                failures += 1
            continue

        sections = inspect_elf_sections(elf_path)
        if not sections:
            print("  [ERROR] Failed to inspect ELF sections: FAIL.\n")
            failures += 1
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
        # Strict zero heap is the E906's rule (model platform, [SPEC-E906-MEM-03]); a target with
        # "heap_policy": "report" lists its heap symbols without failing ([SPEC-AUDIT-19]).
        heap_report_only = target.get("heap_policy") == "report"
        if not heap_ok and not heap_report_only:
            failures += 1
        ram_pct = (ram_bytes / ram_budget) * 100.0
        flash_pct = (flash_bytes / flash_budget) * 100.0

        print(f"  RAM Usage  : {ram_bytes:,} / {ram_budget:,} B ({ram_pct:5.1f}%)")
        print(f"  Flash Usage: {flash_bytes:,} / {flash_budget:,} B ({flash_pct:5.1f}%)")
        heap_word = "PASS" if heap_ok else ("REPORT" if heap_report_only else "FAIL")
        print(f"  Zero-Heap  : {heap_word} ({heap_msg})")
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
            "heap_policy": target.get("heap_policy", "strict"),
            "heap_status": heap_msg
        }

    # Save to metrics file for AbstractX Studio & GitHub Dashboard
    METRICS_OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with open(METRICS_OUTPUT, "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)

    print(f"Saved memory observability metrics to {METRICS_OUTPUT}")
    print(f"Result: {'FAIL' if failures else 'PASS'} ({failures} failing target(s))")
    return 1 if failures else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--allow-missing", action="store_true",
                    help="report a configured target whose ELF is not built as SKIPPED instead of failing")
    ap.add_argument("--elf", nargs="+", metavar="PATH",
                    help="zero-heap check only, on these ELFs (instead of the configured targets)")
    args = ap.parse_args(argv)
    if args.elf:
        return 1 if check_elfs(args.elf) else 0
    return run_analysis(allow_missing=args.allow_missing)


if __name__ == "__main__":
    sys.exit(main())
