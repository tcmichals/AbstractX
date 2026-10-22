#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Runtime status on the board ([SPEC-TRACE-08], [SPEC-E906-IRQ-05], [SPEC-E906-REPL-18]).
Reads the real 'abstractx-trace' endpoint through e906_rpmsg_bridge.py and decodes the
stream-3 reports with tools/visualizer/runtime_stats.py (copied next to this file).
Firmware without the status report sends nothing on the endpoint and these tests fail.
"""
# @verifies [SPEC-TRACE-08] docs/DESIGN_SPECIFICATION.md
# @verifies [SPEC-E906-IRQ-05] targets/allwinner_e906/SPECIFICATION.md
# @verifies [SPEC-E906-REPL-18] apps/e906_repl_app/SPECIFICATION.md
# @verifies [SPEC-E906-REPL-19] apps/e906_repl_app/SPECIFICATION.md

import os
import socket
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from board_common import skip  # noqa: E402
from e906_link import on_e906_board, repl_exec  # noqa: E402

try:
    import runtime_stats as rs  # noqa: E402
except ImportError:
    rs = None

BRIDGE = os.path.join(HERE, "e906_rpmsg_bridge.py")


def _require():
    if not on_e906_board():
        skip("not an E906 board (no remoteproc0)")
    if rs is None or not os.path.exists(BRIDGE):
        skip("copy tools/visualizer/runtime_stats.py and tools/e906_rpmsg_bridge.py next to this file")


def _collect(seconds: float, during=None):
    """Status reports received for `seconds`, optionally while `during()` runs in a thread."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    sock.settimeout(0.5)
    port = sock.getsockname()[1]
    bridge = subprocess.Popen([sys.executable, BRIDGE, "trace", "--udp-port", str(port)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    worker = None
    errors = []
    if during:
        def run():
            try:
                during()
            except Exception as e:  # noqa: BLE001 - reported by the test
                errors.append(e)
        worker = threading.Thread(target=run)
        worker.start()
    reports = []
    t_end = time.monotonic() + seconds
    try:
        while time.monotonic() < t_end:
            try:
                data, _ = sock.recvfrom(2048)
            except socket.timeout:
                continue
            r = rs.decode(data)
            if r:
                reports.append(r)
    finally:
        if worker:
            worker.join()
        bridge.terminate()
        bridge.wait(timeout=3)
        sock.close()
    assert not errors, f"load script failed: {errors[0]}"
    return reports


def test_status_reports_arrive_once_a_second():
    _require()
    reports = _collect(4.5)
    assert len(reports) >= 3, f"{len(reports)} status reports in 4.5 s (expected one per second)"
    gaps = [(b["timestamp_us"] - a["timestamp_us"]) / 1e6 for a, b in zip(reports, reports[1:])]
    assert all(0.8 <= g <= 1.3 for g in gaps), f"report spacing {gaps}"
    last = reports[-1]
    names = {t["name"] for t in last["tasks"]}
    assert {"rpmsg_rx", "repl_rx", "repl_tx"} <= names, f"task names {names}"
    assert last["cycle_hz"] == 200_000_000
    assert 0 < last["pool_high_water"] <= last["pool_capacity"]
    assert last["dropped_resumes"] == 0
    r = rs.rates(reports[-2], last)
    assert r and 0.0 <= r["busy_pct"] < 5.0, f"idle board busy {r and r['busy_pct']:.2f}%"
    assert any(d["name"] == "timer" for d in r["drivers"])


LOAD = """
import abstractx
abstractx.uart_init(115200)  # the streamer switches the GPS to 115200
abstractx.i2c_init(400000)
t = abstractx.ticks_ms()
n = 0
while abstractx.ticks_ms() - t < 3500:
    abstractx.i2c_read(0x1E, 0x03, 6)
    abstractx.uart_read(512)
    x = 0
    for i in range(300):
        x += i
    n += 1
print('LOOPS', n)
"""


def test_status_shows_script_load_and_driver_interrupts():
    _require()
    out = []
    reports = _collect(5.0, during=lambda: out.append(repl_exec(LOAD, 15.0)))
    assert out and "LOOPS" in out[0][0], f"load script output: {out}"
    rates = [rs.rates(a, b) for a, b in zip(reports, reports[1:])]
    rates = [r for r in rates if r]
    assert rates, "no comparable reports during the load"
    peak = max(rates, key=lambda r: r["busy_pct"])
    repl = next(t for t in peak["tasks"] if t["name"] == "repl_rx")
    assert peak["busy_pct"] > 30.0, f"busy {peak['busy_pct']:.1f}% while a script spins"
    assert repl["cpu_pct"] > 20.0, f"repl_rx {repl['cpu_pct']:.1f}% (the script runs inside it)"
    # Task + interrupt time never exceeds busy time (charges are exclusive).
    for r in rates:
        assert sum(t["cpu_pct"] for t in r["tasks"]) + r["isr_pct"] <= r["busy_pct"] + 1.0, r
    drivers = {d["name"]: d for d in peak["drivers"]}
    assert drivers.get("twi1", {}).get("irq_per_s", 0) > 50, f"twi1 interrupts {drivers.get('twi1')}"
    assert "uart3" in drivers, f"uart3 missing from drivers {list(drivers)}"


STUDIO_BRIDGE = os.path.join(HERE, "board_studio_bridge.py")


def test_cli_runs_beside_the_sensor_stream():
    """[SPEC-E906-REPL-19]: CLI commands run on the E906 while the sensor stream keeps going."""
    _require()
    need = ("board_studio_bridge.py", "run_e906_script.py", "raw_sensor_streamer.py", "sensor_contract.py")
    if not all(os.path.exists(os.path.join(HERE, f)) for f in need):
        skip("copy " + ", ".join(need) + " next to this file")
    sink = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sink.bind(("127.0.0.1", 0))
    env = dict(os.environ, STUDIO_HOST_IP="127.0.0.1", STUDIO_PORT=str(sink.getsockname()[1]),
               STREAM_DURATION="20")
    proc = subprocess.Popen([sys.executable, "-u", STUDIO_BRIDGE], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    lines = []
    threading.Thread(target=lambda: [lines.append(l.rstrip()) for l in proc.stdout], daemon=True).start()

    def wait(pred, secs):
        end = time.monotonic() + secs
        while time.monotonic() < end:
            if any(pred(l) for l in list(lines)):
                return True
            time.sleep(0.05)
        return False

    def forwarded():
        return sum(1 for l in list(lines) if "Forwarded #" in l)

    try:
        assert wait(lambda l: "Forwarded #" in l, 12), f"stream did not start: {lines[-5:]}"
        cmds = {"1": "1+2", "2": "x = abstractx.ticks_ms()", "3": "x > 0", "4": "1/0",
                "5": "for i in range(3): print('line', i)"}
        t0 = time.monotonic()
        for cid, code in cmds.items():
            proc.stdin.write(f"CLI {cid} {code.encode().hex()}\n")
        proc.stdin.flush()
        assert wait(lambda l: l.startswith("!CLI:5:END"), 6), f"no reply: {[l for l in lines if '!CLI' in l]}"
        rtt = time.monotonic() - t0
        before = forwarded()
        cli = [l for l in lines if l.startswith("!CLI:")]
        assert "!CLI:1:OUT:3" in cli and "!CLI:1:END:OK" in cli
        assert "!CLI:3:OUT:True" in cli
        assert any(l.startswith("!CLI:4:END:ERR:ZeroDivisionError") for l in cli), cli
        assert [l for l in cli if l.startswith("!CLI:5:OUT:")] == [
            "!CLI:5:OUT:line 0", "!CLI:5:OUT:line 1", "!CLI:5:OUT:line 2"], cli
        assert "!CLI:5:END:OK" in cli
        assert wait(lambda l: False, 1.5) is False and forwarded() > before, "stream stopped after the CLI"
        print(f"CLI: 5 commands answered in {rtt * 1000:.0f} ms; stream continued")
    finally:
        proc.terminate()
        proc.wait(timeout=5)
        sink.close()
