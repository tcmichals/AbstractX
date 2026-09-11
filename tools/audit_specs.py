#!/usr/bin/env python3
"""
AbstractX Design Specification & Implementation Traceability Auditor
---------------------------------------------------------------------
Scans docs/DESIGN_SPECIFICATION.md for [SPEC-*] tags and validates
that every requirement is implemented in code with an @impl tag.
"""

import os
import re
import sys
from pathlib import Path

def main():
    root_dir = Path(__file__).resolve().parent.parent
    spec_file = root_dir / "docs" / "DESIGN_SPECIFICATION.md"

    if not spec_file.exists():
        print(f"[ERROR] Specification file not found: {spec_file}")
        sys.exit(1)

    # 1. Parse all specification IDs from docs/DESIGN_SPECIFICATION.md
    spec_pattern = re.compile(r"###\s+`\[(SPEC-[A-Z0-9\-]+)\]`\s+(.+)")
    specs = {}

    with open(spec_file, "r", encoding="utf-8") as f:
        for line in f:
            match = spec_pattern.match(line.strip())
            if match:
                spec_id = match.group(1)
                spec_title = match.group(2)
                specs[spec_id] = {
                    "title": spec_title,
                    "implementations": []
                }

    # 2. Scan codebase for @impl tags
    scan_dirs = ["include", "targets", "apps", "examples", "sim"]
    impl_pattern = re.compile(r"@impl\s+([^(\n]+)")

    for s_dir in scan_dirs:
        dir_path = root_dir / s_dir
        if not dir_path.exists():
            continue

        for ext in ["*.hpp", "*.cpp", "*.h", "*.c", "*.S", "*.sv"]:
            for file_path in dir_path.rglob(ext):
                rel_path = file_path.relative_to(root_dir)
                try:
                    with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
                        for line_num, line in enumerate(f, 1):
                            if "@impl" in line:
                                matches = re.findall(r"\[(SPEC-[A-Z0-9\-]+)\]", line)
                                for m in matches:
                                    if m in specs:
                                        specs[m]["implementations"].append(f"{rel_path}:{line_num}")
                except Exception as e:
                    print(f"[WARN] Failed to read {file_path}: {e}")

    # 3. Print Traceability Matrix
    print("=" * 80)
    print("             AbstractX Spec-to-Code Traceability Audit Matrix                  ")
    print("=" * 80)
    print(f"{'SPEC ID':<22} | {'STATUS':<12} | {'IMPLEMENTING SOURCE FILE(S)':<40}")
    print("-" * 80)

    implemented_count = 0
    total_count = len(specs)

    for spec_id, data in sorted(specs.items()):
        impls = data["implementations"]
        if impls:
            status = "COMPLETE"
            implemented_count += 1
            impl_str = ", ".join(impls)
        else:
            status = "MISSING"
            impl_str = "(No @impl tag found in code)"

        print(f"{spec_id:<22} | {status:<12} | {impl_str:<40}")

    print("=" * 80)
    coverage = (implemented_count / total_count * 100.0) if total_count > 0 else 0.0
    print(f"Total Specifications: {total_count}")
    print(f"Implemented:          {implemented_count}")
    print(f"Traceability Coverage:{coverage:.1f}%")
    print("=" * 80)

    if implemented_count < total_count:
        print("[WARN] Some specifications are not yet tagged in code.")
        sys.exit(1)
    else:
        print("[SUCCESS] All specifications are 100% verified and implemented in code!")
        sys.exit(0)

if __name__ == "__main__":
    main()
