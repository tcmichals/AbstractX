# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-TRACE-08] [SPEC-STUDIO-15] tools/visualizer/SPECIFICATION.md
"""Decode AbstractX runtime status packets (CTF stream 3) and turn two of them
into rates: CPU busy %, per-task CPU %, per-driver interrupts/s and ISR %.

Layout is [SPEC-TRACE-08] in docs/DESIGN_SPECIFICATION.md; counters are
cumulative u32 and wrap, so every difference is taken modulo 2**32.
"""
import struct

CTF_MAGIC = 0xC1FC1FC1
CTF_HEADER = struct.Struct("<IB3xIIQQII")      # 40 bytes (CtfPacketHeaderWire)
STREAM_ID = 3
EVENT_ID = 0x20
HEADER = struct.Struct("<BBBBQIIIIIIIIHHII")   # 56 bytes (RuntimeStatsHeader)
TASK = struct.Struct("<12sIIII")              # 28 bytes (TaskStatsRecord)
DRIVER = struct.Struct("<8sHHIII")            # 24 bytes (DriverStatsRecord)
assert (CTF_HEADER.size, HEADER.size, TASK.size, DRIVER.size) == (40, 56, 28, 24)

_U32 = 1 << 32


def is_ctf(data: bytes) -> bool:
    return len(data) >= CTF_HEADER.size and struct.unpack_from("<I", data)[0] == CTF_MAGIC


def ctf_stream_id(data: bytes) -> int:
    return data[4]


def _name(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii", "replace")


def decode(data: bytes):
    """One stream-3 packet -> dict, or None if it is not a complete status report."""
    if not is_ctf(data) or ctf_stream_id(data) != STREAM_ID:
        return None
    off = CTF_HEADER.size
    if len(data) < off + HEADER.size:
        return None
    (ev, ver, n_tasks, n_drv, ts_us, hz, cycles, idle, steps, pool_used, pool_hw, pool_cap,
     dropped, q_depth, q_cap, discarded, _r) = HEADER.unpack_from(data, off)
    if ev != EVENT_ID or ver != 1:
        return None
    off += HEADER.size
    if len(data) < off + n_tasks * TASK.size + n_drv * DRIVER.size:
        return None
    tasks = []
    for _ in range(n_tasks):
        name, frame, resumes, run, mx = TASK.unpack_from(data, off)
        off += TASK.size
        tasks.append({"name": _name(name) or f"0x{frame:08x}", "frame": frame,
                      "resumes": resumes, "run_cycles": run, "max_cycles": mx})
    drivers = []
    for _ in range(n_drv):
        name, did, _r2, events, cyc, errors = DRIVER.unpack_from(data, off)
        off += DRIVER.size
        drivers.append({"name": _name(name) or f"irq{did}", "id": did, "events": events,
                        "cycles": cyc, "errors": errors})
    return {"timestamp_us": ts_us, "cycle_hz": hz, "cycles": cycles, "idle_cycles": idle,
            "steps": steps, "pool_used": pool_used, "pool_high_water": pool_hw,
            "pool_capacity": pool_cap, "dropped_resumes": dropped, "queue_depth": q_depth,
            "queue_capacity": q_cap, "trace_discarded": discarded,
            "tasks": tasks, "drivers": drivers}


def _d(a: int, b: int) -> int:
    return (b - a) % _U32


def rates(prev: dict, cur: dict):
    """Rates between two reports from the same target; None if they cannot be compared."""
    if not prev or not cur or cur["cycle_hz"] == 0:
        return None
    dc = _d(prev["cycles"], cur["cycles"])
    dt = (cur["timestamp_us"] - prev["timestamp_us"]) / 1e6
    if dc == 0 or dt <= 0:
        return None
    pct = lambda c: 100.0 * c / dc  # noqa: E731
    idle = pct(_d(prev["idle_cycles"], cur["idle_cycles"]))
    pt = {t["frame"] if t["frame"] else t["name"]: t for t in prev["tasks"]}
    tasks = []
    for t in cur["tasks"]:
        p = pt.get(t["frame"] if t["frame"] else t["name"])
        if p is None or p["name"] != t["name"]:
            p = {"resumes": 0, "run_cycles": 0}
        tasks.append({"name": t["name"], "cpu_pct": pct(_d(p["run_cycles"], t["run_cycles"])),
                      "resumes_per_s": _d(p["resumes"], t["resumes"]) / dt,
                      "max_us": t["max_cycles"] * 1e6 / cur["cycle_hz"]})
    pd = {d["id"]: d for d in prev["drivers"]}
    drivers = []
    for d in cur["drivers"]:
        p = pd.get(d["id"], {"events": 0, "cycles": 0, "errors": 0})
        drivers.append({"name": d["name"], "id": d["id"],
                        "irq_per_s": _d(p["events"], d["events"]) / dt,
                        "isr_pct": pct(_d(p["cycles"], d["cycles"])),
                        "errors": d["errors"], "new_errors": _d(p["errors"], d["errors"])})
    isr = sum(d["isr_pct"] for d in drivers)
    return {"busy_pct": max(0.0, 100.0 - idle), "idle_pct": idle, "isr_pct": isr,
            "steps_per_s": _d(prev["steps"], cur["steps"]) / dt,
            "tasks": sorted(tasks, key=lambda t: -t["cpu_pct"]),
            "drivers": sorted(drivers, key=lambda d: -d["irq_per_s"]),
            "pool_used": cur["pool_used"], "pool_high_water": cur["pool_high_water"],
            "pool_capacity": cur["pool_capacity"], "queue_depth": cur["queue_depth"],
            "queue_capacity": cur["queue_capacity"], "dropped_resumes": cur["dropped_resumes"],
            "trace_discarded": cur["trace_discarded"]}


def encode(report: dict) -> bytes:
    """Inverse of decode() (tests and the --sim feed)."""
    body = HEADER.pack(EVENT_ID, 1, len(report["tasks"]), len(report["drivers"]),
                       report["timestamp_us"], report["cycle_hz"], report["cycles"] % _U32,
                       report["idle_cycles"] % _U32, report["steps"] % _U32, report["pool_used"],
                       report["pool_high_water"], report["pool_capacity"], report["dropped_resumes"],
                       report["queue_depth"], report["queue_capacity"], report["trace_discarded"], 0)
    for t in report["tasks"]:
        body += TASK.pack(t["name"].encode()[:11], t["frame"], t["resumes"] % _U32,
                          t["run_cycles"] % _U32, t["max_cycles"] % _U32)
    for d in report["drivers"]:
        body += DRIVER.pack(d["name"].encode()[:7], d["id"], 0, d["events"] % _U32,
                            d["cycles"] % _U32, d["errors"] % _U32)
    total = CTF_HEADER.size + len(body)
    hdr = CTF_HEADER.pack(CTF_MAGIC, STREAM_ID, total * 8, total * 8,
                          report["timestamp_us"], report["timestamp_us"], 0, 0)
    return hdr + body
