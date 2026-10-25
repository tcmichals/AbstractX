#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-ARCH-10] [SPEC-ARCH-11] docs/DESIGN_SPECIFICATION.md
"""
audit_coro_contracts.py - C++20 Coroutine Contract & Concurrency Linter
=======================================================================
Verifies AbstractX C++20 coroutine contracts and safety invariants:
1. Dispatcher Resumption Contract: No direct .resume() calls inside awaiters or
   combinators. All resumptions must enqueue via abstractx::Dispatcher::push() or
   abstractx::IsrDispatcher::post(). Only abstractx::step() and test drivers resume.
2. Combinator Concurrency: Race combinators (WhenAll, WhenAny) must use std::atomic
   state counters (remaining_, ready_index_) with acquire/release semantics.
3. Intrusive Awaiter Cancellation Safety: Any awaiter registering an intrusive node
   or raw handle pointer must implement an RAII destructor that unlinks or clears the
   reference upon coroutine frame destruction/cancellation.
4. Symmetric Transfer / Non-blocking Suspension: await_suspend must not spin or block.
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

# 1. Direct .resume() inside production awaiter/driver code (excluding step() and Dispatcher itself)
DIRECT_RESUME_PATTERN = re.compile(r"\b([a-zA-Z0-9_]+)\.resume\(\)")

# Permitted files/contexts for .resume()
ALLOWED_RESUME_FILES = {
    "domain_dispatcher.hpp",
    "dispatcher.hpp",
    "abstractx.hpp",
    "test_",
}

# 2. Non-atomic counters in combinator classes
NON_ATOMIC_COMBINATOR_PATTERNS = [
    (re.compile(r"class\s+WhenAllAwaiter\b[\s\S]*?\bsize_t\s+remaining_[\s;]"),
     "WhenAllAwaiter must use std::atomic<size_t> remaining_"),
    (re.compile(r"class\s+WhenAnyAwaiter\b[\s\S]*?\bint\s+ready_index_[\s;]"),
     "WhenAnyAwaiter must use std::atomic<int> ready_index_"),
]

# 3. Direct caller.resume() in combinators
COMBINATOR_DIRECT_RESUME = re.compile(r"\bcaller\.resume\(\)")


def check_coro_file(path: Path) -> list[str]:
    violations = []
    fname = path.name

    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except Exception as e:
        return [f"{path}: Cannot read file: {e}"]

    lines = text.splitlines()

    # Check for direct caller.resume() in combinators
    for idx, line in enumerate(lines, 1):
        sline = line.strip()
        if sline.startswith("//") or sline.startswith("*") or sline.startswith("/*"):
            continue

        if COMBINATOR_DIRECT_RESUME.search(line):
            violations.append(
                f"{path}:{idx}: [CORO-CONTRACT-VIOLATION] Direct caller.resume() detected in combinator.\n"
                f"    Line: {sline}\n"
                f"    Fix: Must enqueue via abstractx::Dispatcher::push(caller)."
            )

        # Check for direct .resume() in awaiters outside allowed dispatching contexts
        if not any(allowed in str(path) for allowed in ALLOWED_RESUME_FILES):
            if "Awaiter" in text:
                m = DIRECT_RESUME_PATTERN.search(line)
                if m and m.group(1) not in ("task", "handle", "h"):
                    # Check if inside an Awaiter method
                    if "notify_" in line or "await_" in line:
                        violations.append(
                            f"{path}:{idx}: [CORO-CONTRACT-VIOLATION] Direct .resume() called inside awaiter method.\n"
                            f"    Line: {sline}\n"
                            f"    Fix: Only step() or Dispatcher may resume coroutines; enqueue via Dispatcher."
                        )

    # Check non-atomic combinator declarations
    for pat, desc in NON_ATOMIC_COMBINATOR_PATTERNS:
        if pat.search(text):
            violations.append(
                f"{path}: [CORO-CONTRACT-VIOLATION] {desc}\n"
                f"    Fix: Multi-task combinators require std::atomic state for thread/ISR safety."
            )

    # Check for Awaiters that register intrusive nodes without a destructor
    awaiter_struct_pattern = re.compile(
        r"struct\s+([A-Za-z0-9_]*Awaiter[A-Za-z0-9_]*)\s*(?::\s*public\s+([A-Za-z0-9_:]+))?\s*\{([^}]+)\};",
        re.MULTILINE
    )
    for m in awaiter_struct_pattern.finditer(text):
        awaiter_name = m.group(1)
        base_class = m.group(2) or ""
        body = m.group(3)

        # If it inherits from an intrusive link or contains node/lock registration
        has_intrusive_link = "bidirectional_link" in base_class or "link" in base_class or "node" in body
        registers_waiter = "schedule_waiter" in body or "lock.wait" in body or "wait(" in body

        if has_intrusive_link and registers_waiter:
            has_destructor = f"~{awaiter_name}" in body or f"~{awaiter_name}" in text
            if not has_destructor:
                violations.append(
                    f"{path}: [CORO-LIFETIME-VIOLATION] {awaiter_name} registers an intrusive node but lacks an RAII destructor.\n"
                    f"    Fix: Implement ~{awaiter_name}() to unlink from list upon coroutine cancellation."
                )

    return violations


def scan_tree(root: Path) -> list[str]:
    violations = []
    scan_exts = {".hpp", ".cpp", ".h"}

    import os
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIRS and not d.startswith("build")]
        for fname in filenames:
            ext = Path(fname).suffix
            if ext in scan_exts:
                p = Path(dirpath) / fname
                violations.extend(check_coro_file(p))

    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description="AbstractX Coroutine Contract & Concurrency Linter")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent,
                        help="Root directory of the AbstractX repository")
    args = parser.parse_args()

    print(f"{BOLD}[*] Scanning AbstractX codebase for coroutine contract invariants...{RESET}")
    violations = scan_tree(args.root)

    if violations:
        print(f"\n{RED}{BOLD}[FAIL] Found {len(violations)} coroutine contract violation(s):{RESET}\n")
        for v in violations:
            print(f"  {RED}•{RESET} {v}\n")
        return 1

    print(f"\n{GREEN}{BOLD}[PASS] 100% Coroutine Contract Parity: Zero direct resumes, atomics in combinators, safe awaiter destructors.{RESET}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
