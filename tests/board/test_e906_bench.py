#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""On-board check of the runtime benchmark firmware ([SPEC-E906-BENCH-*]); skips unless
e906_bench_app or e906_freertos_app is loaded. Plain python3 or pytest."""
# @verifies [SPEC-E906-BENCH-02] [SPEC-E906-BENCH-03] [SPEC-E906-BENCH-04] apps/e906_bench/SPECIFICATION.md
# @verifies [SPEC-E906-RTOS-04] [SPEC-E906-RTOS-05] apps/e906_freertos_app/SPECIFICATION.md

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from board_common import skip  # noqa: E402
import bench_e906_runtime as bench  # noqa: E402


def _link():
    if not os.path.exists("/dev/rpmsg_ctrl0"):
        skip("not an E906 board")
    sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
    from e906_rpmsg_bridge import find_node
    announced = any(open(os.path.join(d, "name")).read().strip() == bench.BENCH["name"]
                    for d in (os.path.join("/sys/class/rpmsg", e) for e in os.listdir("/sys/class/rpmsg"))
                    if os.path.exists(os.path.join(d, "name")))
    if find_node(bench.BENCH["name"]) is None and not announced:
        skip("abstractx-bench not announced (bench firmware not loaded)")
    return bench.BenchLink()


def test_bench_echo_all_sizes():
    link = _link()
    try:
        link.drain()
        for size in bench.SIZES:
            r = bench.run_echo(link, size, 200)
            assert "error" not in r and r["mismatches"] == 0, r
    finally:
        link.close()


def test_bench_suite_reports_and_no_dropped_resumes():
    link = _link()
    try:
        link.drain()
        rep = bench.run_suite(link)
    finally:
        link.close()
    assert "error" not in rep, rep
    assert rep["platform"] in ("baremetal", "freertos"), rep
    assert rep["yield_runs"] == 10000 and rep["yield_cycles_min"] > 0, rep
    assert rep["dropped_resumes"] == 0 and rep["rx_dropped"] == 0, rep
    if rep["platform"] == "freertos":
        assert rep.get("softirq_runs") == 1000, rep  # the FreeRTOS glue routes CLIC line 3
