# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
testTrac checks: ctest suites, cocotb RTL regression, RTL lint and synthesis
structure, traceability matrix. Parsers take text so they are unit-tested.
"""
# @impl [SPEC-AUDIT-09] docs/SPECTRACE.md

import os
import re
import subprocess
import time
from pathlib import Path

CTEST_RE = re.compile(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)")
CTEST_TIME_RE = re.compile(r"Total Test time \(real\) =\s+([\d.]+) sec")
COCOTB_RE = re.compile(r"(\d+)/(\d+) cocotb targets passed")
COCOTB_TESTS_RE = re.compile(r"TESTS=(\d+) PASS=(\d+) FAIL=(\d+)")
MATRIX_RE = re.compile(r"^\s+(VERIFIED|IMPLEMENTED|PARTIAL|SPEC-ONLY|BROKEN)\s*:\s*(\d+)", re.M)
MATRIX_TOTAL_RE = re.compile(r"Requirements : (\d+)")
CRITICAL_LINT = ("MULTIDRIVEN", "SYNCASYNCNET", "CASEINCOMPLETE", "LATCH", "UNDRIVEN")
OSS_CAD = Path(os.path.expanduser("~/.tools/oss-cad-suite"))


def parse_ctest(out: str) -> dict:
    m = CTEST_RE.search(out)
    if not m:
        return {}
    failed, total = int(m.group(2)), int(m.group(3))
    t = CTEST_TIME_RE.search(out)
    return {"tests": total, "passed": total - failed, "failed": failed,
            "test_time_s": float(t.group(1)) if t else 0.0}


def parse_cocotb(out: str) -> dict:
    m = COCOTB_RE.search(out)
    if not m:
        return {}
    tests = [tuple(map(int, x)) for x in COCOTB_TESTS_RE.findall(out)]
    return {"targets": int(m.group(2)), "targets_passed": int(m.group(1)),
            "tests": sum(t[0] for t in tests), "tests_failed": sum(t[2] for t in tests)}


def parse_matrix(out: str) -> dict:
    counts = {k.lower().replace("-", "_"): int(v) for k, v in MATRIX_RE.findall(out)}
    total = MATRIX_TOTAL_RE.search(out)
    if total:
        counts["total"] = int(total.group(1))
        counts["verified_pct"] = round(100.0 * counts.get("verified", 0) / max(counts["total"], 1), 1)
    return counts


def oss_env() -> dict:
    env = dict(os.environ)
    if (OSS_CAD / "bin").is_dir():
        env["PATH"] = f"{OSS_CAD / 'bin'}:{OSS_CAD / 'py3bin'}:{env.get('PATH', '')}"
    return env


def run(cmd, cwd, env=None, timeout=7200):
    start = time.monotonic()
    proc = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, timeout=timeout)
    return proc.returncode, proc.stdout + proc.stderr, time.monotonic() - start


def rtl_files(root: Path) -> list[Path]:
    return sorted(list((root / "rtl").glob("*.sv")) + list((root / "rtl").glob("*/*.sv")))


def run_rtl_lint(root: Path) -> tuple[dict, list[str], float]:
    files = [str(p) for p in rtl_files(root)]
    issues, start = [], time.monotonic()
    for f in files:
        top = Path(f).stem
        _, out, _ = run(["verilator", "--lint-only", "-Wall", "-Wno-DECLFILENAME", "-Wno-UNUSEDSIGNAL",
                         "-Wno-UNUSEDPARAM", "-Wno-PINCONNECTEMPTY", *files, "--top-module", top],
                        root, oss_env(), timeout=600)
        issues += [f"{top}: {l.strip()[:140]}" for l in out.splitlines()
                   if l.startswith("%Warning-") and any(f"-{c}:" in l for c in CRITICAL_LINT)]
    issues = sorted(set(issues))
    return {"modules": len(files), "critical_warnings": len(issues)}, issues, time.monotonic() - start


def run_rtl_synth(root: Path) -> tuple[dict, list[str], float]:
    files = rtl_files(root)
    bad, start = [], time.monotonic()
    for f in files:
        top = f.stem
        script = f"read_verilog -sv {' '.join(str(p) for p in files)}; hierarchy -top {top}; proc; check"
        _, out, _ = run(["yosys", "-q", "-p", script], root, oss_env(), timeout=900)
        if "multiple conflicting drivers" in out:
            bad.append(f"{top}: multiple conflicting drivers")
    return {"modules": len(files), "multi_driver_modules": len(bad)}, bad, time.monotonic() - start
