#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-AUDIT-15] docs/SPECTRACE.md
"""
run_static_analysis.py - clang-tidy (and optionally GCC -fanalyzer) on first-party code.

  python3 tools/run_static_analysis.py                 # clang-tidy on build_host/compile_commands.json
  python3 tools/run_static_analysis.py --fanalyzer     # also an E906 build with -fanalyzer
  python3 tools/run_static_analysis.py --write-baseline   # record today's findings (first triage only)

A finding is reported as <file>:<check>. Findings listed in tools/static_analysis_baseline.txt
are known (printed, not failing); a new finding fails; a baseline entry that no longer occurs
also fails, so the baseline only shrinks (same rule as tools/audit_baseline.txt).
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / "tools" / "static_analysis_baseline.txt"
FIRST_PARTY = ("include/", "src/", "targets/", "applib/", "apps/")
SKIP = ("third_party/", "build", "/micropython/", "/lib/")
FINDING = re.compile(r"^(?P<file>[^:\s]+):\d+:\d+: (?:warning|error): .*\[(?P<check>[\w.,-]+)\]$")
GCC_FINDING = re.compile(r"^(?P<file>[^:\s]+):\d+:\d+: warning: .*\[(?P<check>-Wanalyzer-[\w-]+)\]$")


def rel(path: str) -> str:
    p = Path(path)
    try:
        return p.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return p.as_posix()


def first_party(path: str) -> bool:
    r = rel(path)
    return r.startswith(FIRST_PARTY) and not any(s in r for s in SKIP)


def run_clang_tidy(build_dir: Path, jobs: int) -> set:
    db = build_dir / "compile_commands.json"
    if not db.exists():
        sys.exit(f"[ERROR] {db} missing: configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
    tidy = shutil.which("clang-tidy")
    if not tidy:
        sys.exit("[ERROR] clang-tidy not found (apt install clang-tidy)")
    files = sorted({e["file"] for e in json.loads(db.read_text()) if first_party(e["file"])})

    def one(f):
        r = subprocess.run([tidy, "-p", str(build_dir), "--quiet", f], capture_output=True, text=True)
        return r.stdout

    found = set()
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for out in pool.map(one, files):
            for line in out.splitlines():
                m = FINDING.match(line.strip())
                if m and first_party(m.group("file")):
                    for check in m.group("check").split(","):
                        found.add(f"{rel(m.group('file'))}:{check}")
    return found


def run_fanalyzer(jobs: int) -> set:
    build = ROOT / "build_e906_fanalyzer"
    cfg = subprocess.run(["cmake", "--preset", "e906", "-B", str(build),
                          "-DCMAKE_CXX_FLAGS=-fanalyzer", "-DCMAKE_C_FLAGS=-fanalyzer"],
                         cwd=ROOT, capture_output=True, text=True)
    if cfg.returncode != 0:
        sys.exit(f"[ERROR] E906 configure failed (toolchain in ~/.tools?):\n{cfg.stderr[-2000:]}")
    out = subprocess.run(["cmake", "--build", str(build), "-j", str(jobs)], cwd=ROOT,
                         capture_output=True, text=True)
    found = set()
    for line in (out.stdout + out.stderr).splitlines():
        m = GCC_FINDING.match(line.strip())
        if m and first_party(m.group("file")):
            found.add(f"{rel(m.group('file'))}:{m.group('check')}")
    return found


def load_baseline() -> set:
    if not BASELINE.exists():
        return set()
    entries = set()
    for line in BASELINE.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            entries.add(line)
    return entries


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--build-dir", type=Path, default=ROOT / "build_host")
    ap.add_argument("--fanalyzer", action="store_true", help="also build the E906 target with -fanalyzer")
    ap.add_argument("--write-baseline", action="store_true", help="record current findings as known")
    ap.add_argument("-j", "--jobs", type=int, default=8)
    args = ap.parse_args()

    found = run_clang_tidy(args.build_dir, args.jobs)
    if args.fanalyzer:
        found |= run_fanalyzer(args.jobs)

    if args.write_baseline:
        lines = ["# Known static-analysis findings ([SPEC-AUDIT-15]); this list only shrinks.",
                 "# Format: <file>:<check>  # <engineering_log entry or reason>"]
        lines += [f"{f}  # first triage, not yet reviewed" for f in sorted(found)]
        BASELINE.write_text("\n".join(lines) + "\n")
        print(f"[BASELINE] wrote {len(found)} finding(s) to {BASELINE.relative_to(ROOT)}")
        return 0

    baseline = load_baseline()
    new = sorted(found - baseline)
    gone = sorted(baseline - found)
    for f in sorted(found & baseline):
        print(f"  known: {f}")
    for f in new:
        print(f"  NEW:   {f}")
    for f in gone:
        print(f"  FIXED (remove from baseline): {f}")
    if new or gone:
        print(f"[FAIL] {len(new)} new finding(s), {len(gone)} stale baseline entr(ies)")
        return 1
    print(f"[PASS] static analysis: {len(found)} known finding(s), no new ones")
    return 0


if __name__ == "__main__":
    sys.exit(main())
