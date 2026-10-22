#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Profiler / timeline decoders ([SPEC-TRACE-09], [SPEC-TRACE-10]) against the C++ packets."""
# @verifies [SPEC-TRACE-09] docs/DESIGN_SPECIFICATION.md
# @verifies [SPEC-TRACE-10] docs/DESIGN_SPECIFICATION.md
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "visualizer"))
import event_streams as es  # noqa: E402


def test_round_trip_and_start_unwrap():
    pkt = es.encode_profile([(0x3FFC1234, 0), (0x3FFC5678, 1)], 1000, 3)
    p = es.decode_profile(pkt)
    assert p["sample_hz"] == 1000 and p["dropped"] == 3 and p["samples"] == [(0x3FFC1234, 0), (0x3FFC5678, 1)]
    now = (5 << 32) + 100          # cycle counter just wrapped its low 32 bits
    spans = [{"start": now - 300, "duration": 50, "id": 0x3FFE0000, "kind": es.KIND_RESUME},
             {"start": now - 50, "duration": 20, "id": 23, "kind": es.KIND_ISR}]
    t = es.decode_timeline(es.encode_timeline(spans, 200_000_000, now, 7))
    assert t["cycle_hz"] == 200_000_000 and t["dropped"] == 7
    assert [s["start"] for s in t["spans"]] == [now - 300, now - 50]
    assert t["spans"][1]["kind"] == es.KIND_ISR and t["spans"][1]["id"] == 23


def test_rejects_other_streams_and_truncation():
    pkt = es.encode_profile([(1, 0)])
    assert es.decode_timeline(pkt) is None
    assert es.decode_profile(pkt[:-1]) is None
    assert es.decode_profile(b"\x00" * 64) is None


def test_decodes_the_cpp_packets():
    exe = os.environ.get("EVENT_TRACE_EXE")
    if not exe:
        print("SKIP: EVENT_TRACE_EXE not set (run through ctest)")
        return
    with tempfile.TemporaryDirectory() as d:
        tl, pr = os.path.join(d, "tl.bin"), os.path.join(d, "pr.bin")
        subprocess.run([exe], check=True, env=dict(os.environ, EVENT_TIMELINE_DUMP=tl, EVENT_PROFILE_DUMP=pr),
                       stdout=subprocess.DEVNULL)
        t = es.decode_timeline(open(tl, "rb").read())
        p = es.decode_profile(open(pr, "rb").read())
    assert t and t["cycle_hz"] == 1_000_000 and t["dropped"] == 0 and t["spans"]
    assert all(s["kind"] == es.KIND_RESUME and s["start"] <= t["cycles_now"] for s in t["spans"])
    assert p and p["sample_hz"] == 1000
    assert p["samples"][-2:] == [(0x1111, es.OWNER_IDLE), (0x2222, es.OWNER_NONE)]


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("PASS", name)
