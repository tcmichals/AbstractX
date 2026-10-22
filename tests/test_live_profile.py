#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Live profiler / timeline state ([SPEC-STUDIO-11], [SPEC-STUDIO-17]) without a GUI."""
# @verifies [SPEC-STUDIO-11] tools/visualizer/SPECIFICATION.md
# @verifies [SPEC-STUDIO-17] tools/visualizer/SPECIFICATION.md
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "visualizer"))
import live_profile as lp  # noqa: E402

ROOT = "/repo"
TABLE = {0x100: ("mp_execute_bytecode", "/repo/third_party/micropython/py/vm.c:220"),
         0x104: ("mp_execute_bytecode", "/repo/third_party/micropython/py/vm.c:221 (discriminator 3)"),
         0x200: ("platform_idle_wait", "/repo/targets/allwinner_e906/src/platform.cpp:269"),
         0x300: ("??", "??:0")}


def fake_runner(pcs):
    return [TABLE[pc] for pc in pcs]


def test_paths_and_locations():
    assert lp.normalize_path("/repo/src/../src/runtime.cpp", ROOT) == "src/runtime.cpp"
    assert lp.normalize_path("??", ROOT) == ""
    assert lp.parse_location("/a/b.c:12 (discriminator 2)") == ("/a/b.c", 12)
    assert lp.parse_location("??:?") == ("", 0)


def test_symbolizer_resolves_in_background_and_caches():
    s = lp.Symbolizer("unused.elf", ROOT, runner=fake_runner)
    assert s.lookup(0x100) is None          # queued
    assert s.wait_idle()
    assert s.lookup(0x100) == ("mp_execute_bytecode", "third_party/micropython/py/vm.c", 220)
    s.lookup(0x300)
    s.wait_idle()
    assert s.lookup(0x300) == ("0x00000300", "", 0)  # unknown PC keeps its address


def test_profile_window_aggregates_lines_functions_files_owners():
    s = lp.Symbolizer("unused.elf", ROOT, runner=fake_runner)
    w = lp.ProfileWindow(window_s=5.0)
    frame = 0x3FFE9000
    w.add({"sample_hz": 1000, "dropped": 2,
           "samples": [(0x100, frame)] * 6 + [(0x104, frame)] * 2 + [(0x200, lp.OWNER_IDLE)] * 2}, now=100.0)
    w.add({"sample_hz": 1000, "dropped": 2, "samples": [(0x200, lp.OWNER_IDLE)] * 5}, now=90.0)  # too old
    w.aggregate(s, now=100.5)
    s.wait_idle()
    a = w.aggregate(s, {frame: "repl_rx"}, now=100.5)
    assert a["total"] == 10 and a["unresolved"] == 0
    assert a["owners"] == {"repl_rx": 8, "idle": 2}
    vm = a["lines"][("third_party/micropython/py/vm.c", 220)]
    assert vm["n"] == 6 and vm["owners"]["repl_rx"] == 6
    assert a["funcs"][0][:2] == ("mp_execute_bytecode", 8)
    assert a["files"]["targets/allwinner_e906/src/platform.cpp"] == 2
    assert w.dropped == 2 and w.sample_hz == 1000


def test_timeline_window():
    t = lp.TimelineWindow()
    t.add({"cycle_hz": 200_000_000, "cycles_now": 10_000, "dropped": 0,
           "spans": [{"start": 1000, "duration": 500, "id": 7, "kind": 2},
                     {"start": 9000, "duration": 100, "id": 0x3FFE9000, "kind": 1}]})
    assert [s[2] for s in t.window(2000)] == [0x3FFE9000]
    assert len(t.window(9500)) == 2 and t.cycle_hz == 200_000_000


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("PASS", name)
