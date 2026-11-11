#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/run_e906_bench_suite.py without a board: the rows, the commands, cyclictest parsing, that
a failed row is recorded and the run goes on, and the board lock ([SPEC-AUDIT-13]) is taken first,
refused when held, released on every exit and named in the manifest."""
# @verifies [SPEC-E906-BENCH-08] [SPEC-E906-BENCH-09] apps/e906_bench/SPECIFICATION.md
# @verifies [SPEC-AUDIT-13] docs/SPECTRACE.md (board lock: run_locked, make_manifest, manifest_entry)

import contextlib
import io
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import run_e906_bench_suite as r  # noqa: E402
sys.path.insert(0, str(ROOT / "tests"))
from test_board_lock import local_board  # noqa: E402

# The format of cyclictest on the A5E (-m -p 95 -i 1000 -l 1000 -t1 -a 2 -q -h 20000, 2026-11-07), with
# counts edited so they add up to 1000 and one overflow added, which the real run did not have.
CYCLICTEST = """# /dev/cpu_dma_latency set to 0us
# Histogram
000015 000008
000016 000017
000017 000081
000018 000446
000019 000372
000020 000049
000021 000021
000022 000003
000025 000001
000028 000001
# Total: 000000999
# Min Latencies: 00015
# Avg Latencies: 00018
# Max Latencies: 00028
# Histogram Overflows: 00001
# Histogram Overflow at cycle number:
# Thread 0: 00512
"""


def test_every_article_row_is_planned():
    names = {x.name for x in r.plan(60000)}
    for tag in ("idle", "load"):
        for fw in ("abstractx", "freertos_wait"):
            for kind in ("bench", "cyclic", "oneway_c_rt", "oneway_c_other", "oneway_py_rt", "oneway_py_other"):
                assert f"{fw}_{kind}_{tag}" in names, (fw, kind, tag)
        assert f"freertos_wait_native_cyclic_{tag}" in names
        assert f"abstractx_native_cyclic_{tag}" not in names, "F exists only on FreeRTOS"
        assert f"freertos_xtimer_cyclic_{tag}" in names and f"freertos_tick_hook_cyclic_{tag}" in names
        assert f"linux_cyclictest_rt_{tag}" in names and f"linux_cyclictest_other_{tag}" in names
        assert f"freertos_debug_cyclic_{tag}" in names and f"freertos_debug_bench_{tag}" in names
    assert len(names) == len(r.plan(60000)), "row names are unique"


def config_macros(debug: int) -> dict:
    """FreeRTOSConfig.h through the real preprocessor."""
    out = subprocess.run(["cc", "-E", "-dM", f"-DABX_FREERTOS_DEBUG={debug}", "-x", "c",
                          str(ROOT / "apps/e906_freertos_app/FreeRTOSConfig.h")],
                         check=True, capture_output=True, text=True).stdout
    macros = {}
    for line in out.splitlines():
        parts = line.split(None, 2)
        macros[parts[1].split("(")[0]] = parts[2] if len(parts) > 2 else ""
    return macros


def test_debug_option_gates_every_debug_cost():
    on, off = config_macros(1), config_macros(0)
    for name in ("configASSERT", "traceTASK_SWITCHED_IN", "traceTASK_SWITCHED_OUT"):
        assert name in on and name not in off, name
    assert "2" in on["configCHECK_FOR_STACK_OVERFLOW"]
    assert "ABX_FREERTOS_DEBUG" in off["configCHECK_FOR_STACK_OVERFLOW"]  # ( 0 ? 2 : 0 )
    assert on["configUSE_PORT_OPTIMISED_TASK_SELECTION"] == off["configUSE_PORT_OPTIMISED_TASK_SELECTION"] == "1"
    main = (ROOT / "apps/e906_freertos_app/src/main.cpp").read_text()
    idle = main[main.index("void vApplicationIdleHook"):main.index("void vApplicationStackOverflowHook")]
    assert idle.count('"wfi"') == 2, "both builds still sleep in the idle hook"
    assert "#else" in main[main.index("bool abx_bench_idle_stats"):main.index("void vApplicationIdleHook")]


def test_step_stamps_every_phase_in_order_and_the_report_names_them():
    src = (ROOT / "src/runtime.cpp").read_text()
    body = src[src.index("uint64_t step() noexcept {"):src.index("void StepAwaiter::await_suspend")]
    marks = re.findall(r"stats::step_mark\(stats::StepPhase::(\w+)\)", body)
    assert marks == ["Timer", "Dispatch", "Io", "Network", "Flush", "Tasks", "Deadline"], marks
    assert body.index("stats::step_begin()") < body.index("service_due_us")
    # the last mark comes before the early return for ready work, so every pass is fully counted
    assert body.index("StepPhase::Deadline") < body.index("return kNow")
    bench = (ROOT / "apps/e906_bench/bench.cpp").read_text()
    for phase in marks:
        assert f'"step_{phase.lower()}_cycles", prof.total(StepPhase::{phase})' in bench, phase
    assert '#if ABX_STEP_PROFILE' in bench and '"step_stamp_cycles"' in bench


def test_row_commands():
    rows = {x.name: x for x in r.plan(5000)}
    assert "--cyclic 5000" in rows["abstractx_cyclic_idle"].command
    assert "--native-cyclic 5000" in rows["freertos_wait_native_cyclic_idle"].command
    c_rt = rows["abstractx_oneway_c_rt_idle"].command
    assert "--oneway 5000 --reader c --rt" in c_rt and "abstractx_oneway_c_rt_idle.json" in c_rt
    py = rows["freertos_wait_oneway_py_other_idle"].command
    assert "--reader python" in py and "--rt" not in py
    rt, other = rows["linux_cyclictest_rt_idle"], rows["linux_cyclictest_other_idle"]
    assert "-p 95" in rt.command and "-p" not in other.command.replace("-m", "") and rt.stdout and other.stdout
    assert "-a 2" in rt.command and f"-h {r.HIST_US}" in rt.command
    assert not rt.firmware, "the Linux rows load nothing"


def test_load_wraps_only_load_rows_and_keeps_the_exit_code():
    rows = {x.name: x for x in r.plan(5000)}
    assert "stress-ng" not in rows["abstractx_cyclic_idle"].command
    loaded = rows["abstractx_cyclic_load"].command
    assert loaded.startswith(r.STRESS) and "rc=$?" in loaded and loaded.endswith("exit $rc") and "kill $S" in loaded
    # the wrapper really returns the row's exit code, not the kill's
    wrapped = r.with_load("false", 1).replace(r.STRESS + " --timeout 61s -q", "sleep 30").replace("sleep 5", "sleep 0")
    assert subprocess.run(["sh", "-c", wrapped]).returncode == 1
    wrapped = r.with_load("true", 1).replace(r.STRESS + " --timeout 61s -q", "sleep 30").replace("sleep 5", "sleep 0")
    assert subprocess.run(["sh", "-c", wrapped]).returncode == 0


def test_only_and_runs():
    assert {x.name for x in r.plan(1000, only="cyclictest_rt_idle")} == {"linux_cyclictest_rt_idle"}
    names = [x.name for x in r.plan(1000, runs=2, only="cyclictest_rt_idle")]
    assert names == ["linux_cyclictest_rt_idle_run1", "linux_cyclictest_rt_idle_run2"]


def test_cyclictest_parse():
    p = r.parse_cyclictest(CYCLICTEST, 1000)
    assert "error" not in p, p
    assert (p["n"], p["min_us"], p["avg_us"], p["max_us"], p["overflows"]) == (1000, 15, 18, 28, 1), p
    # 8 + 17 + 81 + 446 = 552 at bucket 18 reaches half; upper edge 19
    assert p["p50_us"] == 19 and p["p99_us"] == 22 and p["p999_us"] == 29, p
    assert "error" in r.parse_cyclictest(CYCLICTEST, 2000), "wrong sample count"
    assert "error" in r.parse_cyclictest("", 1000), "no output"


def test_firmware_modes_build_apart():
    by = {f.name: f for f in r.FIRMWARES}
    assert by["abstractx"].build_dir.name == by["freertos_debug"].build_dir.name == "build_e906"
    assert by["freertos_xtimer"].build_dir.name == "build_e906_timer_xtimer"
    cmds = [" ".join(c) for c in r.build_commands()]
    assert any("-DABX_FREERTOS_TIMER=TICK_HOOK" in c for c in cmds)
    # [SPEC-E906-BENCH-06] every FreeRTOS timing build carries no debug cost; only freertos_debug does
    for name in ("freertos_wait", "freertos_xtimer", "freertos_tick_hook"):
        assert "-DABX_FREERTOS_DEBUG=OFF" in by[name].cmake_args, name
    assert by["freertos_debug"].cmake_args == [] and by["abstractx"].cmake_args == []
    assert by["freertos_debug"].app == "e906_freertos_app"
    assert sum("-DABX_FREERTOS_DEBUG=OFF" in c for c in cmds) == 4, cmds
    # [SPEC-E906-BENCH-09] the stamps cost cycles: only the two profile rows carry them, in their own builds
    profiled = [f for f in r.FIRMWARES if "-DABX_STEP_PROFILE=ON" in f.cmake_args]
    assert sorted(f.name for f in profiled) == ["abstractx_step_profile", "freertos_step_profile"]
    assert all(f.rows == ("bench",) for f in profiled)
    assert len({f.build_dir for f in r.FIRMWARES if f.step_profile}) == 2
    assert not {f.build_dir for f in profiled} & {f.build_dir for f in r.FIRMWARES if not f.step_profile}
    cmake = (ROOT / "apps/e906_freertos_app/CMakeLists.txt").read_text()
    for f in r.FIRMWARES:
        assert not f.mode or f'"{f.mode}"' in cmake, f.mode
    for path in r.TOOLS + [r.READER_SRC]:
        assert (ROOT / path).is_file(), path


class Proc:
    def __init__(self, rc=0, stdout=""):
        self.returncode, self.stdout = rc, stdout


def fake_board(fail_row="", fail_load="", no_json="", temps=None):
    """Stands in for subprocess.run: fails the named row, or the firmware load, or the JSON fetch.
    `temps` is the list of hottest-zone readings the board gives, one per read (45 C when it runs out)."""
    calls = []
    temps = list(temps or [])

    def run(cmd, **kw):
        calls.append(cmd)
        text = " ".join(cmd)
        if text.endswith(r.TEMPS):
            return Proc(0, f"{temps.pop(0) if temps else 45000}\n44000\n")
        if fail_load and f"echo {fail_load}.elf" in text:
            return Proc(1)
        if cmd[-1].startswith(f"cat {r.REMOTE_RESULTS}/"):  # the JSON fetch over ssh
            if no_json and no_json in text:
                return Proc(1)
            kw["stdout"].write(b'{"ok": 1}')
            return Proc()
        if fail_row and fail_row in text:
            return Proc(1)
        return Proc(0, CYCLICTEST)
    return run, calls


def drive(**knobs):
    rows = r.plan(1000, only="") + []
    rows = [x for x in rows if x.name in (
        "abstractx_cyclic_idle", "abstractx_oneway_c_rt_idle", "freertos_wait_cyclic_idle", "linux_cyclictest_rt_idle")]
    run, calls = fake_board(**knobs)
    with tempfile.TemporaryDirectory() as d, contextlib.redirect_stdout(io.StringIO()):
        results = r.run_rows(rows, r.FIRMWARES, "10.0.0.9", Path(d), 1000, run=run)
        files = sorted(os.listdir(d))
    return results, calls, files


def test_hot_board_stops_the_run_and_keeps_every_row():
    # 45 C before row 1, 86 C after it: row 1 is kept, the other three are not run
    results, calls, files = drive(temps=[45000, 86000])
    assert [x["rc"] for x in results] == [0, 2, 2, 2], results
    assert results[0]["temp_mC_after"] == 86000 and "thermal" in results[0]["thermal_stop"]
    assert all("thermal" in x["error"] for x in results[1:]) and len(files) == 1
    assert sum("echo start" in " ".join(c) for c in calls) == 1, "nothing loaded once hot"
    results, _, _ = drive(temps=[90000])
    assert [x["rc"] for x in results] == [2, 2, 2, 2], "too hot before the first row: nothing runs"
    assert r.hottest_mc("47000\n85500\n") == 85500 and r.hottest_mc("") is None


def test_push_checks_md5_on_the_board():
    with tempfile.TemporaryDirectory() as d:
        src = Path(d) / "fw.elf"
        src.write_bytes(b"firmware")
        good = __import__("hashlib").md5(b"firmware").hexdigest()
        for answer, ok in ((good, True), ("0" * 32, False)):
            def run(cmd, answer=answer, **kw):
                if cmd[-1].startswith("md5sum"):
                    return Proc(0, f"{answer}  /lib/firmware/fw.elf\n")
                assert cmd[-1] == "cat > /lib/firmware/fw.elf" and kw["stdin"].read() == b"firmware"
                return Proc()
            assert r.push("root@10.0.0.9", src, "/lib/firmware/fw.elf", run) is ok


def test_clean_run_loads_each_firmware_once():
    results, calls, files = drive()
    assert [x["rc"] for x in results] == [0, 0, 0, 0], results
    loads = [c for c in calls if "echo start" in " ".join(c)]
    assert len(loads) == 2, "abstractx once, freertos_wait once; Linux rows load nothing"
    assert len(files) == 4, files
    assert json.loads(json.dumps(results))  # the manifest takes this as is


def test_failed_row_is_recorded_and_the_run_goes_on():
    results, _, _ = drive(fail_row="--oneway")
    by = {x["row"]: x for x in results}
    assert by["abstractx_oneway_c_rt_idle"]["rc"] == 1
    assert by["freertos_wait_cyclic_idle"]["rc"] == 0 and by["linux_cyclictest_rt_idle"]["rc"] == 0
    assert len(results) == 4, "never left out"


def test_firmware_that_will_not_start_fails_its_rows_only():
    results, _, _ = drive(fail_load="abstractx")
    by = {x["row"]: x for x in results}
    assert by["abstractx_cyclic_idle"]["rc"] == 2 and by["abstractx_oneway_c_rt_idle"]["rc"] == 2
    assert "did not start" in by["abstractx_cyclic_idle"]["error"]
    assert by["freertos_wait_cyclic_idle"]["rc"] == 0


def test_missing_json_fails_the_row_even_if_the_command_passed():
    results, _, _ = drive(no_json="abstractx_cyclic_idle")
    by = {x["row"]: x for x in results}
    assert by["abstractx_cyclic_idle"]["rc"] == 1 and "no JSON" in by["abstractx_cyclic_idle"]["error"]


def test_row_that_hangs_is_recorded_and_the_firmware_is_loaded_again():
    run, calls = fake_board()

    def hangs(cmd, **kw):
        if "--oneway" in " ".join(cmd):
            raise subprocess.TimeoutExpired(cmd, 1)
        return run(cmd, **kw)
    rows = [x for x in r.plan(1000) if x.name in ("abstractx_oneway_c_rt_idle", "abstractx_cyclic_idle",
                                                   "linux_cyclictest_rt_idle")]
    rows.sort(key=lambda x: "oneway" not in x.name)  # the hang first, then the same firmware again
    with tempfile.TemporaryDirectory() as d, contextlib.redirect_stdout(io.StringIO()):
        results = r.run_rows(rows, r.FIRMWARES, "10.0.0.9", Path(d), 1000, run=hangs)
    by = {x["row"]: x for x in results}
    assert by["abstractx_oneway_c_rt_idle"]["rc"] == 1 and by["abstractx_oneway_c_rt_idle"]["error"] == "timed out"
    assert by["abstractx_cyclic_idle"]["rc"] == 0 and by["linux_cyclictest_rt_idle"]["rc"] == 0
    assert sum("echo start" in " ".join(c) for c in calls) == 2, "loaded again after the hang"


def test_record():
    m = {"commit": "abc", "dirty": False, "kernel": "7.1.0", "rows": [{"rc": 0}, {"rc": 1}]}
    rec = r.make_record(m, "2026-11-07T12:00:00Z")
    assert rec["source"] == "board" and rec["result"] == "fail" and rec["metrics"]["failed"] == 1
    m["rows"] = [{"rc": 0}]
    assert r.make_record(m, "t")["result"] == "pass"


def test_dry_run_and_no_board():
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        assert r.main(["--ip", "10.0.0.9", "--dry-run", "--only", "cyclictest_rt_idle"]) == 0
    assert "linux_cyclictest_rt_idle" in out.getvalue() and out.getvalue().startswith("# 1 rows")
    os.environ.pop("ABSTRACTX_BOARD_IP", None)
    with contextlib.redirect_stdout(io.StringIO()):
        assert r.main([]) == 2, "no board -> could not run"
        assert r.main(["--ip", "10.0.0.9", "--only", "nothing_matches"]) == 2


def test_board_lock_first_and_released_when_the_run_stops():
    with tempfile.TemporaryDirectory() as d:
        lock = str(Path(d) / "abstractx-board.lock")
        calls, order = [], []
        no_rproc = lambda cmd, **kw: subprocess.CompletedProcess(cmd, 1, stdout="", stderr="")  # noqa: E731
        with patch.object(r.board_lock, "LOCK_PATH", lock), patch.object(
                r.board_target, "require_a5e", side_effect=lambda ip: order.append(len(calls))), \
                patch.object(r.subprocess, "run", side_effect=local_board(lock, other=no_rproc, calls=calls)), \
                contextlib.redirect_stdout(io.StringIO()):
            assert r.main(["--ip", "10.0.0.9", "--buildroot-out", d, "--only", "cyclictest_rt_idle"]) == 2
        assert lock in calls[0][-1] and "mkdir" in calls[0][-1], "the lock is the first thing on the board"
        assert order == [1], "the model check runs after the lock"
        assert "RELEASED" in calls[-1][-1] and not os.path.exists(lock), "released when the run stops early"


def test_held_board_lock_stops_the_run():
    with tempfile.TemporaryDirectory() as d:
        lock = str(Path(d) / "abstractx-board.lock")
        os.mkdir(lock)
        (Path(lock) / "owner").write_text('{"tool": "run_e906_board_suite", "user": "x", "host": "pc2", "pid": 7}\n')
        calls, out = [], io.StringIO()
        with patch.object(r.board_lock, "LOCK_PATH", lock), patch.object(r.board_target, "require_a5e") as model, \
                patch.object(r.subprocess, "run", side_effect=local_board(lock, calls=calls)), \
                contextlib.redirect_stdout(out):
            assert r.main(["--ip", "10.0.0.9", "--buildroot-out", d, "--only", "cyclictest_rt_idle"]) == 2
        model.assert_not_called()
        assert len(calls) == 1 and "in use" in out.getvalue() and "run_e906_board_suite" in out.getvalue()
        assert os.path.isdir(lock), "the holder's lock is untouched"


def test_manifest_names_the_lock_holder():
    info = {"path": "/run/abstractx-board.lock", "host": "pc", "user": "u", "pid": 1, "tool": "run_e906_bench_suite",
            "acquired": "2026-11-11T12:00:00Z", "start": "2026-11-11T12:00:00Z", "start_epoch": 1, "token": "t"}
    m = r.make_manifest("2026-11-11T12:00:00Z", "Linux 7.1", {}, 1000, {}, [], info)
    assert m["board_lock"] == {"path": "/run/abstractx-board.lock", "host": "pc", "user": "u", "pid": 1,
                               "tool": "run_e906_bench_suite", "acquired": "2026-11-11T12:00:00Z"}


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
    print("[PASS] run_e906_bench_suite")
