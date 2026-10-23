#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @verifies [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
"""Per-interrupt cost of UART3 (line 90) and TWI1 (line 91) under a MicroPython load.
Runs on the board next to the board tests. Usage: measure_isr.py uart|twi [seconds]"""
import sys, statistics
import test_e906_trace as t
from e906_link import repl_exec
import event_streams as es

which = sys.argv[1] if len(sys.argv) > 1 else "uart"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0
UART = """
import abstractx
abstractx.uart_init(115200)
t = abstractx.ticks_ms()
n = 0
while abstractx.ticks_ms() - t < %d:
    n += len(abstractx.uart_read(512))
    abstractx.sleep_ms(100)
print('BYTES', n)
""" % int((secs - 1.5) * 1000)
TWI = """
import abstractx
abstractx.i2c_init(400000)
t = abstractx.ticks_ms()
n = 0
while abstractx.ticks_ms() - t < %d:
    abstractx.i2c_read(0x1E, 0x03, 6)
    n += 1
print('READS', n)
""" % int((secs - 1.5) * 1000)
line = 90 if which == "uart" else 91
out, raw = [], []
reports = t._collect(secs, during=lambda: out.append(repl_exec(UART if which == "uart" else TWI, secs + 10)), raw=raw)
print("script:", out[0][0].strip() if out else out)
spans = [s for p in map(es.decode_timeline, raw) if p for s in p["spans"] if s["kind"] == es.KIND_ISR and s["id"] == line]
hz = 200e6
us = sorted(s["duration"] * 1e6 / hz for s in spans)
rates = [r for r in (t.rs.rates(a, b) for a, b in zip(reports, reports[1:])) if r]
drv = [d for r in rates for d in r["drivers"] if d["id"] == line and d["irq_per_s"] > 0]
if drv:
    print(f"line {line}: {statistics.mean(d['irq_per_s'] for d in drv):.0f} irq/s, "
          f"{statistics.mean(d['isr_pct'] for d in drv):.3f}% CPU (status reports)")
if us:
    print(f"spans {len(us)}: mean {statistics.mean(us):.2f} us, median {us[len(us)//2]:.2f}, "
          f"p99 {us[int(len(us)*0.99)]:.2f}, max {us[-1]:.2f} us")
