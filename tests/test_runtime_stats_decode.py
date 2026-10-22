# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runtime status decoder ([SPEC-TRACE-08], [SPEC-STUDIO-15])."""
# @verifies [SPEC-TRACE-08] docs/DESIGN_SPECIFICATION.md
# @verifies [SPEC-STUDIO-15] tools/visualizer/SPECIFICATION.md
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "visualizer"))
import runtime_stats as rs  # noqa: E402


def report(ts, cycles, idle, task_run, irq_events, irq_cycles, errors=0):
    return {"timestamp_us": ts, "cycle_hz": 200_000_000, "cycles": cycles, "idle_cycles": idle,
            "steps": ts // 1000, "pool_used": 512, "pool_high_water": 1024, "pool_capacity": 8192,
            "dropped_resumes": 0, "queue_depth": 0, "queue_capacity": 32, "trace_discarded": 0,
            "tasks": [{"name": "repl_rx", "frame": 0x3FFC1000, "resumes": ts // 100,
                       "run_cycles": task_run, "max_cycles": 2000}],
            "drivers": [{"name": "uart3", "id": 90, "events": irq_events, "cycles": irq_cycles,
                         "errors": errors}]}


class RuntimeStatsDecode(unittest.TestCase):
    def test_round_trip(self):
        r = report(1_000_000, 5, 6, 7, 8, 9)
        d = rs.decode(rs.encode(r))
        self.assertEqual(d["tasks"][0]["name"], "repl_rx")
        self.assertEqual(d["drivers"][0]["id"], 90)
        self.assertEqual(d["pool_capacity"], 8192)

    def test_rates_across_u32_wrap(self):
        # 1 s at 200 MHz with the cycle counter wrapping in between.
        a = report(1_000_000, 2**32 - 100_000_000, 2**32 - 50_000_000, 0, 1000, 0)
        b = report(2_000_000, 100_000_000, 100_000_000, 40_000_000, 1100, 2_000_000)
        r = rs.rates(rs.decode(rs.encode(a)), rs.decode(rs.encode(b)))
        self.assertAlmostEqual(r["idle_pct"], 75.0)
        self.assertAlmostEqual(r["busy_pct"], 25.0)
        self.assertAlmostEqual(r["tasks"][0]["cpu_pct"], 20.0)
        self.assertAlmostEqual(r["drivers"][0]["irq_per_s"], 100.0)
        self.assertAlmostEqual(r["drivers"][0]["isr_pct"], 1.0)
        self.assertAlmostEqual(r["tasks"][0]["max_us"], 10.0)

    def test_rejects_other_streams_and_truncation(self):
        pkt = rs.encode(report(1, 1, 1, 1, 1, 1))
        other = pkt[:4] + bytes([0]) + pkt[5:]
        self.assertIsNone(rs.decode(other))
        self.assertIsNone(rs.decode(pkt[:-1]))
        self.assertIsNone(rs.decode(b"\x10" * 64))   # a TLP is not a CTF packet

    def test_packet_from_the_cpp_side(self):
        exe = Path(os.environ.get("RUNTIME_STATS_EXE", ROOT / "build_host" / "sim" / "test_runtime_stats"))
        if not exe.exists():
            self.skipTest("build_host/sim/test_runtime_stats not built")
        with tempfile.TemporaryDirectory() as td:
            out = os.path.join(td, "status.bin")
            subprocess.run([str(exe)], check=True, capture_output=True,
                           env={**os.environ, "RUNTIME_STATS_DUMP": out})
            d = rs.decode(Path(out).read_bytes())
        self.assertIsNotNone(d, "C++ packet must decode with the Python layout")
        self.assertEqual(d["cycle_hz"], 1_000_000)
        self.assertEqual(d["idle_cycles"], 80)
        runs = {t["name"]: t["run_cycles"] for t in d["tasks"]}
        self.assertEqual(runs["outer"], 60)
        self.assertEqual(runs["inner"], 180)


if __name__ == "__main__":
    unittest.main()
