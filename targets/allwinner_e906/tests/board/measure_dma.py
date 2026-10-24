# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# Per-interrupt CPU cost of the E906 DMA completion (line 88) from timeline spans.
# @verifies [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md
import statistics
import test_e906_trace as t
from e906_link import repl_exec
import event_streams as es
CODE = """
import abstractx
abstractx.sleep_ms(1500)
s = 0
for i in range(200):
    ok, d, c = abstractx.dma_bench(1024, 1)
    s += d
print('MEAN_DMA_CYC', s // 200)
"""
out, raw = [], []
t._collect(6.0, during=lambda: out.append(repl_exec(CODE, 20)), raw=raw)
print(out[0][0].strip() if out else out)
spans = [s for p in map(es.decode_timeline, raw) if p for s in p["spans"] if s["kind"] == es.KIND_ISR and s["id"] == 88]
import collections
print("packets", sum(1 for p in map(es.decode_timeline, raw) if p), "isr ids", collections.Counter(s["id"] for p in map(es.decode_timeline, raw) if p for s in p["spans"] if s["kind"] == es.KIND_ISR))
us = sorted(s["duration"] * 1e6 / 200e6 for s in spans)
if us: print(f"line 88 ISR spans {len(us)}: mean {statistics.mean(us):.2f} us, max {us[-1]:.2f} us")
