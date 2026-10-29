#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Run every cocotb test registered in sim/CMakeLists.txt (add_cocotb_test lines).

Each run happens in a temporary copy of rtl/ and sim/cocotb/, so no build
output lands in the repository. A run passes only if cocotb prints a
`TESTS=n PASS=n FAIL=0` line with n > 0; a run that prints no such line did
not build or did not run and counts as a failure.

Usage:
  source ~/.tools/oss-cad-suite/environment
  python3 tools/run_cocotb.py [--only NAME_SUBSTRING] [--trace]

Skill: .agents/skills/cocotb-regression/SKILL.md
"""
# @impl [SPEC-AUDIT-06] docs/SPECTRACE.md (executable form of the cocotb-regression skill)

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEST_RE = re.compile(r"^\s*add_cocotb_test\(\s*(\S+)\s+(\S+)\s+(\S+)(.*)\)\s*$")
RESULT_RE = re.compile(r"TESTS=(\d+) PASS=(\d+) FAIL=(\d+)")


def registered_tests():
    tests = []
    for line in (ROOT / "sim" / "CMakeLists.txt").read_text().splitlines():
        m = TEST_RE.match(line)
        if m:
            extra = shlex.split(m.group(4))  # e.g. "SIM_PARAMS=-GA=1 -GB=2"
            tests.append((m.group(1), m.group(2), m.group(3), extra))
    return tests


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--only", default="", help="run targets whose name contains this text")
    ap.add_argument("--trace", action="store_true", help="enable FST waveform tracing (needs lz4)")
    ap.add_argument("--log", default="", help="write each target's full output to DIR/<target>.log")
    args = ap.parse_args()

    if not shutil.which("cocotb-config"):
        print("cocotb not found: source ~/.tools/oss-cad-suite/environment", file=sys.stderr)
        return 2

    tests = [t for t in registered_tests() if args.only in t[0]]
    failures = 0
    with tempfile.TemporaryDirectory(prefix="abstractx-cocotb-") as tmp:
        work = Path(tmp)
        shutil.copytree(ROOT / "rtl", work / "rtl")
        shutil.copytree(ROOT / "sim" / "cocotb", work / "sim" / "cocotb")
        cocotb_dir = work / "sim" / "cocotb"
        for name, top, module, extra in tests:
            shutil.rmtree(cocotb_dir / "sim_build", ignore_errors=True)
            cmd = ["make", f"TOPLEVEL={top}", f"MODULE={module}",
                   f"COCOTB_TOPLEVEL={top}", f"COCOTB_TEST_MODULES={module}", *extra]
            if not args.trace:
                cmd.append("TRACE=")
            env = dict(os.environ, ABSTRACTX_ROOT=str(ROOT))  # tests may import repo tools
            proc = subprocess.run(cmd, cwd=cocotb_dir, capture_output=True, text=True, env=env)
            out = proc.stdout + proc.stderr
            if args.log:
                Path(args.log).mkdir(parents=True, exist_ok=True)
                (Path(args.log) / f"{name}.log").write_text(out)
            results = RESULT_RE.findall(out)
            if results:
                total, passed, failed = (int(x) for x in results[-1])
                ok = proc.returncode == 0 and total > 0 and failed == 0 and passed == total
                summary = f"TESTS={total} PASS={passed} FAIL={failed}"
            else:
                ok = False
                summary = "DID NOT RUN (no TESTS= line)"
            failures += 0 if ok else 1
            print(f"{'PASS' if ok else 'FAIL'}  {name:<28} {top:<22} {summary}")
            if not ok:
                tail = [l for l in out.splitlines() if "Error" in l or "FAIL" in l or "assert" in l.lower()]
                for line in tail[-6:]:
                    print(f"      {line.strip()[:160]}")
    print(f"\n{len(tests) - failures}/{len(tests)} cocotb targets passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
