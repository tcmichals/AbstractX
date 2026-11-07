#!/usr/bin/env python3
# @impl [SPEC-AUDIT-07] [SPEC-AUDIT-08] [SPEC-AUDIT-12] docs/SPECTRACE.md
"""
AbstractX Design Specification & Implementation Traceability Auditor
---------------------------------------------------------------------
Scans every specification file (or the one given) for [SPEC-*] tags and validates
that every requirement has an @impl tag in code. This is traceability only:
a tag says nothing about whether the code works. Completion is the
`python3 tools/spectrace_matrix.py --matrix` verdict ([SPEC-AUDIT-12]).
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

    # 1. Parse all specification IDs from specification markdown (with or without backticks)
    spec_pattern = re.compile(r"###\s+`?\[(SPEC-[A-Z0-9\-]+)\]`?\s+(.+)")
    # @impl [SPEC-AUDIT-07] docs/SPECTRACE.md (spec before code: a declared "Not implemented yet" is PLANNED)
    not_impl_pattern = re.compile(r"^\s*\*\s*\*\*Not implemented yet\*\*:\s*(.*)$")
    specs = {}

    current = None
    with open(spec_file, "r", encoding="utf-8") as f:
        for line in f:
            match = spec_pattern.match(line.strip())
            if match:
                spec_id = match.group(1)
                spec_title = match.group(2)
                specs[spec_id] = {
                    "title": spec_title,
                    "implementations": [],
                    "planned": None,
                }
                current = spec_id
            elif line.startswith("## ") or line.startswith("### "):
                current = None
            elif current is not None:
                ni = not_impl_pattern.match(line)
                if ni:
                    specs[current]["planned"] = ni.group(1).strip() or "not implemented yet"

    # 2. Scan codebase for @impl tags
    # drivers/ and src/ were missing until 2026-10-23: tags in them were silently not counted
    # (engineering_log.md 2026-10-23, [SPEC-ARCH-12]).
    # cmake/ added 2026-11-03: toolchain-file tags ([SPEC-BUILD-02]) were not counted.
    # "." is the root CMakeLists.txt only, added 2026-11-06: its [SPEC-BUILD-03] tag was not counted.
    scan_dirs = [".", "include", "src", "applib", "targets", "apps", "examples", "sim", "tools", "rtl", "hw", "trace", "cmake"]
    valid_exts = {
        ".hpp", ".cpp", ".h", ".c", ".S", ".s", ".sv", ".py", ".xdc",
        ".ld", ".dts", ".dtsi", ".dtso", ".yaml", ".yml", ".cmake",
    }
    valid_names = {"CMakeLists.txt"}
    skip_dirs = {"build", "bld", ".git", "dl", "venv", ".system_generated"}

    for s_dir in scan_dirs:
        dir_path = root_dir / s_dir
        if not dir_path.exists():
            continue

        walker = [(root_dir, [], ["CMakeLists.txt"])] if s_dir == "." else os.walk(dir_path)
        for root, dirs, files in walker:
            dirs[:] = [d for d in dirs if d not in skip_dirs]
            for file_name in files:
                file_path = Path(root) / file_name
                if file_path.suffix not in valid_exts and file_name not in valid_names:
                    continue
                rel_path = file_path.relative_to(root_dir)
                try:
                    with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
                        for line_num, line in enumerate(f, 1):
                            if "@impl" in line:
                                matches = re.findall(r"\[(SPEC-[A-Z0-9\-]+)\]", line)
                                for m in matches:
                                    if m in specs:
                                        specs[m]["implementations"].append(f"{rel_path}:{line_num}")
                except Exception:
                    pass

    implemented_count = sum(1 for data in specs.values() if data["implementations"])
    planned_count = sum(1 for data in specs.values() if not data["implementations"] and data["planned"])
    total_count = len(specs)
    coverage = (implemented_count / total_count * 100.0) if total_count > 0 else 0.0

    return {
        "spec_file": str(spec_file),
        "total": total_count,
        "implemented": implemented_count,
        "planned": planned_count,
        "coverage": coverage,
        "specs": specs
    }

def audit_all(root_dir: Path) -> dict:
    """[SPEC-AUDIT-12] all specifications: per-file results merged, untagged listed with their file."""
    # @impl [SPEC-AUDIT-12] docs/SPECTRACE.md (all specifications, P0-07)
    sys.path.insert(0, str(root_dir / "tools"))
    from spectrace_drift import discover_all_specs
    total = implemented = planned = 0
    per_file, untagged = [], []
    for spec in discover_all_specs(root_dir):
        if not spec.exists():
            continue
        r = run_traceability_audit(spec, root_dir)
        if r["total"] == 0:
            continue
        rel = spec.relative_to(root_dir).as_posix()
        total, implemented, planned = total + r["total"], implemented + r["implemented"], planned + r["planned"]
        per_file.append((rel, r["total"], r["implemented"], r["planned"]))
        untagged += [(rel, k) for k, d in sorted(r["specs"].items()) if not d["implementations"] and not d["planned"]]
    return {"total": total, "implemented": implemented, "planned": planned,
            "per_file": per_file, "untagged": untagged}


def matrix_line(root_dir: Path) -> str:
    """The completion figure from spectrace_matrix ([SPEC-AUDIT-07]); read it together with tag coverage."""
    sys.path.insert(0, str(root_dir / "tools"))
    import spectrace_matrix as sm
    reqs, skills, _ = sm.build_matrix(root_dir)
    counts: dict[str, int] = {}
    for r in reqs.values():
        v = r.verdict(set(skills))
        counts[v] = counts.get(v, 0) + 1
    done = counts.get("VERIFIED", 0)
    detail = ", ".join(f"{k} {counts[k]}" for k in sorted(counts))
    return (f"Complete (implemented AND verified): {done}/{len(reqs)} = {100.0 * done / max(len(reqs), 1):.1f}% "
            f"({detail}; tools/spectrace_matrix.py --matrix)")


def main():
    root_dir = Path(__file__).resolve().parent.parent
    if len(sys.argv) > 2 and sys.argv[1] == "--spec":
        spec_file = Path(sys.argv[2])
    elif len(sys.argv) > 1 and not sys.argv[1].startswith("--"):
        spec_file = Path(sys.argv[1])
    else:
        spec_file = None

    if spec_file is None:
        res = audit_all(root_dir)
        print("=" * 80)
        print("        AbstractX Spec-to-Code Traceability (all specifications; tags only)     ")
        print("=" * 80)
        for rel, t, i, p in res["per_file"]:
            print(f"{rel:<62} {i:>3}/{t:<3} tagged  {p} planned")
        print("-" * 80)
        for rel, tag in res["untagged"]:
            print(f"[UNTAGGED] {tag:<26} {rel}  (no @impl, not declared 'Not implemented yet')")
        print("=" * 80)
        print(f"Requirements:         {res['total']}")
        print(f"Tagged in code:       {res['implemented']}")
        print(f"Planned (declared):   {res['planned']}")
        print(f"Untagged:             {len(res['untagged'])}")
        print(f"Tag coverage:         {100.0 * res['implemented'] / max(res['total'], 1):.1f}% (traceability only)")
        print(matrix_line(root_dir))
        print("=" * 80)
        sys.exit(1 if res["untagged"] else 0)

    try:
        results = run_traceability_audit(spec_file, root_dir)
    except FileNotFoundError as e:
        print(f"[ERROR] {e}")
        sys.exit(1)

    specs = results["specs"]
    total_count = results["total"]
    implemented_count = results["implemented"]
    coverage = results["coverage"]

    print("=" * 80)
    print("             AbstractX Spec-to-Code Traceability Audit Matrix                  ")
    print("=" * 80)
    print(f"{'SPEC ID':<22} | {'STATUS':<12} | {'IMPLEMENTING SOURCE FILE(S)':<40}")
    print("-" * 80)

    for spec_id, data in sorted(specs.items()):
        impls = data["implementations"]
        if impls:
            status = "TAGGED"
            impl_str = ", ".join(impls)
        elif data["planned"]:
            status = "PLANNED"
            impl_str = f"(not implemented yet: {data['planned'][:60]})"
        else:
            status = "MISSING"
            impl_str = "(No @impl tag found in code)"

        print(f"{spec_id:<22} | {status:<12} | {impl_str:<40}")

    print("=" * 80)
    planned_count = results["planned"]
    print(f"Total Specifications: {total_count}")
    print(f"Tagged in code:       {implemented_count}")
    print(f"Planned (declared):   {planned_count}")
    print(f"Traceability Coverage:{coverage:.1f}%")
    print("=" * 80)

    if implemented_count + planned_count < total_count:
        print("[WARN] Some specifications are not yet tagged in code and not declared 'Not implemented yet'.")
        sys.exit(1)
    print("[OK] Every specification in this file is tagged in code or declared planned "
          "(traceability only, not verification; see tools/spectrace_matrix.py --matrix).")
    sys.exit(0)


if __name__ == "__main__":
    main()

