#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Tests for tools/trac.py and tools/trac_lib (sizeTrac / perfTrac, evidence ledger).
@verifies [SPEC-AUDIT-09] docs/SPECTRACE.md
"""

import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from trac_lib import ledger  # noqa: E402
from trac_lib import size_perf as sp  # noqa: E402
from trac_lib import test_checks as tc  # noqa: E402

SIZE_A = """build/app  :
section              size         addr
.vectors              380   1073479680
.text               72000   1073480064
.data                 340   1073552064
.bss               110000   1073552404
.trace_buffer        4096   1073741892
.comment               43            0
Total              186859
"""

NEXTPNR = """Info: Max frequency for clock 'clk': 110.19 MHz (PASS at 27.00 MHz)
Info: Routing complete.
Info: Device utilisation:
Info: 	                LUT4:    4747/  20736    22%
Info: 	                 DFF:    4759/  15552    30%
Info: 	               BSRAM:       0/     46     0%
Info: Max frequency for clock 'clk': 121.85 MHz (PASS at 27.00 MHz)
"""


def test_size_parsing_and_models():
    secs = sp.parse_size_A(SIZE_A)
    assert secs[".text"] == 72000 and "Total" not in secs
    m = sp.size_metrics(secs, "sram_only")
    assert m["text"] == 72380 and m["bss"] == 114096 and m["ram_bytes"] == 72380 + 340 + 114096
    m2 = sp.size_metrics(secs, "flash_ram")
    assert m2["flash_bytes"] == 72380 + 340 and m2["ram_bytes"] == 340 + 114096


def test_size_budget_and_growth():
    t = {"ram_budget_bytes": 196608, "max_allowed_growth_bytes": 2048}
    ok = {"ram_bytes": 186000, "flash_bytes": 0}
    assert sp.judge_size(ok, t, None)[0] == "pass"
    assert sp.judge_size({"ram_bytes": 200000, "flash_bytes": 0}, t, None)[0] == "fail"
    res, why = sp.judge_size({"ram_bytes": 189000, "flash_bytes": 0}, t, ok)
    assert res == "fail" and "grew 3000" in why


def test_nextpnr_routed_fmax_is_the_last_line():
    p = sp.parse_nextpnr(NEXTPNR)
    assert p["fmax"]["clk"]["fmax_mhz"] == 121.85, "post-route value, not the placement estimate"
    assert p["util"]["LUT4"] == {"used": 4747, "total": 20736, "pct": 22}


def test_ledger_append_read_delta():
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        subprocess.run(["git", "init", "-q"], cwd=root, check=True)
        r1 = ledger.make_record(root, "size", "elf:x", "pass", {"ram_bytes": 100})
        r1["time"], r1["host"] = "2026-10-01T00:00:00Z", "pc-1"
        ledger.append(root, r1)
        r2 = ledger.make_record(root, "size", "elf:x", "pass", {"ram_bytes": 120})
        r2["time"], r2["host"] = "2026-11-02T00:00:00Z", "pc-2"
        records = ledger.read_all(root)
        ledger.with_delta(r2, ledger.previous(records, "elf:x"))
        assert r2["delta"] == {"ram_bytes": 20}
        path = ledger.append(root, r2)
        assert path.parts[-2:] == ("2026-11", "pc-2.jsonl"), "one file per month and host"
        assert len(ledger.read_all(root)) == 2


def test_test_parsers():
    ct = tc.parse_ctest("100% tests passed, 0 tests failed out of 31\n\nTotal Test time (real) =  12.34 sec")
    assert ct == {"tests": 31, "passed": 31, "failed": 0, "test_time_s": 12.34}
    assert tc.parse_ctest("90% tests passed, 3 tests failed out of 30")["failed"] == 3
    cb = tc.parse_cocotb("PASS  a  TESTS=4 PASS=4 FAIL=0\nPASS  b  TESTS=3 PASS=3 FAIL=0\n\n2/2 cocotb targets passed")
    assert cb == {"targets": 2, "targets_passed": 2, "tests": 7, "tests_failed": 0}
    mx = tc.parse_matrix("Requirements : 130   skills: 14\n  VERIFIED    : 20\n  BROKEN      : 0\n")
    assert mx["verified"] == 20 and mx["total"] == 130 and mx["verified_pct"] == 15.4


if __name__ == "__main__":
    test_test_parsers()
    test_size_parsing_and_models()
    test_size_budget_and_growth()
    test_nextpnr_routed_fmax_is_the_last_line()
    test_ledger_append_read_delta()
    print("[PASS] trac")
