#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""zynq_diagnostics.py without hardware ([SPEC-ZYNQ-03]): TLP word order against the RTL field map
(rtl/asp_tlp_pkg.sv bit positions, little-endian), completions found by tag, other TLPs kept
(EL-1027-22), exclusive lock on the UIO device. Plain python3, no pytest.

@verifies [SPEC-ZYNQ-03] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md
"""
import fcntl
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "hw/zynq7000/qmtech_zynq7020/tools"))
import zynq_diagnostics as zd  # noqa: E402

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)
        print("FAIL", msg)


def vec(words):
    """16 DWORDs -> router vector, DW k at bits [32k+31:32k] (asp_axi_lite_bridge ingress packing)."""
    return sum((w & 0xFFFFFFFF) << (32 * k) for k, w in enumerate(words))


def unvec(v):
    return [(v >> (32 * k)) & 0xFFFFFFFF for k in range(16)]


class FakeBridge:
    """The bridge TLP ports plus a control plane with one scratch register, decoded with the
    asp_tlp_pkg.sv bit positions (type [7:0], tag [23:16], channel [31:24], addr [63:32],
    len [79:64], payload word k [160+32k +: 32])."""

    def __init__(self):
        self.ing, self.egr, self.regs = [], [], {}

    def read_csr(self, off):
        if off == zd.REG_EGR_COUNT:
            return len(self.egr)
        if off == zd.REG_TLP_OUT_PORT:
            w = self.egr[0][0]
            self.egr[0] = self.egr[0][1:]
            if not self.egr[0]:
                self.egr.pop(0)
            return w
        return 0

    def write_csr(self, off, val):
        assert off == zd.REG_TLP_IN_PORT
        self.ing.append(val)
        if len(self.ing) == 16:
            v, self.ing = vec(self.ing), []
            typ, tag, ch = v & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF
            addr, n = (v >> 32) & 0xFFFFFFFF, (v >> 64) & 0xFFFF
            check(ch == zd.CH_CONTROL and n == 1, f"channel {ch:#x} / length {n}")
            if typ == zd.TLP_MEM_WR:
                self.regs[addr] = (v >> 160) & 0xFFFFFFFF
            elif typ == zd.TLP_MEM_RD:
                cpl = zd.TLP_CPL_D | (tag << 16) | (zd.CH_CONTROL << 24) | (addr << 32) | (1 << 64)
                cpl |= self.regs.get(addr, 0) << 160
                self.egr.append(unvec(cpl))


def diag_with(fake):
    d = zd.ZynqDiagnostics.__new__(zd.ZynqDiagnostics)
    d.tag_counter, d.unclaimed = 1, zd.collections.deque(maxlen=256)
    d.read_csr, d.write_csr = fake.read_csr, fake.write_csr
    return d


def test_word_order():
    w = zd.tlp_words(zd.TLP_MEM_WR, 0x11, 0x40000008, [0xCAFEF00D])
    check(w[0] == 0x01110002, f"DW0 {w[0]:#010x}: type byte 0, tag byte 2, channel byte 3")
    check(w[1] == 0x40000008 and w[2] == 1 and w[5] == 0xCAFEF00D, f"DW1/2/5 {w[1]:#x} {w[2]:#x} {w[5]:#x}")
    t = zd.parse_tlp_words(w)
    check((t["type"], t["tag"], t["addr"], t["words"]) == (2, 0x11, 0x40000008, [0xCAFEF00D]), str(t))


def test_round_trip_and_kept_tlps():
    fake = FakeBridge()
    d = diag_with(fake)
    for pat in (0xA5A55A5A, 0x12345678, 0xDEADBEEF):
        d.write32(zd.SYS_REG_SCRATCH, pat)
        check(d.read32(zd.SYS_REG_SCRATCH) == pat, f"scratch {pat:#x}")
    # a PIO stream TLP waiting in egress before the completion: kept, not dropped
    stream = 0x10 | (0x40 << 24) | (0x40000700 << 32) | (1 << 64) | (0x55 << 160)
    fake.egr.append(unvec(stream))
    d.write32(zd.SYS_REG_SCRATCH, 7)
    check(d.read32(zd.SYS_REG_SCRATCH) == 7, "read after a stream TLP")
    check(len(d.unclaimed) == 1 and d.unclaimed[0]["type"] == 0x10 and d.unclaimed[0]["words"] == [0x55],
          f"unclaimed {list(d.unclaimed)}")
    tags = set()
    for _ in range(600):
        tags.add(d._next_tag())
    check(0 not in tags and len(tags) == 255, f"tags {min(tags)}..{max(tags)} ({len(tags)})")


def test_lock():
    class FakeRegion:
        def __init__(self, path, index=0):
            self.fd = os.open(path, os.O_RDWR)

        def close(self):
            os.close(self.fd)

    orig = zd.UioRegion
    zd.UioRegion = FakeRegion
    try:
        with tempfile.NamedTemporaryFile() as f:
            holder = os.open(f.name, os.O_RDWR)
            fcntl.flock(holder, fcntl.LOCK_EX | fcntl.LOCK_NB)      # the AbstractX host holds it
            try:
                zd.ZynqDiagnostics(f.name)
                check(False, "started while the device was locked")
            except RuntimeError as e:
                check("in use" in str(e), str(e))
            os.close(holder)
            d = zd.ZynqDiagnostics(f.name)                          # free: starts
            d.close()
    finally:
        zd.UioRegion = orig


if __name__ == "__main__":
    test_word_order()
    test_round_trip_and_kept_tlps()
    test_lock()
    print("PASS" if not FAILS else f"FAIL ({len(FAILS)})")
    sys.exit(1 if FAILS else 0)
