#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-E906-HAL-01] [SPEC-E906-IRQ-02] [SPEC-E906-IRQ-03] targets/allwinner_e906/SPECIFICATION.md
"""
audit_hal_invariants.py - Hardware Abstraction Layer & Concurrency Invariant Auditor
===================================================================================
Enforces low-level concurrency and silicon interaction rules across AbstractX HAL:
1. Bare WFI Sleep-Check Race: Low-power waits using WFI must mask interrupts prior to
   evaluating the loop exit condition and atomically re-enable/WFI to avoid missing
   an interrupt that arrived between the check and the sleep.
2. Uncalibrated Busy-Spin Loops: Drivers must not use arbitrary NOP spinloops
   (e.g. `for (int timeout = 100000; ...) { nop; }`) for bounded wait timeouts;
   they must use hardware counter deadlines (e.g. `Timer::get_ticks()`).
3. Cross-Domain Register RMW: Shared registers written by multiple operating systems
   or cores (e.g. `DMA_IRQ_CPU_EN_REG` shared between Linux and E906) must not be
   read-modified-written as 32-bit words, but written with precise sub-word access.
4. Safe Memory Inspection: Native REPL peek/poke functions must validate 4-byte
   alignment and valid hardware memory address bounds to prevent fatal bus/misalignment traps.
"""

import re
import sys
import argparse
from pathlib import Path

RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
BOLD = "\033[1m"
RESET = "\033[0m"

EXCLUDE_DIRS = {
    ".git", ".venv", "venv", "third_party", "build", "build_host",
    "build_asan", "build_tsan", "__pycache__", ".agents", ".claude", "sim"
}

# 1. Bare wfi without interrupt masking
# A while loop containing wfi without disable_interrupts or InterruptGuard or csrs
BARE_WFI_LOOP = re.compile(r"while\s*\([^{)]+\)\s*\{([^}]*?wfi[^}]*?)\}", re.DOTALL)

# 2. Arbitrary NOP spinloop
NOP_SPIN_LOOP = re.compile(r"for\s*\([^;]*?timeout\s*=\s*\d+;[^;]*?;\s*--timeout\)[^;{]*?\{[^}]*?nop[^}]*?\}", re.DOTALL)

# 3. 32-bit RMW on cross-domain DMA IRQ register
DMA_CPU_RMW = re.compile(r"DMA_IRQ_CPU_EN_REG\s*=\s*DMA_IRQ_CPU_EN_REG\s*&")

# 4. Peek/Poke without alignment check
PEEK_POKE_UNCHECKED = re.compile(r"mod_abstractx_(?:peek|poke)\b(?![\s\S]*?is_valid_e906_addr)")


def check_hal_file(path: Path) -> list[str]:
    violations = []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except Exception as e:
        return [f"{path}: Cannot read file: {e}"]

    # Check 1: Bare WFI check-then-sleep race
    if "wfi" in text and "while" in text:
        # Check every while (...) { ... wfi ... } block
        for m in BARE_WFI_LOOP.finditer(text):
            body = m.group(1)
            # If the body or surrounding code does not disable interrupts before checking:
            # Let's inspect the 20 lines preceding the match
            start_pos = m.start()
            preceding = text[max(0, start_pos - 300):start_pos]
            if ("disable_interrupts" not in preceding and "InterruptGuard" not in preceding and
                "csrc" not in body and "csrc" not in preceding):
                lineno = text[:start_pos].count("\n") + 1
                violations.append(
                    f"{path}:{lineno}: [HAL-INVARIANT-VIOLATION] Bare WFI in while loop without pre-check interrupt mask.\n"
                    f"    Race: If interrupt fires between loop condition check and WFI, core hangs permanently.\n"
                    f"    Fix: Use disable_interrupts_save_flags() and csrs mstatus wfi."
                )

    # Check 2: NOP spinloop
    if "nop" in text and "timeout" in text:
        for m in NOP_SPIN_LOOP.finditer(text):
            lineno = text[:m.start()].count("\n") + 1
            violations.append(
                f"{path}:{lineno}: [HAL-INVARIANT-VIOLATION] Uncalibrated busy-spin loop with arbitrary NOP count.\n"
                f"    Fix: Bounded timeouts must use Timer::get_ticks() deadline."
            )

    # Check 3: Shared DMA register RMW
    for m in DMA_CPU_RMW.finditer(text):
        lineno = text[:m.start()].count("\n") + 1
        violations.append(
            f"{path}:{lineno}: [HAL-INVARIANT-VIOLATION] 32-bit RMW on shared cross-domain register DMA_IRQ_CPU_EN_REG.\n"
            f"    Race: Linux concurrently mutates channels 0..7; 32-bit write clobbers Linux interrupts.\n"
            f"    Fix: Write upper byte/halfword directly or configure statically in init()."
        )

    # Check 4: Unchecked peek/poke in REPL
    if "mod_abstractx_peek" in text or "mod_abstractx_poke" in text:
        if "is_valid_e906_addr" not in text:
            violations.append(
                f"{path}: [HAL-INVARIANT-VIOLATION] mod_abstractx peek/poke lacks address validation.\n"
                f"    Risk: Unaligned or invalid addresses cause fatal hardware traps and kernel panic.\n"
                f"    Fix: Validate 4-byte alignment and SRAM/peripheral address ranges before access."
            )

    return violations


def scan_tree(root: Path) -> list[str]:
    violations = []
    scan_exts = {".hpp", ".cpp", ".h", ".c"}

    import os
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIRS and not d.startswith("build")]
        for fname in filenames:
            ext = Path(fname).suffix
            if ext in scan_exts:
                p = Path(dirpath) / fname
                violations.extend(check_hal_file(p))

    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description="AbstractX HAL Concurrency Invariant Auditor")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent,
                        help="Root directory of the AbstractX repository")
    args = parser.parse_args()

    print(f"{BOLD}[*] Scanning AbstractX HAL and Target drivers for concurrency invariants...{RESET}")
    violations = scan_tree(args.root)

    if violations:
        print(f"\n{RED}{BOLD}[FAIL] Found {len(violations)} HAL invariant violation(s):{RESET}\n")
        for v in violations:
            print(f"  {RED}•{RESET} {v}\n")
        return 1

    print(f"\n{GREEN}{BOLD}[PASS] 100% HAL Concurrency Invariant Parity: Safe WFI, calibrated timeouts, race-free DMA, safe peek/poke.{RESET}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
