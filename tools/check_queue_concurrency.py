#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-TLP-03] docs/DESIGN_SPECIFICATION.md#spec-tlp-03
"""
check_queue_concurrency.py - Verify Queue Concurrency Contracts (SPSC vs MPSC)
==============================================================================
Enforces [SPEC-TLP-03]:
1. Shared telemetry rings, I/O processor request/completion rings, and any queue
   accessed across multiple coroutines, worker threads, or ISRs MUST use
   MpscRingBuffer<T, Cap> / MpscTlpRing<Cap> with InterruptLock.
2. Pure SpscRingBuffer<T, Cap> / SpscTlpRing<Cap> is restricted strictly to
   dedicated 1:1 hardware DMA streams (e.g. UartRxRingBase) or single-writer
   point-to-point channels.
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

EXCLUDE_DIRS = {".git", ".venv", "venv", "third_party", "build", "build_host", "build_asan", "build_tsan", "__pycache__"}

# Patterns that indicate multi-producer shared queue usage
DISALLOWED_SPSC_PATTERNS = [
    (re.compile(r"\bSpscTlpRing\b.*?\b(g_telemetry_ring|g_tx_ring|g_sensor_ring|egress_tx_ring|ingress_rx_ring|s_tlp_ingress_ring)\b"),
     "Shared queue must use MpscTlpRing (multi-producer concurrency requires InterruptLock)"),
    (re.compile(r"\b(egress_tx_ring|ingress_rx_ring|s_tlp_ingress_ring|g_telemetry_ring)\b.*?\bSpscTlpRing\b"),
     "Shared queue must use MpscTlpRing (multi-producer concurrency requires InterruptLock)"),
    (re.compile(r"\bSpscRingBuffer<[^>]+>\s+(tx_queue_|rx_queue_|g_telemetry_ring)\b"),
     "Multi-producer queue must use MpscRingBuffer with InterruptLock"),
]


def check_file(path: Path) -> list[str]:
    violations = []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return violations

    lines = text.splitlines()
    for idx, line in enumerate(lines, 1):
        sline = line.strip()
        if sline.startswith("//") or sline.startswith("*") or sline.startswith("/*"):
            continue
        for pat, msg in DISALLOWED_SPSC_PATTERNS:
            if pat.search(line):
                violations.append(f"{path}:{idx}: [QUEUE-CONCURRENCY-DEFECT] {msg}\n    Line: {sline}")
    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description="AbstractX Queue Concurrency Invariant Checker")
    parser.add_argument("--files", nargs="*", help="Specific files to check")
    args = parser.parse_args()

    root_dir = Path(__file__).resolve().parent.parent
    files_to_check = []

    if args.files:
        files_to_check = [Path(f).resolve() for f in args.files]
    else:
        for search_dir in ["include", "apps", "targets", "drivers"]:
            base = root_dir / search_dir
            if not base.exists():
                continue
            for p in base.rglob("*"):
                if p.is_file() and not any(ex in p.parts for ex in EXCLUDE_DIRS):
                    if p.suffix in {".hpp", ".h", ".cpp"}:
                        files_to_check.append(p)

    violations = []
    for path in sorted(files_to_check):
        violations.extend(check_file(path))

    if violations:
        print(f"{RED}{BOLD}[QUEUE CONCURRENCY CHECK FAILED]{RESET} Found {len(violations)} defect(s):")
        for v in violations:
            print(f"  {RED}x{RESET} {v}")
        return 1

    print(f"{GREEN}{BOLD}[QUEUE CONCURRENCY CHECK PASSED]{RESET} Checked {len(files_to_check)} files; all shared queues use MpscTlpRing / MpscRingBuffer.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
