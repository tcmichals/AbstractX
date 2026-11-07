#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
bench_e906_runtime.py - Collect the E906 runtime benchmark (stdlib only, runs on the board).

  python3 bench_e906_runtime.py [--n 1000] [--json out.json]
  python3 bench_e906_runtime.py --imu 2000 [--json out.json]   # IMU DRDY auto-DMA run only
  python3 bench_e906_runtime.py --cyclic 60000 [--profiler-on] [--json out.json] # cyclictest equivalent (1 ms)
  python3 bench_e906_runtime.py --native-cyclic 60000 [--json out.json] # native FreeRTOS task on vTaskDelayUntil(1)
  python3 bench_e906_runtime.py --oneway 60000 [--rt] [--reader c|python] [--oneway-size 32] [--json out.json]
                                # E906 event to the data in a Linux thread; needs e906_oneway_reader next to this file

Talks to endpoint 1030 'abstractx-bench' served by e906_bench_app (bare metal) or
e906_freertos_app (FreeRTOS); both run the same benchmark code ([SPEC-E906-BENCH-*]).
Exit 0 ok, 1 a check failed, 2 could not run (endpoint not announced).
"""
# @impl [SPEC-E906-BENCH-02] [SPEC-E906-BENCH-04] [SPEC-E906-BENCH-05] [SPEC-E906-BENCH-06] [SPEC-E906-BENCH-07] apps/e906_bench/SPECIFICATION.md

import ctypes
import json
import os
import select
import signal
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
cur = HERE
while cur != os.path.dirname(cur):
    if os.path.exists(os.path.join(cur, ".git")):
        t = os.path.join(cur, "tools")
        if os.path.isdir(t) and t not in sys.path:
            sys.path.insert(0, t)
        break
    cur = os.path.dirname(cur)

BENCH = {"name": "abstractx-bench", "addr": 1030}
MCYCLE_HZ = 200_000_000  # [SPEC-E906-BENCH-02] measured 2026-10-21
SIZES = (16, 256, 496)


def parse_report(messages) -> dict:
    """'key=value' lines from one or more report messages, up to 'end'. Numbers become ints."""
    out = {}
    for msg in messages:
        for line in msg.decode("utf-8", "replace").splitlines():
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            out[key] = int(value) if value.isdigit() else value
    return out


def report_complete(messages) -> bool:
    return "end" in parse_report(messages)


def percentile(sorted_vals, p: float):
    if not sorted_vals:
        return None
    k = min(len(sorted_vals) - 1, max(0, int(round(p / 100.0 * (len(sorted_vals) - 1)))))
    return sorted_vals[k]


def summarize_us(samples_s) -> dict:
    v = sorted(x * 1e6 for x in samples_s)
    return {"n": len(v), "min_us": round(v[0], 1), "median_us": round(percentile(v, 50), 1),
            "p99_us": round(percentile(v, 99), 1), "max_us": round(v[-1], 1)} if v else {"n": 0}


def cycles_to_ns(report: dict) -> dict:
    return {k.replace("_cycles", "_ns"): round(v * 1e9 / MCYCLE_HZ, 1)
            for k, v in report.items() if "_cycles_" in k and isinstance(v, int)}


class BenchLink:
    def __init__(self):
        from e906_rpmsg_bridge import create_endpoint
        self.fd = os.open(create_endpoint("/dev/rpmsg_ctrl0", BENCH["name"], BENCH["addr"]),
                          os.O_RDWR | os.O_NONBLOCK)

    def close(self):
        os.close(self.fd)

    def send(self, data: bytes):
        os.write(self.fd, data)

    def recv(self, timeout_s: float):
        if select.select([self.fd], [], [], timeout_s)[0]:
            try:
                return os.read(self.fd, 4096)
            except BlockingIOError:
                return None
        return None

    def drain(self):
        while self.recv(0.05) is not None:
            pass


def run_echo(link: BenchLink, size: int, n: int) -> dict:
    payload = b"E" + bytes((i & 0xFF for i in range(size - 1)))
    rtts, bad = [], 0
    t_start = time.monotonic()
    for _ in range(n):
        t0 = time.monotonic()
        link.send(payload)
        reply = link.recv(1.0)
        if reply is None:
            return {"size": size, "error": "timeout", "completed": len(rtts)}
        rtts.append(time.monotonic() - t0)
        bad += reply != payload
    elapsed = time.monotonic() - t_start
    out = {"size": size, "mismatches": bad, "throughput_Bps": int(2 * size * n / elapsed)}
    out.update(summarize_us(rtts))
    return out


def run_suite(link: BenchLink, timeout_s: float = 30.0, request: bytes = b"R") -> dict:
    link.send(request)
    msgs, deadline = [], time.monotonic() + timeout_s
    while time.monotonic() < deadline and not report_complete(msgs):
        m = link.recv(0.5)
        if m:
            msgs.append(m)
    rep = parse_report(msgs)
    if "end" not in rep:
        rep["error"] = "incomplete report"
    return rep


def run_imu(link: BenchLink, count: int) -> dict:
    """[SPEC-E906-BENCH-05] DRDY auto-DMA run; the firmware stops itself on a timeout."""
    rep = run_suite(link, timeout_s=count / 1000.0 + 10.0, request=b"A" + count.to_bytes(4, "little"))
    if rep.get("imu_init") != "ok" or rep.get("imu_timeout") or rep.get("samples") != count:
        rep.setdefault("error", "imu run incomplete")
    return rep


def run_cyclic(link: BenchLink, count: int, profiler: bool = False) -> dict:
    """[SPEC-E906-BENCH-06] cyclictest equivalent: 1 ms deadlines, wake lateness."""
    flags = 1 if profiler else 0
    payload = b"C" + count.to_bytes(4, "little") + bytes([flags])
    rep = run_suite(link, timeout_s=count / 1000.0 + 15.0, request=payload)
    if rep.get("cyc_count", 0) + rep.get("cyc_missed", 0) != count:
        rep.setdefault("error", "cyclic run incomplete")
    return rep


def run_native_cyclic(link: BenchLink, count: int) -> dict:
    """[SPEC-E906-BENCH-06] native FreeRTOS task on vTaskDelayUntil(1)."""
    payload = b"F" + count.to_bytes(4, "little")
    rep = run_suite(link, timeout_s=count / 1000.0 + 15.0, request=payload)
    if rep.get("cyc_count", 0) + rep.get("cyc_missed", 0) != count:
        rep.setdefault("error", "native cyclic run incomplete")
    return rep


# ---------------------------------------------------------------------------
# [SPEC-E906-BENCH-07] one way: E906 event to the data in a Linux thread
# ---------------------------------------------------------------------------
TICKS_PER_US = 24
HIST_US = 20000
SAMPLE_TAG = 0x01
SAMPLE = struct.Struct("<B3xIQQQ")
READER_BIN = os.path.join(HERE, "e906_oneway_reader")


def clock_bounds(pings) -> dict:
    """(t0, e, t1) probes to bounds on the offset d, from t0 <= e - d <= t1."""
    return {"lo": max(e - t1 for t0, e, t1 in pings), "hi": min(e - t0 for t0, e, t1 in pings),
            "min_rtt": min(t1 - t0 for t0, e, t1 in pings), "n": len(pings)}


def clock_verdict(before: dict) -> tuple:
    """(kind, offset) from the first probe."""
    if before["lo"] > before["hi"]:
        return "inconsistent", 0
    if before["lo"] <= 0 <= before["hi"]:
        return "shared", 0
    return "calibrated", before["lo"] + (before["hi"] - before["lo"]) // 2


def clock_reach(bounds: dict, offset: int) -> int:
    return max(abs(bounds["lo"] - offset), abs(bounds["hi"] - offset))


def hist_stats(values) -> dict:
    """Same keys and 1 us buckets as e906_oneway_reader.c. `values` in ticks, never negative."""
    n = len(values)
    v = sorted(values)

    def pct(per_mille):
        need = (n * per_mille + 999) // 1000
        return min(v[need - 1] // TICKS_PER_US + 1, HIST_US) if n else HIST_US
    to_ns = lambda t: t * 1000 // TICKS_PER_US  # noqa: E731
    return {"n": n, "min_ns": to_ns(v[0]) if n else 0, "avg_ns": to_ns(sum(v) // n) if n else 0,
            "max_ns": to_ns(v[-1]) if n else 0, "p50_us": pct(500), "p99_us": pct(990), "p999_us": pct(999),
            "over_20ms": sum(1 for t in v if t // TICKS_PER_US >= HIST_US)}


def python_ticks(pair: int) -> int:
    ns = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
    return ns // 1_000_000_000 * 24_000_000 + (ns % 1_000_000_000) * TICKS_PER_US // 1000 + pair


def probe_clock(fd: int, pair: int, n: int) -> dict:
    pings = []
    for _ in range(n):
        t0 = python_ticks(pair)
        os.write(fd, b"T")
        reply = os.read(fd, 512)
        t1 = python_ticks(pair)
        if len(reply) != 9 or reply[0:1] != b"T":
            raise RuntimeError("clock probe: unexpected reply")
        pings.append((t0, int.from_bytes(reply[1:9], "little"), t1))
    return clock_bounds(pings)


def oneway_check(result: dict, count: int) -> None:
    """Sets result['error'] from the same rules as the C reader."""
    rep = result["report"]
    if any(k not in rep for k in ("ow_sent", "ow_tx_fail", "ow_missed")):
        result.setdefault("error", "report incomplete")
    elif rep["ow_sent"] + rep["ow_tx_fail"] + rep["ow_missed"] != count:
        result.setdefault("error", "firmware counts do not add up")
    elif result["lost"]:
        result.setdefault("error", "samples lost")
    elif result["out_of_order"]:
        result.setdefault("error", "samples out of order")
    elif result["negative"]:
        result.setdefault("error", "negative time: clock wrong")
    elif rep.get("dropped_resumes") != 0:
        result.setdefault("error", "dropped resumes")


def run_oneway_python(fd: int, count: int, size: int = 32, rt: bool = False, cpu: int = 2, pings: int = 2000,
                      pair: int = 0, timeout_s: float = 0.0) -> dict:
    """The Python reader: same steps and report keys as e906_oneway_reader.c."""
    os.sched_setaffinity(0, {cpu})
    if rt:
        os.sched_setscheduler(0, os.SCHED_FIFO, os.sched_param(95))
        if ctypes.CDLL(None).mlockall(3) != 0:
            raise OSError("mlockall failed")
    result = {"reader": "python", "sched": "fifo95" if rt else "other", "cpu": cpu, "count": count, "size": size,
              "clock": {"kind": "unknown", "offset_ticks": 0, "uncertainty_ns": 0, "before": {}, "after": {}},
              "report": {}, "received": 0, "lost": 0, "out_of_order": 0, "negative": 0}
    sent, event, report, last_seq = [], [], [], -1

    def on_alarm(_sig, _frame):
        raise TimeoutError()
    old = signal.signal(signal.SIGALRM, on_alarm)
    budget = timeout_s or count / 1000.0 + 15.0
    clock = result["clock"]
    try:
        signal.setitimer(signal.ITIMER_REAL, budget)
        before = clock["before"] = probe_clock(fd, pair, pings)
        kind, offset = clock_verdict(before)
        clock.update(kind=kind, offset_ticks=offset)
        if kind == "inconsistent":
            raise RuntimeError("clock bounds cross: not a fixed offset")
        uncertainty = clock_reach(before, offset)
        signal.setitimer(signal.ITIMER_REAL, budget)
        os.write(fd, b"O" + count.to_bytes(4, "little") + b"\0" + size.to_bytes(2, "little"))
        while not report_complete(report):
            msg = os.read(fd, 512)
            t = python_ticks(pair)  # the data is in this thread
            if msg[:1] == bytes([SAMPLE_TAG]) and len(msg) >= SAMPLE.size:
                _, seq, deadline, _woke, stamp = SAMPLE.unpack_from(msg)
                result["out_of_order"] += seq <= last_seq
                last_seq = seq
                a, b = t - (stamp - offset), t - (deadline - offset)
                result["negative"] += a < -uncertainty or b < -uncertainty
                sent.append(max(a, 0))
                event.append(max(b, 0))
            else:
                report.append(msg)
        signal.setitimer(signal.ITIMER_REAL, budget)
        after = clock["after"] = probe_clock(fd, pair, pings)
        if after["lo"] > after["hi"]:
            clock["kind"] = "inconsistent"
            raise RuntimeError("second clock probe: bounds cross")
        uncertainty = max(uncertainty, clock_reach(after, offset))
        clock["uncertainty_ns"] = uncertainty * 1000 // TICKS_PER_US
        if kind == "shared" and not after["lo"] <= 0 <= after["hi"]:
            raise RuntimeError("first clock probe said shared, second contradicts it")
    except TimeoutError:
        result["error"] = "timed out"
    except (RuntimeError, OSError) as e:
        result["error"] = str(e)
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, old)
    result.update(sent_to_read=hist_stats(sent), event_to_read=hist_stats(event), received=len(sent),
                  report=parse_report(report))
    result["lost"] = result["report"].get("ow_sent", len(sent)) - len(sent)
    oneway_check(result, count)
    return result


def linux_setup(cpu: int = 2) -> dict:
    """What decides a Linux latency number; recorded with every run (Part 4 §8)."""
    def read(path):
        try:
            with open(path) as f:
                return f.read().strip()
        except OSError:
            return None
    base = f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/"
    return {"governor": read(base + "scaling_governor"), "cur_khz": read(base + "scaling_cur_freq"),
            "rt_kernel": read("/sys/kernel/realtime"), "cmdline": read("/proc/cmdline")}


def clock_pair(reader_bin: str) -> int:
    out = subprocess.run([reader_bin, "--clock-pair"], check=True, capture_output=True, text=True).stdout
    return json.loads(out)["cntvct_minus_monoraw_ticks"]


def run_oneway_c(reader_bin: str, node: str, count: int, size: int, rt: bool) -> dict:
    cmd = [reader_bin, "--dev", node, "--count", str(count), "--size", str(size)] + (["--rt"] if rt else [])
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode == 2 or not proc.stdout:
        return {"reader": "c", "error": f"could not run: {proc.stderr.strip()}"}
    return json.loads(proc.stdout)


def main_oneway(count: int, size: int, reader: str, rt: bool, reader_bin: str, json_out) -> int:
    from e906_rpmsg_bridge import create_endpoint
    if not os.path.exists("/dev/rpmsg_ctrl0"):
        print("could not run: /dev/rpmsg_ctrl0 missing (not an E906 board)")
        return 2
    if not os.access(reader_bin, os.X_OK):
        print(f"could not run: {reader_bin} missing (cross-compile it, Part 4 §7)")
        return 2
    node = create_endpoint("/dev/rpmsg_ctrl0", BENCH["name"], BENCH["addr"])
    if reader == "c":
        rep = run_oneway_c(reader_bin, node, count, size, rt)
    else:
        pair = clock_pair(reader_bin)
        fd = os.open(node, os.O_RDWR)  # blocking: the read() wake-up is what is measured
        try:
            rep = run_oneway_python(fd, count, size, rt, pair=pair)
        finally:
            os.close(fd)
        rep["clock_pair_ticks"] = pair
    result = {"check": "board:e906:oneway", "time": time.strftime("%Y-%m-%dT%H:%M:%S"),
              "uname": os.uname().release, "setup": linux_setup(), "oneway": rep}
    print(json.dumps(result, indent=1))
    if json_out:
        with open(json_out, "w") as f:
            json.dump(result, f, indent=1)
    return 1 if "error" in rep else 0


def main(argv) -> int:
    n, json_out, imu, cyc, native_cyc = 1000, None, 0, 0, 0
    profiler = False
    oneway, oneway_size, reader, rt, reader_bin = 0, 32, "c", False, READER_BIN
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == "--n" and args:
            n = int(args.pop(0))
        elif a == "--imu" and args:
            imu = int(args.pop(0))
        elif a == "--cyclic" and args:
            cyc = int(args.pop(0))
        elif a == "--native-cyclic" and args:
            native_cyc = int(args.pop(0))
        elif a == "--profiler-on":
            profiler = True
        elif a == "--oneway" and args:
            oneway = int(args.pop(0))
        elif a == "--oneway-size" and args:
            oneway_size = int(args.pop(0))
        elif a == "--reader" and args:
            reader = args.pop(0)
        elif a == "--rt":
            rt = True
        elif a == "--reader-bin" and args:
            reader_bin = args.pop(0)
        elif a == "--json" and args:
            json_out = args.pop(0)
        else:
            print(__doc__.strip())
            return 2
    if oneway:
        try:
            return main_oneway(oneway, oneway_size, reader, rt, reader_bin, json_out)
        except (OSError, RuntimeError) as e:
            print(f"could not run: {e} (is e906_bench_app or e906_freertos_app loaded?)")
            return 2
    from e906_rpmsg_bridge import find_node
    if not os.path.exists("/dev/rpmsg_ctrl0"):
        print("could not run: /dev/rpmsg_ctrl0 missing (not an E906 board)")
        return 2
    try:
        link = BenchLink()
    except (OSError, RuntimeError) as e:
        print(f"could not run: {e} (is e906_bench_app or e906_freertos_app loaded?)")
        return 2
    if cyc or native_cyc:
        count = native_cyc if native_cyc else cyc
        try:
            link.drain()
            rep = run_native_cyclic(link, count) if native_cyc else run_cyclic(link, count, profiler)
        finally:
            link.close()
        check_name = "board:e906:native-cyclic" if native_cyc else "board:e906:cyclic"
        result = {"check": check_name, "time": time.strftime("%Y-%m-%dT%H:%M:%S"),
                  "uname": os.uname().release, "count": count, "cyclic": rep}
        print(json.dumps(result, indent=1))
        if json_out:
            with open(json_out, "w") as f:
                json.dump(result, f, indent=1)
        return 1 if "error" in rep else 0
    if imu:
        try:
            link.drain()
            rep = run_imu(link, imu)
        finally:
            link.close()
        result = {"check": "board:e906:imu-drdy", "time": time.strftime("%Y-%m-%dT%H:%M:%S"),
                  "uname": os.uname().release, "count": imu, "imu": rep, "imu_ns": cycles_to_ns(rep)}
        print(json.dumps(result, indent=1))
        if json_out:
            with open(json_out, "w") as f:
                json.dump(result, f, indent=1)
        return 1 if "error" in rep or rep.get("dropped_resumes", 0) != 0 else 0
    try:
        link.drain()
        echo = [run_echo(link, s, n) for s in SIZES]
        suite = run_suite(link)
    finally:
        link.close()
    result = {"check": "board:e906:bench", "time": time.strftime("%Y-%m-%dT%H:%M:%S"),
              "uname": os.uname().release, "n": n, "echo": echo, "suite": suite,
              "suite_ns": cycles_to_ns(suite)}
    print(json.dumps(result, indent=1))
    if json_out:
        with open(json_out, "w") as f:
            json.dump(result, f, indent=1)
    failed = any("error" in e or e.get("mismatches") for e in echo) or "error" in suite \
        or suite.get("dropped_resumes", 0) != 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
