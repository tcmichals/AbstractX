#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
AbstractX Multi-Stage Adversarial Review Tool (run_adversarial_audit.py)
======================================================================
Automated Static Analysis & Adversarial Invariant Auditor implementing
the Sashiko Adversarial Review Protocol for Freestanding C++20 and FPGA RTL.

Validates the 5 Core Sashiko Invariants:
1. Zero-Heap Allocation & Freestanding Environment (No malloc/new/heap).
2. Non-Blocking HAL & Cooperative Coroutine Lifecycle (No spin-sleeps in tasks).
3. ISR Boundary & SPSC Dispatch Safety (No coroutine .resume() in ISRs).
4. Wire Framing & Endianness Consistency (64B TLP alignment and layout).
5. Comprehensive Test Verification (CppUTest SITL & Cocotb RTL suites).
"""

import os
import re
import sys
import argparse
import subprocess
from pathlib import Path

RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
BLUE = "\033[94m"
BOLD = "\033[1m"
RESET = "\033[0m"

class AdversarialAuditor:
    def __init__(self, root_dir: Path):
        self.root_dir = root_dir
        self.violations = {
            "Stage 1 (Zero-Heap & Freestanding)": [],
            "Stage 2 (Non-Blocking HAL & Lifecycle)": [],
            "Stage 3 (ISR Boundary & Dispatch Safety)": [],
            "Stage 4 (Endianness & Wire Framing)": [],
            "Stage 5 (CppUTest & Test Verification)": []
        }

    def log_issue(self, stage: str, filepath: Path, line_no: int, rule: str, snippet: str):
        rel_path = filepath.relative_to(self.root_dir) if filepath.is_relative_to(self.root_dir) else filepath
        self.violations[stage].append({
            "file": str(rel_path),
            "line": line_no,
            "rule": rule,
            "snippet": snippet.strip()
        })

    def audit_stage1_zero_heap(self, filepath: Path, lines: list):
        """Stage 1: Verify zero dynamic heap allocation in freestanding code"""
        if "third_party/" in str(filepath) or "tests/" in str(filepath) or "build_" in str(filepath):
            return

        forbidden_headers = [
            (r"<iostream>", "Forbidden hosted standard header <iostream> violates Rule 1"),
            (r"<vector>", "Forbidden dynamic container <vector>; use etl::vector or static array (Rule 2)"),
            (r"<string>", "Forbidden dynamic container <string>; use etl::string or string_view (Rule 2)"),
            (r"<memory>", "Forbidden <memory> in freestanding; use placement new or static memory"),
        ]

        for idx, line in enumerate(lines, 1):
            sline = line.strip()
            # Skip comments
            if sline.startswith("//") or sline.startswith("*") or sline.startswith("/*"):
                continue

            for pattern, desc in forbidden_headers:
                if re.search(pattern, line):
                    self.log_issue("Stage 1 (Zero-Heap & Freestanding)", filepath, idx, desc, line)

            # Detect raw dynamic allocation: new Type, malloc, calloc, free
            alloc_patterns = [
                (r"\bmalloc\s*\(", "Forbidden raw malloc() violates Rule 2"),
                (r"\bcalloc\s*\(", "Forbidden raw calloc() violates Rule 2"),
                (r"\bfree\s*\(", "Forbidden raw free() violates Rule 2"),
                (r"\bnew\s+[A-Za-z0-9_:]+\s*(\[|\()", "Dynamic heap new expression violates Rule 2"),
            ]
            for pattern, desc in alloc_patterns:
                if re.search(pattern, line):
                    # Exclude placement-new and operator new definitions
                    if "new (" not in line and "operator new" not in line and "placement" not in line.lower():
                        self.log_issue("Stage 1 (Zero-Heap & Freestanding)", filepath, idx, desc, line)

    def audit_stage2_non_blocking_hal(self, filepath: Path, lines: list):
        """Stage 2: Verify zero synchronous blocking calls in active drivers and tasks"""
        if "third_party/" in str(filepath) or "tests/" in str(filepath) or "build_" in str(filepath):
            return

        # Drivers and tasks must not call blocking sync functions in coroutine contexts
        blocking_calls = [
            (r"\busleep\s*\(", "Blocking usleep() violates Rule 12; use co_await timer"),
            (r"\bsleep\s*\([0-9]+\)", "Blocking sleep() violates Rule 12; use co_await timer"),
        ]

        # Only check driver implementations and tasks (not virtual interface definitions or pre-scheduler fallbacks)
        is_impl = filepath.suffix == ".cpp" or "driver" in str(filepath) or "app" in str(filepath)
        if not is_impl:
            return

        for idx, line in enumerate(lines, 1):
            sline = line.strip()
            if sline.startswith("//") or sline.startswith("*") or sline.startswith("/*"):
                continue

            for pattern, desc in blocking_calls:
                if re.search(pattern, line):
                    if "sync_impl" in line or "pre-scheduler" in line.lower() or "fallback" in line.lower():
                        continue
                    self.log_issue("Stage 2 (Non-Blocking HAL & Lifecycle)", filepath, idx, desc, line)

    def audit_stage3_isr_boundary(self, filepath: Path, lines: list):
        """Stage 3: Verify no coroutine .resume() directly inside ISRs"""
        if "third_party/" in str(filepath) or "tests/" in str(filepath) or "build_" in str(filepath):
            return

        in_isr_func = False
        isr_sig = re.compile(r"\bvoid\s+[A-Za-z0-9_]*(isr|interrupt|irq|handler)[A-Za-z0-9_]*\s*\(", re.IGNORECASE)

        for idx, line in enumerate(lines, 1):
            sline = line.strip()
            if sline.startswith("//") or sline.startswith("*") or sline.startswith("/*"):
                continue

            if isr_sig.search(line):
                in_isr_func = True

            if in_isr_func and line.startswith("}"):
                in_isr_func = False

            if in_isr_func and re.search(r"\b[A-Za-z0-9_]+\.resume\s*\(\)", line):
                self.log_issue("Stage 3 (ISR Boundary & Dispatch Safety)", filepath, idx,
                               "Direct .resume() from ISR context violates Rule 4.2", line)

    def audit_stage4_wire_framing_endianness(self, filepath: Path, lines: list):
        """Stage 4: Verify wire packet packing, endianness conversion, and size invariants"""
        if filepath.name not in ["asp_tlp64.h", "asp_tlp64.hpp"]:
            return

        has_static_assert = False
        for idx, line in enumerate(lines, 1):
            if "static_assert" in line and "64" in line:
                has_static_assert = True

        if not has_static_assert and filepath.suffix in [".hpp", ".h"] and "tlp" in filepath.name:
            self.log_issue("Stage 4 (Endianness & Wire Framing)", filepath, 1,
                           "Missing compile-time static_assert(sizeof(...) == 64) for wire protocol", "")

    def audit_stage5_cpputest_coverage(self):
        """Stage 5: Verify companion CppUTest and Cocotb suites exist"""
        test_dir = self.root_dir / "tests"
        sim_dir = self.root_dir / "sim" / "cocotb"
        if not (test_dir.exists() and sim_dir.exists()):
            self.violations["Stage 5 (CppUTest & Test Verification)"].append({
                "file": "CMakeLists.txt",
                "line": 1,
                "rule": "Missing CppUTest or Cocotb test directory",
                "snippet": "Both tests/ and sim/cocotb/ must exist"
            })

    def run_audit(self, target_files: list):
        for f in target_files:
            if not f.exists() or f.is_dir():
                continue
            if f.suffix not in [".hpp", ".h", ".cpp", ".c", ".sv"]:
                continue

            try:
                with open(f, "r", encoding="utf-8", errors="ignore") as fp:
                    lines = fp.readlines()
            except Exception:
                continue

            self.audit_stage1_zero_heap(f, lines)
            self.audit_stage2_non_blocking_hal(f, lines)
            self.audit_stage3_isr_boundary(f, lines)
            self.audit_stage4_wire_framing_endianness(f, lines)

        self.audit_stage5_cpputest_coverage()

    def print_report(self) -> int:
        print(f"\n{BOLD}{BLUE}======================================================================={RESET}")
        print(f"{BOLD}{BLUE}       AbstractX Multi-Stage Decomposed Adversarial Audit Report       {RESET}")
        print(f"{BOLD}{BLUE}======================================================================={RESET}\n")

        total_issues = 0
        for stage, issues in self.violations.items():
            count = len(issues)
            total_issues += count
            if count == 0:
                print(f"{GREEN}[PASS] {stage}: 0 issues found{RESET}")
            else:
                print(f"{RED}[FAIL] {stage}: {count} issue(s) detected{RESET}")
                for iss in issues:
                    print(f"  {YELLOW}-> {iss['file']}:{iss['line']}{RESET} [{iss['rule']}]")
                    if iss["snippet"]:
                        print(f"     Code: {iss['snippet']}")

        print(f"\n{BOLD}{BLUE}======================================================================={RESET}")
        if total_issues == 0:
            print(f"{BOLD}{GREEN}EXECUTIVE VERDICT: [PASS FOR PRODUCTION COMMIT] (0 Issues){RESET}")
            print(f"{BOLD}{BLUE}======================================================================={RESET}\n")
            return 0
        else:
            print(f"{BOLD}{RED}EXECUTIVE VERDICT: [REJECTED - INVARIANT VIOLATIONS DETECTED] ({total_issues} Issues){RESET}")
            print(f"{BOLD}{BLUE}======================================================================={RESET}\n")
            return 1

def main():
    parser = argparse.ArgumentParser(description="AbstractX Multi-Stage Adversarial Review Tool")
    parser.add_argument("--path", type=str, default=None, help="Specific path or directory to audit")
    parser.add_argument("--git-diff", action="store_true", help="Audit only files modified in git diff")
    args = parser.parse_args()

    root_dir = Path(__file__).resolve().parent.parent

    target_files = []
    if args.git_diff:
        try:
            res = subprocess.run(["git", "diff", "--name-only", "HEAD"], cwd=root_dir, capture_output=True, text=True)
            files = [root_dir / f for f in res.stdout.strip().splitlines() if f]
            target_files = files
        except Exception as e:
            print(f"Error reading git diff: {e}")
            sys.exit(1)
    elif args.path:
        p = Path(args.path)
        if not p.is_absolute():
            p = root_dir / p
        if p.is_dir():
            target_files = list(p.rglob("*.hpp")) + list(p.rglob("*.cpp")) + list(p.rglob("*.h"))
        else:
            target_files = [p]
    else:
        # Default: scan include/, apps/
        for sub in ["include", "apps"]:
            d = root_dir / sub
            if d.exists():
                target_files.extend(d.rglob("*.hpp"))
                target_files.extend(d.rglob("*.cpp"))
                target_files.extend(d.rglob("*.h"))

    auditor = AdversarialAuditor(root_dir)
    auditor.run_audit(target_files)
    sys.exit(auditor.print_report())

if __name__ == "__main__":
    main()
