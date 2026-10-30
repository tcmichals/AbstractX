#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
bench_e906_runtime.py - Collect the E906 runtime benchmark (stdlib only, runs on the board).

  python3 bench_e906_runtime.py [--n 1000] [--json out.json]
  python3 bench_e906_runtime.py --imu 2000 [--json out.json]   # IMU DRDY auto-DMA run only

Talks to endpoint 1030 'abstractx-bench' served by e906_bench_app (bare metal) or
e906_freertos_app (FreeRTOS); both run the same benchmark code ([SPEC-E906-BENCH-*]).
Exit 0 ok, 1 a check failed, 2 could not run (endpoint not announced).
"""
# @impl [SPEC-E906-BENCH-02] [SPEC-E906-BENCH-04] [SPEC-E906-BENCH-05] apps/e906_bench/SPECIFICATION.md

import json
import os
import select
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


def main(argv) -> int:
    n, json_out, imu = 1000, None, 0
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == "--n" and args:
            n = int(args.pop(0))
        elif a == "--imu" and args:
            imu = int(args.pop(0))
        elif a == "--json" and args:
            json_out = args.pop(0)
        else:
            print(__doc__.strip())
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
