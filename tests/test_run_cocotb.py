#!/usr/bin/env python3
# @verifies [SPEC-AUDIT-12] docs/SPECTRACE.md
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/run_cocotb.py: a registered suite is never skipped and nothing-ran is never a pass.
Runs with plain python3 or pytest."""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import run_cocotb as rc  # noqa: E402


def test_trailing_comment_is_registered():
    # 2026-11-05: this line was dropped by the runner while ctest registered it.
    t = rc.registered_tests("add_cocotb_test(sim-a top_a test_a)   # [SPEC-X-01]\n"
                            "add_cocotb_test(sim-b top_b test_b SIM_PARAMS=-GA=1)\n")
    assert [x[0] for x in t] == ["sim-a", "sim-b"]
    assert t[1][3] == ["SIM_PARAMS=-GA=1"]


def test_unparsable_line_is_an_error():
    try:
        rc.registered_tests("add_cocotb_test(only_two_args top)\n")
    except SystemExit:
        return
    raise AssertionError("an unparsable registration was skipped")


def test_no_match_is_not_a_pass():
    p = subprocess.run([sys.executable, str(ROOT / "tools" / "run_cocotb.py"), "--only", "no-such-target-xyz"],
                       capture_output=True, text=True, timeout=120,
                       env={"PATH": "/usr/bin:/bin:" + str(Path.home() / ".tools/oss-cad-suite/bin")})
    if "cocotb not found" in p.stderr:
        return  # no simulator on this machine: the runner already exits 2
    assert p.returncode != 0, p.stdout + p.stderr


def test_every_registered_line_parses():
    assert len(rc.registered_tests()) > 0


def test_runaway_is_killed_and_fails():
    import time
    t0 = time.monotonic()
    rc_, out, timed_out = rc.run_with_timeout(["sh", "-c", "echo start; sleep 30 & wait"], ROOT, None, 1)
    assert timed_out and rc_ != 0 and "start" in out
    assert time.monotonic() - t0 < 10, "the child (sleep) kept the runner waiting"


if __name__ == "__main__":
    test_trailing_comment_is_registered()
    test_unparsable_line_is_an_error()
    test_no_match_is_not_a_pass()
    test_every_registered_line_parses()
    test_runaway_is_killed_and_fails()
    print("[PASS] run_cocotb: comments allowed, unparsable lines fail, no match fails, runaway killed")
