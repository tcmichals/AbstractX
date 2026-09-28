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

def run_traceability_audit(spec_file=None, root_dir=None):
    """Programmatically runs traceability audit and returns audit results dictionary."""
    if root_dir is None:
        root_dir = Path(__file__).resolve().parent.parent
    if spec_file is None:
        spec_file = root_dir / "docs" / "DESIGN_SPECIFICATION.md"
    elif not isinstance(spec_file, Path):
        spec_file = Path(spec_file)
    if not spec_file.is_absolute():
        spec_file = root_dir / spec_file

    if not spec_file.exists():
        raise FileNotFoundError(f"Specification file not found: {spec_file}")

    # 1. Parse all specification IDs from specification markdown
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
                    pass

    implemented_count = sum(1 for data in specs.values() if data["implementations"])
    total_count = len(specs)
    coverage = (implemented_count / total_count * 100.0) if total_count > 0 else 0.0

    return {
        "spec_file": str(spec_file),
        "total": total_count,
        "implemented": implemented_count,
        "coverage": coverage,
        "specs": specs
    }

def main():
    root_dir = Path(__file__).resolve().parent.parent
    if len(sys.argv) > 1:
        spec_file = Path(sys.argv[1])
    else:
        spec_file = root_dir / "docs" / "DESIGN_SPECIFICATION.md"

    try:
        results = run_traceability_audit(spec_file, root_dir)
    except FileNotFoundError as e:
        print(f"[ERROR] {e}")
        sys.exit(1)

    specs = results["specs"]
    total_count = results["total"]
    implemented_count = results["implemented"]
    coverage = results["coverage"]

    # 3. Print Traceability Matrix
    print("=" * 80)
    print("             AbstractX Spec-to-Code Traceability Audit Matrix                  ")
    print("=" * 80)
    print(f"{'SPEC ID':<22} | {'STATUS':<12} | {'IMPLEMENTING SOURCE FILE(S)':<40}")
    print("-" * 80)

    for spec_id, data in sorted(specs.items()):
        impls = data["implementations"]
        if impls:
            status = "COMPLETE"
            impl_str = ", ".join(impls)
        else:
            status = "MISSING"
            impl_str = "(No @impl tag found in code)"

        print(f"{spec_id:<22} | {status:<12} | {impl_str:<40}")

    print("=" * 80)
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

