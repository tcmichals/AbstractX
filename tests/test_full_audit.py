#!/usr/bin/env python3
# @verifies [SPEC-AUDIT-17] docs/SPECTRACE.md
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/full_audit.py and tools/gate.sh never report a stage that did not run as passing.
Runs with plain python3 or pytest."""

import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import full_audit as fa  # noqa: E402


def test_failing_stage_fails_the_audit():
    saved = dict(fa.AUTO_STAGES)
    try:
        fa.AUTO_STAGES.clear()
        fa.AUTO_STAGES["ok"] = lambda a: fa.Result("ok", "ok", fa.PASS, "")
        fa.AUTO_STAGES["bad"] = lambda a: fa.Result("bad", "bad", fa.FAIL, "")
        with tempfile.TemporaryDirectory() as d:
            sys.argv = ["full_audit.py", "--stages", "ok,bad", "--out", f"{d}/r.md"]
            assert fa.main() == 1
            report = Path(d, "r.md").read_text()
            assert "**FAIL**" in report and "never PASS" in report
            sys.argv = ["full_audit.py", "--stages", "ok", "--out", f"{d}/r.md"]
            assert fa.main() == 0
    finally:
        fa.AUTO_STAGES.clear()
        fa.AUTO_STAGES.update(saved)


def test_timeout_is_not_a_pass():
    rc, out = fa.sh(["sleep", "5"], timeout=1)
    assert rc == 124 and "timeout" in out


def test_missing_tool_is_skipped_not_passed():
    saved = fa.have
    try:
        fa.have = lambda tool: None
        for stage in (fa.stage_static, fa.stage_mutation, fa.stage_coverage, fa.stage_ci):
            r = stage(None)
            assert r.status == fa.SKIPPED and r.summary, (stage.__name__, r)
    finally:
        fa.have = saved


def test_static_analysis_is_skipped_when_required_clang_tidy_is_missing():
    saved_have, saved_sh = fa.have, fa.sh
    try:
        fa.have = lambda tool: "/fake/cppcheck" if tool == "cppcheck" else None
        fa.sh = lambda cmd, timeout, cwd=fa.ROOT: (0, "")
        r = fa.stage_static(None)
        assert r.status == fa.SKIPPED
        assert "clang-tidy not installed" in r.summary
        assert any("cppcheck (advisory" in detail for detail in r.details)
    finally:
        fa.have, fa.sh = saved_have, saved_sh


def test_gate_stops_on_build_error_before_ctest():
    """A fake cmake whose --build fails and a fake ctest that leaves a marker: no marker allowed."""
    with tempfile.TemporaryDirectory() as d:
        marker = Path(d, "ctest_ran")
        for name, body in (("cmake", '[ "$1" = "--build" ] && { echo "error: boom"; exit 2; }; exit 0'),
                           ("ctest", f'touch "{marker}"; exit 0')):
            p = Path(d, name)
            p.write_text(f"#!/bin/sh\n{body}\n")
            p.chmod(p.stat().st_mode | stat.S_IEXEC)
        env = dict(os.environ, PATH=f"{d}:{os.environ['PATH']}", GATE_BUILD_PREFIX=f"{d}/b")
        p = subprocess.run(["bash", str(ROOT / "tools" / "gate.sh"), "--quick"], cwd=ROOT, env=env,
                           capture_output=True, text=True, timeout=120)
        assert p.returncode != 0, p.stdout
        assert "build failed" in p.stdout and "no tests run" in p.stdout, p.stdout
        assert not marker.exists(), "ctest ran after a failed build"


def test_coverage_uses_gcov_of_the_compiler_version():
    # A GCC 14 build read by gcov 13 fails (gcov exit 3); pick gcov-<major> of CMAKE_CXX_COMPILER.
    with tempfile.TemporaryDirectory() as d:
        cxx = Path(d, "fake-c++")
        cxx.write_text("#!/bin/sh\necho 99.1.0\n")
        gcov = Path(d, "gcov-99")
        gcov.write_text("#!/bin/sh\nexit 0\n")
        for p in (cxx, gcov):
            p.chmod(p.stat().st_mode | stat.S_IEXEC)
        Path(d, "CMakeCache.txt").write_text(f"CMAKE_CXX_COMPILER:FILEPATH={cxx}\n")
        saved = os.environ["PATH"]
        os.environ["PATH"] = f"{d}:{saved}"
        try:
            assert fa.matching_gcov(Path(d)) == "gcov-99"
            gcov.unlink()
            assert fa.matching_gcov(Path(d)) == "gcov", "falls back when gcov-<major> is missing"
        finally:
            os.environ["PATH"] = saved


if __name__ == "__main__":
    test_failing_stage_fails_the_audit()
    test_timeout_is_not_a_pass()
    test_missing_tool_is_skipped_not_passed()
    test_static_analysis_is_skipped_when_required_clang_tidy_is_missing()
    test_gate_stops_on_build_error_before_ctest()
    test_coverage_uses_gcov_of_the_compiler_version()
    print("[PASS] full_audit / gate: failures fail, timeouts and missing tools never pass, build error stops ctest")
