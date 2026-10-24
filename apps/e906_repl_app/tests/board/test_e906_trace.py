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
# @verifies [SPEC-TRACE-09] docs/DESIGN_SPECIFICATION.md
# @verifies [SPEC-TRACE-10] docs/DESIGN_SPECIFICATION.md
# @verifies [SPEC-E906-IRQ-06] targets/allwinner_e906/SPECIFICATION.md

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
    import event_streams as es  # noqa: E402
except ImportError:
    rs = es = None

BRIDGE = os.path.join(HERE, "e906_rpmsg_bridge.py")


def _require():
    if not on_e906_board():
        skip("not an E906 board (no remoteproc0)")
    if rs is None or not os.path.exists(BRIDGE):
        skip("copy tools/visualizer/runtime_stats.py, event_streams.py and tools/e906_rpmsg_bridge.py next to this file")


def _collect(seconds: float, during=None, raw=None):
    """Status reports received for `seconds`, optionally while `during()` runs in a thread.
    Every datagram is also appended to `raw` when given."""
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
            if raw is not None:
                raw.append(data)
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


def _events(raw):
    prof = [p for p in map(es.decode_profile, raw) if p]
    tl = [t for t in map(es.decode_timeline, raw) if t]
    samples = [smp for p in prof for smp in p["samples"]]
    spans = [sp for t in tl for sp in t["spans"]]
    return prof, tl, samples, spans


def test_profiler_and_timeline_when_idle():
    """[SPEC-TRACE-09]/[SPEC-TRACE-10]/[SPEC-E906-IRQ-06]: ~1 kHz samples, idle stays whole, busy % low."""
    _require()
    raw = []
    reports = _collect(4.5, raw=raw)
    prof, tl, samples, spans = _events(raw)
    assert prof and tl, f"profile packets {len(prof)}, timeline packets {len(tl)}"
    assert prof[-1]["sample_hz"] == 1000, prof[-1]["sample_hz"]
    assert tl[-1]["cycle_hz"] == 200_000_000, tl[-1]["cycle_hz"]
    assert 3000 <= len(samples) <= 5000, f"{len(samples)} samples in 4.5 s at 1 kHz"
    idle = sum(1 for _, owner in samples if owner == es.OWNER_IDLE)
    assert idle > 0.9 * len(samples), f"idle board: {idle}/{len(samples)} samples idle"
    idle_spans = [sp for sp in spans if sp["kind"] == es.KIND_IDLE]
    assert idle_spans and len(idle_spans) < 50 * 4.5, f"{len(idle_spans)} idle spans: sample interrupts woke the loop"
    # Drop counters are cumulative since boot: compare across this window only.
    d_samples = prof[-1]["dropped"] - prof[0]["dropped"]
    d_spans = tl[-1]["dropped"] - tl[0]["dropped"]
    assert d_samples == 0 and d_spans == 0, f"dropped while idle: samples {d_samples}, spans {d_spans}"
    r = rs.rates(reports[-2], reports[-1])
    assert r["busy_pct"] < 5.0, f"idle busy {r['busy_pct']:.2f}% with the profiler on"
    print(f"idle: {len(samples)} samples, {len(idle_spans)} idle spans, busy {r['busy_pct']:.2f}%")


def test_profiler_and_timeline_follow_the_script():
    """[SPEC-TRACE-09]/[SPEC-TRACE-10]: under load, samples and resume spans belong to repl_rx; twi1 spans appear."""
    _require()
    raw, out = [], []
    reports = _collect(5.0, during=lambda: out.append(repl_exec(LOAD, 15.0)), raw=raw)
    assert out and "LOOPS" in out[0][0], f"load script output: {out}"
    prof, tl, samples, spans = _events(raw)
    frames = {t["frame"]: t["name"] for t in reports[-1]["tasks"]}
    repl = next(f for f, n in frames.items() if n == "repl_rx")
    in_repl = sum(1 for _, owner in samples if owner == repl)
    assert in_repl > 0.3 * len(samples), f"{in_repl}/{len(samples)} samples owned by repl_rx while the script spins"
    assert all(0x3FFC0000 <= pc < 0x3FFF0000 or 0x40000000 <= pc < 0x40020000 for pc, _ in samples), "PC outside firmware"
    assert any(sp["kind"] == es.KIND_RESUME and sp["id"] == repl for sp in spans)
    twi = next(d["id"] for d in reports[-1]["drivers"] if d["name"] == "twi1")
    isr = [sp for sp in spans if sp["kind"] == es.KIND_ISR and sp["id"] == twi]
    assert isr, "no twi1 interrupt spans during the I2C loop"
    d_samples = prof[-1]["dropped"] - prof[0]["dropped"]
    d_spans = tl[-1]["dropped"] - tl[0]["dropped"]
    print(f"load: {in_repl}/{len(samples)} samples in repl_rx, {len(isr)} twi1 spans, "
          f"dropped in window: samples {d_samples}, spans {d_spans}")
    assert d_samples < 0.05 * len(samples), f"{d_samples} samples dropped vs {len(samples)} received"


def _no_recipient_lines():
    out = subprocess.run(["dmesg"], capture_output=True, text=True).stdout
    return sum(1 for line in out.splitlines() if "no recipient" in line or "callbacks suppressed" in line)


def test_trace_quiet_after_reader_closes():
    """[SPEC-E906-IPC-05]: once the reader closes and its 3 s lease runs out, the E906 sends nothing
    more on the trace endpoint, so Linux logs no 'msg received with no recipient' (2026-10-22: the
    first profiler firmware flooded the serial console and the board was rebooted twice)."""
    _require()
    reports = _collect(2.0)
    assert reports, "no status while a reader listened"
    time.sleep(4.0)                     # lease runs out
    before = _no_recipient_lines()
    time.sleep(3.0)
    after = _no_recipient_lines()
    assert after == before, f"{after - before} new 'no recipient' kernel lines after the reader closed"
