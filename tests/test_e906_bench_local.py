#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Off-board checks of the benchmark collector: report parsing, stats, units ([SPEC-E906-BENCH-02/04])."""
# @verifies [SPEC-E906-BENCH-02] [SPEC-E906-BENCH-04] apps/e906_bench/SPECIFICATION.md

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests" / "board"))
sys.path.insert(0, str(ROOT / "tools"))

import bench_e906_runtime as bench  # noqa: E402


def test_report_parsing_across_messages():
    msgs = [b"platform=freertos\nyield_runs=10000\nyield_cycles_min=410\n",
            b"softirq=no-interrupt\ndropped_resumes=0\nend=1\n"]
    rep = bench.parse_report(msgs)
    assert rep["platform"] == "freertos" and rep["yield_cycles_min"] == 410
    assert rep["softirq"] == "no-interrupt" and bench.report_complete(msgs)
    assert not bench.report_complete(msgs[:1])


def test_units_and_percentiles():
    assert bench.cycles_to_ns({"yield_cycles_avg": 200, "rx_msgs": 5}) == {"yield_ns_avg": 1000.0}
    s = bench.summarize_us([0.001, 0.002, 0.003, 0.004])
    assert s["min_us"] == 1000.0 and s["max_us"] == 4000.0 and s["n"] == 4


def test_protocol_constants_match_firmware():
    src = (ROOT / "apps" / "e906_bench" / "bench.hpp").read_text()
    assert f"ENDPOINT_ADDR = {bench.BENCH['addr']}" in src
    assert f'SERVICE_NAME = "{bench.BENCH["name"]}"' in src
    cpp = (ROOT / "apps" / "e906_bench" / "bench.cpp").read_text()
    assert int(re.search(r"kPayload = (\d+)", cpp).group(1)) == max(bench.SIZES) == 496
    for key in ("yield_cycles", "softirq_isr_cycles", "softirq_wake_cycles", "dropped_resumes",
                "coro_pool_high_water", "rx_dropped", '"end"'):
        assert key in cpp, key


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
    print("[PASS] e906 bench collector (off-board)")
