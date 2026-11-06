# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Control TLPs -> AXI4-Lite ([SPEC-TLP-04]) on asp_ctrl_plane: asp_tlp_axil_master + axil_crossbar +
asp_sys_regs, with the PIO port answered by an AXI4-Lite slave model (random ready / valid delays,
error regions, AXI stability check). Replaces the Wishbone master test (no Wishbone, 2026-11-05).

@verifies [SPEC-TLP-04] [SPEC-TLP-07] docs/DESIGN_SPECIFICATION.md
"""

import random
import struct

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, ReadOnly, RisingEdge

from axil_bfm import AxilSlave, DECERR, SLVERR

MEM_RD, MEM_WR, CPL_D = 0x01, 0x02, 0x03
PIO = 0x40000700


def tlp(typ, tag, addr, length, words=(), seq=0, ts=0, flags=0, channel=0x01):
    """Same field map as rtl/asp_tlp_pkg.sv: the asp_tlp64_t bytes as one little-endian number."""
    payload = b"".join(struct.pack("<I", w & 0xFFFFFFFF) for w in words[:10])
    raw = struct.pack("<BBBBIHHQ40sI", typ, flags, tag, channel, addr & 0xFFFFFFFF, length & 0xFFFF,
                      seq & 0xFFFF, ts & ((1 << 64) - 1), payload, 0)
    return int.from_bytes(raw, "little")


def parse(v):
    typ, flags, tag, _ch, addr, n, seq, ts, payload, _crc = struct.unpack("<BBBBIHHQ40sI", v.to_bytes(64, "little"))
    return {"type": typ, "flags": flags, "tag": tag, "addr": addr, "len": n, "seq": seq, "ts": ts,
            "words": [struct.unpack_from("<I", payload, 4 * k)[0] for k in range(min(n, 10))]}


class Env:
    def __init__(self, dut, cpl_ready=1.0, slave_delay=0.0, errors=None):
        self.dut, self.cpl_ready = dut, cpl_ready
        self.cpls = []
        self.slave = AxilSlave(dut, "m_pio", delay=slave_delay, errors=errors)
        cocotb.start_soon(self._sink())

    async def _sink(self):
        held = None
        while True:
            await FallingEdge(self.dut.clk)
            self.dut.m_cpl_tready.value = int(random.random() < self.cpl_ready)
            await ReadOnly()
            v, d = int(self.dut.m_cpl_tvalid.value), int(self.dut.m_cpl_tdata.value)
            r = int(self.dut.m_cpl_tready.value)
            if held is not None:
                assert v and d == held, "completion changed or withdrawn before tready"
            if v and r:
                self.cpls.append(parse(d))
                held = None
            else:
                held = d if v else None

    async def send(self, t):
        await FallingEdge(self.dut.clk)
        self.dut.s_tlp_tdata.value = t
        self.dut.s_tlp_tvalid.value = 1
        for _ in range(5000):
            await RisingEdge(self.dut.clk)
            if int(self.dut.s_tlp_tready.value):
                break
        else:
            raise AssertionError("TLP not accepted")
        await FallingEdge(self.dut.clk)
        self.dut.s_tlp_tvalid.value = 0

    async def wait_cpls(self, n, limit=20000):
        for _ in range(limit):
            if len(self.cpls) >= n:
                return
            await RisingEdge(self.dut.clk)
        raise AssertionError(f"{len(self.cpls)} completions, expected {n}")

    async def idle(self, limit=5000):
        """Wait until the master takes a new TLP again (all writes of the last one finished)."""
        await self.send(tlp(0x7F, 0, 0, 0))      # unknown type: consumed only once the master is idle


async def start(dut, **kw):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    dut.rst_n.value = 0
    dut.i_sys_timestamp.value = 0
    dut.s_tlp_tvalid.value = 0
    dut.s_tlp_tdata.value = 0
    dut.m_cpl_tready.value = 1
    env = Env(dut, **kw)
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1
    await ClockCycles(dut.clk, 3)
    return env


@cocotb.test(timeout_time=2, timeout_unit="ms")
async def test_sys_id_and_scratch(dut):
    env = await start(dut)
    await env.send(tlp(MEM_RD, 0x11, 0x40000000, 1, seq=7, ts=123))
    await env.wait_cpls(1)
    c = env.cpls[0]
    assert (c["type"], c["tag"], c["flags"], c["len"], c["seq"], c["ts"]) == (CPL_D, 0x11, 0, 1, 7, 123), c
    assert c["words"] == [0xABF10164]
    await env.send(tlp(MEM_WR, 0, 0x40000008, 1, [0xCAFEF00D]))
    await env.send(tlp(MEM_RD, 0x12, 0x40000008, 1))
    await env.wait_cpls(2)
    assert env.cpls[1]["words"] == [0xCAFEF00D]


@cocotb.test(timeout_time=2, timeout_unit="ms")
async def test_bursts_of_nine_and_length_clipping(dut):
    env = await start(dut, slave_delay=0.3)
    words = [0x1000 + i for i in range(9)]
    await env.send(tlp(MEM_WR, 0, PIO + 0x40, 9, words))
    await env.idle()
    assert [(a, d) for a, d, _ in env.slave.writes] == [(PIO + 0x40 + 4 * i, w) for i, w in enumerate(words)]
    await env.send(tlp(MEM_RD, 0x21, PIO + 0x40, 12))           # 12 -> clipped to 9
    await env.wait_cpls(1)
    assert env.cpls[0]["len"] == 9 and env.cpls[0]["words"] == words and env.cpls[0]["flags"] == 0
    await env.send(tlp(MEM_RD, 0x22, PIO + 0x40, 0))            # 0 -> 1
    await env.wait_cpls(2)
    assert env.cpls[1]["len"] == 1 and env.cpls[1]["words"] == [0x1000]


@cocotb.test(timeout_time=2, timeout_unit="ms")
async def test_decerr_and_slverr_reported(dut):
    env = await start(dut, errors={PIO + 0x48: SLVERR})
    await env.send(tlp(MEM_RD, 0x31, 0x40000100, 1))          # no slave: DECERR
    await env.wait_cpls(1)
    assert env.cpls[0]["flags"] & 3 == DECERR and env.cpls[0]["len"] == 0, env.cpls[0]
    for i in range(4):
        await env.send(tlp(MEM_WR, 0, PIO + 0x40 + 4 * i, 1, [0x50 + i]))
    await env.send(tlp(MEM_RD, 0x32, PIO + 0x40, 4))           # third word fails
    await env.wait_cpls(2)
    c = env.cpls[1]
    assert c["flags"] & 3 == SLVERR and c["len"] == 2 and c["words"] == [0x50, 0x51], c
    # failed writes are counted in CTRL_WR_ERR (0x40000018); a burst stops at the first failure
    await env.send(tlp(MEM_WR, 0, 0x40000100, 3, [1, 2, 3]))  # DECERR on the first word
    await env.send(tlp(MEM_WR, 0, PIO + 0x44, 3, [7, 8, 9]))   # 0x44 ok, 0x48 SLVERR, 0x4C never written
    await env.send(tlp(MEM_RD, 0x33, 0x40000018, 1))
    await env.wait_cpls(3)
    # three failed writes: the setup write to 0x48 (the SLVERR region covers writes too), the DECERR,
    # and 0x48 again inside the burst
    assert env.cpls[2]["words"] == [3], env.cpls[2]
    assert (PIO + 0x4C, 9, 0) not in env.slave.writes


@cocotb.test(timeout_time=20, timeout_unit="ms")
async def test_random_1000_against_model(dut):
    env = await start(dut, cpl_ready=0.4, slave_delay=0.3)
    model = {}
    expect = []
    for i in range(1000):
        n = random.randint(1, 9)
        base = PIO + 4 * random.randint(0, 100)
        if random.random() < 0.5:
            words = [random.getrandbits(32) for _ in range(n)]
            await env.send(tlp(MEM_WR, 0, base, n, words))
            for k, w in enumerate(words):
                model[base + 4 * k] = w
        else:
            tag = i & 0xFF
            await env.send(tlp(MEM_RD, tag, base, n, seq=i))
            expect.append((tag, i, [model.get(base + 4 * k, 0) for k in range(n)]))
    await env.wait_cpls(len(expect), limit=200000)
    await ClockCycles(dut.clk, 200)
    assert len(env.cpls) == len(expect), "duplicate or missing completions"
    for c, (tag, seq, words) in zip(env.cpls, expect):
        assert (c["tag"], c["seq"], c["words"], c["flags"]) == (tag, seq, words, 0), (c, tag, seq, words)
