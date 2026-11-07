#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Off-board checks of the one-way readers ([SPEC-E906-BENCH-07]): the C and the Python reader run
against a fake firmware on a socket, including the failure cases. The real firmware and the real
clocks need the board."""
# @verifies [SPEC-E906-BENCH-07] apps/e906_bench/SPECIFICATION.md

import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = next(p for p in Path(__file__).resolve().parents if (p / ".git").exists())
BOARD = ROOT / "apps" / "e906_repl_app" / "tests" / "board"
sys.path.insert(0, str(BOARD))
sys.path.insert(0, str(ROOT / "tools"))

import bench_e906_runtime as bench  # noqa: E402

LATE_TICKS = 1000  # fake firmware: deadline this far before `sent`, so event - sent = 41,666 ns
CPU = sorted(os.sched_getaffinity(0))[0]
_BIN = None


def reader_bin() -> str:
    global _BIN
    if _BIN is None:
        out = tempfile.mkdtemp(prefix="oneway_reader_")
        _BIN = os.path.join(out, "e906_oneway_reader")
        cc = shutil.which("cc") or shutil.which("gcc")
        subprocess.run([cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-o", _BIN,
                        str(BOARD / "e906_oneway_reader.c")], check=True)
    return _BIN


def now_ticks() -> int:
    return bench.python_ticks(0)


class FakeFirmware(threading.Thread):
    """The firmware side of 'T' and 'O' with knobs for each thing that can go wrong."""

    def __init__(self, sock, offset=0, lose=0, future=0, no_report=False, wild_probe=False, shared_then_moved=False,
                 miscount=0, dropped=0, slow_even_probes=False):
        super().__init__(daemon=True)
        self.__dict__.update(sock=sock, offset=offset, lose=lose, future=future, no_report=no_report,
                             wild_probe=wild_probe, moved=shared_then_moved, miscount=miscount, dropped=dropped,
                             slow=slow_even_probes)
        self.probes = 0
        self.run_done = False

    def run(self):
        try:
            while True:
                req = self.sock.recv(512)
                if not req:
                    return
                if req[:1] == b"T":
                    self.probes += 1
                    e = now_ticks() + self.offset
                    if self.wild_probe and self.probes % 2:
                        e += 24_000_000
                    if self.moved and self.run_done:
                        e += 24_000_000
                    if self.slow and self.probes % 2 == 0:
                        time.sleep(0.002)  # stamped, then a slow reply: t1 is late for this probe only
                    self.sock.send(b"T" + e.to_bytes(8, "little"))
                elif req[:1] == b"O":
                    self.do_run(int.from_bytes(req[1:5], "little"), int.from_bytes(req[6:8], "little"))
        except OSError:
            return

    def do_run(self, count, size):
        sent = 0
        for seq in range(count):
            time.sleep(0.0002)
            if seq < self.lose:
                sent += 1  # counted as sent, never arrives
                continue
            stamp = now_ticks() + self.offset + self.future
            msg = bench.SAMPLE.pack(bench.SAMPLE_TAG, seq, stamp - LATE_TICKS, stamp - 10, stamp)
            self.sock.send(msg + b"\0" * (size - len(msg)))
            sent += 1
        self.run_done = True
        if self.no_report:
            return
        sent += self.miscount
        self.sock.send((f"platform=fake\now_count={count}\now_sent={sent}\now_tx_fail=0\now_missed=0\n").encode())
        self.sock.send(f"ow_late_ns_min=1\ndropped_resumes={self.dropped}\nend=1\n".encode())


def run_c(fd, count, timeout_s):
    proc = subprocess.run([reader_bin(), "--fd", str(fd), "--count", str(count), "--pings", "200", "--cpu", str(CPU),
                           "--timeout-s", str(timeout_s)], pass_fds=[fd], capture_output=True, text=True, timeout=60)
    assert proc.returncode in (0, 1), proc.stderr
    rep = json.loads(proc.stdout)
    assert (proc.returncode == 1) == ("error" in rep), rep
    return rep


def run_python(fd, count, timeout_s):
    saved = os.sched_getaffinity(0)
    try:
        return bench.run_oneway_python(fd, count, cpu=CPU, pings=200, timeout_s=timeout_s)
    finally:
        os.sched_setaffinity(0, saved)


READERS = {"c": run_c, "python": run_python}


def both(count=100, timeout_s=20, **knobs):
    """Runs each reader against its own fake firmware and returns {reader: report}."""
    out = {}
    for name, fn in READERS.items():
        ours, theirs = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        FakeFirmware(theirs, **knobs).start()
        try:
            out[name] = fn(ours.fileno(), count, timeout_s)
        finally:
            ours.close()
    return out


def test_same_clock_is_shared_and_passes():
    for name, r in both().items():
        assert "error" not in r, (name, r)
        assert r["clock"]["kind"] == "shared" and r["clock"]["offset_ticks"] == 0, (name, r["clock"])
        assert r["reader"] == name and r["received"] == 100 and r["lost"] == 0, (name, r)
        assert r["sent_to_read"]["n"] == r["event_to_read"]["n"] == 100
        # the fake stamps the deadline LATE_TICKS before `sent`, so the difference is known
        diff = r["event_to_read"]["avg_ns"] - r["sent_to_read"]["avg_ns"]
        assert abs(diff - LATE_TICKS * 1000 // 24) <= 50, (name, diff)
        assert r["report"]["platform"] == "fake" and r["report"]["end"] == 1
        h = r["sent_to_read"]
        assert h["min_ns"] <= h["avg_ns"] <= h["max_ns"] and h["p50_us"] <= h["p99_us"] <= h["p999_us"]


def test_reports_have_the_same_keys():
    r = both(count=20)
    keys = lambda d: set(d) - {"clock_pair_ticks"}  # noqa: E731
    assert keys(r["c"]) == keys(r["python"]), (keys(r["c"]) ^ keys(r["python"]))
    assert set(r["c"]["sent_to_read"]) == set(r["python"]["sent_to_read"])
    assert set(r["c"]["clock"]) == set(r["python"]["clock"])


def test_different_clock_is_calibrated_with_a_real_uncertainty():
    offset = 10 * 24_000_000
    for name, r in both(offset=offset).items():
        assert "error" not in r, (name, r)
        c = r["clock"]
        assert c["kind"] == "calibrated", (name, c)
        # the offset is removed: not 10 s, and within the uncertainty the run reports
        assert abs(c["offset_ticks"] - offset) * 1000 // 24 <= c["uncertainty_ns"], (name, c)
        assert 0 < c["uncertainty_ns"] < 1_000_000_000, (name, c)
        assert r["sent_to_read"]["avg_ns"] < 1_000_000_000, (name, r["sent_to_read"])


def test_slow_probes_do_not_widen_the_bounds():
    # Both bounds come from the best probes: a slow reply must not move them, or the verdict and the
    # uncertainty would depend on the worst probe. The last probe of each pass is a slow one.
    for name, r in both(count=20, slow_even_probes=True).items():
        for b in (r["clock"]["before"], r["clock"]["after"]):
            assert -b["min_rtt"] <= b["lo"] <= 0 <= b["hi"] <= b["min_rtt"], (name, b)
        assert r["clock"]["kind"] == "shared", (name, r["clock"])


def test_clock_that_is_not_a_fixed_offset_stops_the_run():
    for name, r in both(wild_probe=True).items():
        assert r["clock"]["kind"] == "inconsistent" and "bounds cross" in r["error"], (name, r)
        assert r["received"] == 0, (name, r)


def test_shared_verdict_contradicted_after_the_run_fails():
    for name, r in both(shared_then_moved=True).items():
        assert "contradicts" in r.get("error", ""), (name, r)


def test_stamp_from_the_future_is_negative_and_fails():
    for name, r in both(future=24_000_000).items():
        assert r["negative"] > 0 and "negative" in r["error"], (name, r)


def test_lost_samples_fail_and_are_counted():
    for name, r in both(lose=3).items():
        assert r["lost"] == 3 and r["received"] == 97 and "lost" in r["error"], (name, r)


def test_counts_that_do_not_add_up_fail():
    for name, r in both(miscount=1).items():
        assert "add up" in r["error"], (name, r)


def test_dropped_resumes_fail():
    for name, r in both(dropped=2).items():
        assert "dropped" in r["error"], (name, r)


def test_missing_report_times_out_and_does_not_hang():
    for name, r in both(no_report=True, timeout_s=2).items():
        assert "timed out" in r["error"], (name, r)
        assert r["received"] == 100, (name, r)  # what arrived is still reported


def test_percentiles_match_between_readers():
    # one known distribution through the Python stats; the C buckets use the same formula
    values = [i * bench.TICKS_PER_US for i in range(1000)]  # 0..999 us
    s = bench.hist_stats(values)
    assert s["p50_us"] == 500 and s["p99_us"] == 990 and s["p999_us"] == 999 and s["over_20ms"] == 0
    assert bench.hist_stats([])["n"] == 0
    assert bench.hist_stats([20000 * bench.TICKS_PER_US])["over_20ms"] == 1


def test_clock_bounds_formula():
    # one probe: t0 <= e - d <= t1
    b = bench.clock_bounds([(100, 150, 200)])
    assert b["lo"] == -50 and b["hi"] == 50 and b["min_rtt"] == 100
    assert bench.clock_verdict(b) == ("shared", 0)
    assert bench.clock_verdict(bench.clock_bounds([(100, 1150, 200)])) == ("calibrated", 1000)
    assert bench.clock_verdict({"lo": 5, "hi": 1})[0] == "inconsistent"


def test_wire_format_matches_firmware():
    cpp = (ROOT / "apps" / "e906_bench" / "bench.cpp").read_text()
    fields = re.search(r"struct OneWaySample \{(.*?)\};", cpp, re.S).group(1)
    names = re.findall(r"uint\d+_t (\w+)", fields)
    assert names == ["tag", "pad", "seq", "deadline", "woke", "sent"], names
    assert "sizeof(OneWaySample) == 32" in cpp and bench.SAMPLE.size == 32
    assert f"kOneWayTag = 0x{bench.SAMPLE_TAG:02x}" in cpp
    c = (BOARD / "e906_oneway_reader.c").read_text()
    assert "#define SAMPLE_LEN 32" in c and f"#define SAMPLE_TAG 0x{bench.SAMPLE_TAG:02x}" in c
    assert f"#define HIST_US {bench.HIST_US}" in c
    # every key the readers require is one the firmware sends
    for key in ("ow_sent", "ow_tx_fail", "ow_missed", "ow_count", "dropped_resumes"):
        assert f'"{key}"' in cpp, key
    assert "get_i64(buf + 24)" in c and "get_i64(buf + 8)" in c  # sent, deadline
    assert struct.calcsize("<B3xI") == 8


def test_clock_pair_prints_a_number():
    out = json.loads(subprocess.run([reader_bin(), "--clock-pair"], check=True, capture_output=True, text=True).stdout)
    assert isinstance(out["cntvct_minus_monoraw_ticks"], int) and out["width_ticks"] >= 0


if __name__ == "__main__":
    failed = 0
    for test_name, fn in list(globals().items()):
        if test_name.startswith("test_"):
            try:
                fn()
            except AssertionError as e:
                failed += 1
                print(f"[FAIL] {test_name}: {str(e)[:600]}")
    if failed:
        sys.exit(1)
    print("[PASS] e906 one-way readers (off-board)")
