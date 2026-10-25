#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-TLP-03] docs/DESIGN_SPECIFICATION.md#spec-tlp-03
"""
check_queue_concurrency.py - Cross-Language Queue Concurrency Invariant Checker
==============================================================================
Enforces [SPEC-TLP-03] and Architecture Invariants across C++, Python, and SystemVerilog:

1. Single-Producer Single-Consumer (SPSC):
   - Strictly restricted to 1:1 dedicated point-to-point hardware DMA streams
     (e.g. UartRxRingBase, SM FIFO, asp_axis_fifo) or isolated single-writer channels.
   - NEVER permitted for shared telemetry, I/O dispatch, or multi-task sinks.

2. Multi-Producer Single-Consumer (MPSC):
   - MANDATORY for shared telemetry rings (g_telemetry_ring), I/O request queues,
     and any sink pushed to by multiple coroutines, worker threads, or ISRs.
   - In C++: MpscRingBuffer<T, Cap> / MpscTlpRing<Cap> with InterruptLock.
   - In Python: queue.Queue / queue.SimpleQueue with thread safety or asyncio.Queue.
   - In SystemVerilog: Shared egress sinks MUST arbitrate multi-channel streams
     through asp_router.sv or round-robin arbiters before FIFO write strobes.

3. Multi-Producer Multi-Consumer (MPMC):
   - STRICTLY PROHIBITED across all embedded and FPGA layers. Multi-consumer
     contention causes priority inversion, CAS thrashing, and timing non-determinism.
   - If multiple consumers are required, partition into dedicated 1:1 channels
     or route via the 64-byte TLP crossbar switch fabric.
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
    "build_asan", "build_tsan", "__pycache__", ".system_generated"
}

# C++ Disallowed SPSC / MPMC Patterns
CPP_DISALLOWED_PATTERNS = [
    (re.compile(r"\bSpscTlpRing\b.*?\b(g_telemetry_ring|g_tx_ring|g_sensor_ring|egress_tx_ring|ingress_rx_ring|s_tlp_ingress_ring)\b"),
     "Shared multi-producer queue must use MpscTlpRing with InterruptLock, not SpscTlpRing"),
    (re.compile(r"\b(egress_tx_ring|ingress_rx_ring|s_tlp_ingress_ring|g_telemetry_ring)\b.*?\bSpscTlpRing\b"),
     "Shared multi-producer queue must use MpscTlpRing with InterruptLock, not SpscTlpRing"),
    (re.compile(r"\bSpscRingBuffer<[^>]+>\s+(tx_queue_|rx_queue_|g_telemetry_ring)\b"),
     "Multi-producer queue must use MpscRingBuffer with InterruptLock"),
    (re.compile(r"\b(MpmcRingBuffer|MpmcTlpRing|mpmc_queue)\b"),
     "MPMC queues are strictly prohibited in AbstractX; partition into SPSC channels or MPSC sinks"),
]

# Python Concurrency Checks
# Flag raw list / deque append across multi-threaded scripts without lock
PY_THREADING_RE = re.compile(r"\bthreading\.Thread\b")
PY_UNSAFE_QUEUE_PATTERNS = [
    (re.compile(r"\b(shared_queue|tx_queue|rx_queue|packet_list)\s*=\s*(?:collections\.)?deque\(\)"),
     "Multi-threaded Python queue should use 'queue.Queue' or protect deque with 'threading.Lock'"),
]

# SystemVerilog Concurrency Checks
# Flag multi-driven FIFO write strobes or un-arbitrated multi-writer OR logic
SV_UNARBITRATED_FIFO_PATTERNS = [
    (re.compile(r"\bassign\s+(\w*fifo_wr_en\w*|\w*wr_en\w*)\s*=.*?[|].*?;"),
     "Un-arbitrated multi-producer FIFO write strobe detected. Use an explicit arbiter (asp_rr_arbiter / asp_router)"),
]


def check_cpp_file(path: Path) -> list[str]:
    violations = []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return violations

    for idx, line in enumerate(text.splitlines(), 1):
        sline = line.strip()
        if sline.startswith("//") or sline.startswith("*") or sline.startswith("/*"):
            continue
        for pat, msg in CPP_DISALLOWED_PATTERNS:
            if pat.search(line):
                violations.append(f"{path}:{idx}: [C++ QUEUE-CONCURRENCY-DEFECT] {msg}\n    Line: {sline}")
    return violations


def check_python_file(path: Path) -> list[str]:
    violations = []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return violations

    lines = text.splitlines()
    has_threads = bool(PY_THREADING_RE.search(text))
    if not has_threads:
        return violations

    for idx, line in enumerate(lines, 1):
        sline = line.strip()
        if sline.startswith("#"):
            continue
        for pat, msg in PY_UNSAFE_QUEUE_PATTERNS:
            if pat.search(line):
                violations.append(f"{path}:{idx}: [PYTHON CONCURRENCY-DEFECT] {msg}\n    Line: {sline}")
    return violations


def check_sv_file(path: Path) -> list[str]:
    violations = []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return violations

    lines = text.splitlines()
    for idx, line in enumerate(lines, 1):
        sline = line.strip()
        if sline.startswith("//") or sline.startswith("/*") or sline.startswith("*"):
            continue
        for pat, msg in SV_UNARBITRATED_FIFO_PATTERNS:
            if pat.search(line):
                violations.append(f"{path}:{idx}: [RTL CONCURRENCY-DEFECT] {msg}\n    Line: {sline}")
    return violations


def print_usage_guide():
    guide = f"""
{BOLD}AbstractX Queue Concurrency Architecture Guide (SPSC vs MPSC vs MPMC){RESET}
======================================================================
{BOLD}1. Single-Producer Single-Consumer (SPSC){RESET}
   * {GREEN}Valid Usage{RESET}: Dedicated 1:1 hardware DMA streams (e.g. UartRxRingBase,
     SPI DMA burst ring, asp_axis_fifo, PIO state machine FIFO) where EXACTLY ONE
     producer writes and EXACTLY ONE consumer reads.
   * {GREEN}Advantage{RESET}: 100% lock-free, zero atomic RMW loops, zero CAS overhead.
   * {RED}Violation{RESET}: Allowing two coroutines, two threads, or an ISR + main loop
     to push into an SPSC ring concurrently.

{BOLD}2. Multi-Producer Single-Consumer (MPSC){RESET}
   * {GREEN}Valid Usage{RESET}: Shared sinks where multiple tasks/cores emit datagrams:
     - `g_telemetry_ring`: telemetry emitted by IMU fusion, GPS, barometer, and motor tasks.
     - `g_tx_ring`: commands dispatched from multiple flight loop coroutines.
     - Egress TLP router inputs arbitrated across multiple peripheral endpoints.
   * {GREEN}Implementation{RESET}:
     - C++: `MpscRingBuffer<T, Cap>` or `MpscTlpRing<Cap>` with `InterruptLock`.
     - Python: `queue.Queue` or `asyncio.Queue`.
     - SystemVerilog: `asp_router.sv` full-crossbar with round-robin arbitration.

{BOLD}3. Multi-Producer Multi-Consumer (MPMC){RESET}
   * {RED}STRICTLY PROHIBITED{RESET} in AbstractX. MPMC rings cause cacheline bouncing,
     priority inversion, and ABA races under hard-realtime bare-metal constraints.
   * If multiple consumers are required, partition into dedicated 1:1 SPSC channels
     or route via the 64-byte TLP crossbar switch fabric.

{BOLD}4. Linux Asynchronous I/O Architecture{RESET}
   * AbstractX native Linux extension (`targets/linux/`) provides a zero-heap
     stackless C++20 coroutine runtime (`asp_coro.hpp`) + lightweight `EpollReactor`
     over UIO, character devices, and eventfd.
   * Compared to Boost.Asio:
     - Boost.Asio requires dynamic memory allocations (`std::function`, promises)
       and heavy Boost dependencies.
     - AbstractX Linux extension maintains 100% API and zero-copy SPSC/MPSC ring
       parity with bare-metal microcontrollers (Pico 2 W, ESP32-P4, XuanTie E906).
"""
    print(guide)


def main() -> int:
    parser = argparse.ArgumentParser(description="AbstractX Cross-Language Queue Concurrency Invariant Checker")
    parser.add_argument("--files", nargs="*", help="Specific files to check")
    parser.add_argument("--lang", choices=["cpp", "py", "sv", "all"], default="all", help="Target language to check")
    parser.add_argument("--usage", action="store_true", help="Print architectural SPSC/MPSC/MPMC usage guide")
    args = parser.parse_args()

    if args.usage:
        print_usage_guide()
        return 0

    root_dir = Path(__file__).resolve().parent.parent
    violations = []
    checked_count = 0

    if args.files:
        files = [Path(f).resolve() for f in args.files]
        for f in files:
            if f.suffix in {".hpp", ".h", ".cpp"}:
                violations.extend(check_cpp_file(f))
                checked_count += 1
            elif f.suffix == ".py":
                violations.extend(check_python_file(f))
                checked_count += 1
            elif f.suffix in {".sv", ".v"}:
                violations.extend(check_sv_file(f))
                checked_count += 1
    else:
        # Scan C++
        if args.lang in {"cpp", "all"}:
            for search_dir in ["include", "apps", "targets", "drivers"]:
                base = root_dir / search_dir
                if not base.exists():
                    continue
                for p in base.rglob("*"):
                    if p.is_file() and not any(ex in p.parts for ex in EXCLUDE_DIRS):
                        if p.suffix in {".hpp", ".h", ".cpp"}:
                            violations.extend(check_cpp_file(p))
                            checked_count += 1

        # Scan Python
        if args.lang in {"py", "all"}:
            for search_dir in ["tools", "hw", "tests"]:
                base = root_dir / search_dir
                if not base.exists():
                    continue
                for p in base.rglob("*.py"):
                    if p.is_file() and not any(ex in p.parts for ex in EXCLUDE_DIRS):
                        violations.extend(check_python_file(p))
                        checked_count += 1

        # Scan SystemVerilog
        if args.lang in {"sv", "all"}:
            for search_dir in ["rtl", "hw"]:
                base = root_dir / search_dir
                if not base.exists():
                    continue
                for p in base.rglob("*.sv"):
                    if p.is_file() and not any(ex in p.parts for ex in EXCLUDE_DIRS):
                        violations.extend(check_sv_file(p))
                        checked_count += 1

    if violations:
        print(f"{RED}{BOLD}[QUEUE CONCURRENCY CHECK FAILED]{RESET} Found {len(violations)} defect(s):")
        for v in violations:
            print(f"  {RED}x{RESET} {v}")
        return 1

    print(f"{GREEN}{BOLD}[QUEUE CONCURRENCY CHECK PASSED]{RESET} Checked {checked_count} files (C++, Python, SystemVerilog); all queues adhere to [SPEC-TLP-03] concurrency contracts.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
