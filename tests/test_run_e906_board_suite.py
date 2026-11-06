#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/run_e906_board_suite.py without a board: every file it copies exists, the dry run prints the
three steps, and a suite result becomes a ledger record the matrix reads as board evidence.

@verifies [SPEC-E906-REPL-15] apps/e906_repl_app/SPECIFICATION.md (make_record commands)
"""
import io
import contextlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import run_e906_board_suite as r  # noqa: E402


def main() -> int:
    missing = [f for f in r.SUITE_FILES if not (ROOT / f).is_file()]
    assert not missing, f"files the suite needs are missing: {missing}"
    assert "tests/board/run_board_tests.py" in r.SUITE_FILES
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        assert r.main(["--ip", "10.0.0.9", "--dry-run", "-k", "uart"]) == 0
    lines = out.getvalue().splitlines()
    assert len(lines) == 3 and lines[0].startswith("ssh root@10.0.0.9") and lines[1].startswith("scp -O")
    assert "timeout 600 python3 run_board_tests.py" in lines[2] and "-k uart" in lines[2], lines[2]
    rec = r.make_record({"counts": {"PASS": 23, "FAIL": 0, "SKIP": 2}, "firmware": "e906_repl_app.elf",
                         "uname": "7.1.0"}, "abc123def456", False, 0, "2026-11-06T12:00:00Z")
    assert rec["source"] == "board" and rec["result"] == "pass" and rec["check"] == "e906-board-selftest"
    assert rec["metrics"] == {"passed": 23, "failed": 0, "skipped": 2, "firmware": "e906_repl_app.elf", "uname": "7.1.0"}
    bad = r.make_record({"counts": {"PASS": 20, "FAIL": 1, "SKIP": 0}}, "abc", False, 1, "2026-11-06T12:00:00Z")
    assert bad["result"] == "fail"
    assert r.make_record({"counts": {"PASS": 3}}, "abc", False, 2, "t")["result"] == "fail", "could not run is not a pass"
    with contextlib.redirect_stdout(io.StringIO()):
        import os
        os.environ.pop("ABSTRACTX_BOARD_IP", None)
        assert r.main([]) == 2, "no board -> could not run"
    print("[PASS] run_e906_board_suite")
    return 0


if __name__ == "__main__":
    sys.exit(main())
