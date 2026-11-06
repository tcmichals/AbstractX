#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/run_e906_unit_tests.py without a board: trace0 parsing, the remote script always restores the
previous firmware, records, dry run, no board.

@verifies [SPEC-AUDIT-18] docs/SPECTRACE.md (parse make_record remote_script)
"""
import contextlib
import io
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import run_e906_unit_tests as r  # noqa: E402

TRACE = """[E906] unit tests
PASS tlp_layout_offsets
  CHECK failed: ring.pop(v) && v == i at tests/unit/unit_ring.cpp:20
FAIL spsc_ring_order_and_bounds
UNIT: 1 passed, 1 failed
"""


def main() -> int:
    res = r.parse(TRACE)
    assert res["passed"] == 1 and res["failed"] == 1, res
    assert res["tests"] == [("PASS", "tlp_layout_offsets"), ("FAIL", "spsc_ring_order_and_bounds")]
    assert r.parse("[E906] unit tests\n")["passed"] is None, "no UNIT line -> could not run"
    rec = r.make_record(res, "abc", False, "2026-11-06T00:00:00Z")
    assert rec["result"] == "fail" and rec["source"] == "board" and rec["check"] == "e906-unit-tests"
    assert r.make_record(r.parse("UNIT: 4 passed, 0 failed\n"), "abc", False, "t")["result"] == "pass"
    script = r.remote_script(30)
    assert "trap restore EXIT" in script and 'echo "$OLD" > $R/firmware' in script, "always restores the old firmware"
    assert "seq 1 300" in script and r.TRACE0 in script
    with contextlib.redirect_stdout(io.StringIO()) as out:
        assert r.main(["--ip", "10.0.0.9", "--dry-run"]) == 0
    assert "/lib/firmware/e906_unit_tests.elf" in out.getvalue()
    os.environ.pop("ABSTRACTX_BOARD_IP", None)
    with contextlib.redirect_stdout(io.StringIO()):
        assert r.main([]) == 2
    print("[PASS] run_e906_unit_tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())
