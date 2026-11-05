# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
asp_pio_wrap as an AXI4-Lite slave ([SPEC-TLP-05], no Wishbone since 2026-11-05).

Tile registers are forwarded to the pio2 tile's AXI4-Lite port; the TLP data path registers
(OUT_CFG 0x0C, OUT_TIMEOUT 0x90, IN_CFG 0x94) are served by the wrapper itself (the tile reads 0 there).
Checks: single accesses, both kinds mixed with random valid gaps, 1000 random accesses against a model,
OKAY on every response.
"""
# @verifies [SPEC-TLP-05] docs/DESIGN_SPECIFICATION.md

import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles

from axil_bfm import AxilMaster

BASE = 0x40000700
SM_SHIFTCTRL = lambda n: 0x100 + 0x20 * n + 0x08   # noqa: E731  plain read/write tile registers
SM_PINCTRL = lambda n: 0x100 + 0x20 * n + 0x0C     # noqa: E731
TILE = [f(n) for n in range(4) for f in (SM_SHIFTCTRL, SM_PINCTRL)]
LOCAL = {0x0C: 0xFFFFFFFF & ~(0x7 << 1) & ~(0x7 << 28), 0x90: 0x00FFFFFF, 0x94: 0x0000FF01}  # writable bits


async def setup(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    dut.rst_n.value = 0
    dut.i_sys_timestamp.value = 0
    dut.i_gpio.value = 0
    dut.s_tlp_tvalid.value = 0
    dut.s_tlp_tdata.value = 0
    dut.m_tlp_tready.value = 1
    m = AxilMaster(dut, "s_axil", gap=0.3)
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1
    await ClockCycles(dut.clk, 3)
    return m


async def wr(m, off, val):
    assert await m.write(BASE + off, val) == 0


async def rd(m, off):
    data, resp = await m.read(BASE + off)
    assert resp == 0
    return data


@cocotb.test(timeout_time=5, timeout_unit="ms")
async def test_tile_and_local_registers(dut):
    m = await setup(dut)
    await wr(m, SM_PINCTRL(0), 0x1111_0000)
    assert await rd(m, SM_PINCTRL(0)) == 0x1111_0000             # forwarded to the tile
    await wr(m, 0x094, 0x4001)                                   # IN_CFG: served by the wrapper
    assert await rd(m, 0x094) == 0x4001
    await wr(m, 0x090, 0x123456)                                 # OUT_TIMEOUT
    assert await rd(m, 0x090) == 0x123456
    assert int(dut.u_tlp_ingress.i_channel.value) == 0x40 and int(dut.u_tlp_ingress.i_en.value) == 1
    assert await rd(m, 0x098) & 0x3F == 0                        # IN_STATUS: queue empty


@cocotb.test(timeout_time=50, timeout_unit="ms")
async def test_random_1000(dut):
    m = await setup(dut)
    rng = random.Random(11)
    model = {}
    for off in TILE:
        model[off] = await rd(m, off)
    for off in LOCAL:
        model[off] = await rd(m, off)
    for _ in range(1000):
        if rng.random() < 0.6:
            off = rng.choice(TILE)
            mask = 0xFFFFFFFF
        else:
            off = rng.choice(list(LOCAL))
            mask = LOCAL[off]
        if rng.random() < 0.5:
            v = rng.getrandbits(32)
            await wr(m, off, v)
            model[off] = v & mask
        else:
            got = await rd(m, off)
            assert got & mask == model[off] & mask, f"0x{off:03X}: read 0x{got:08X}, model 0x{model[off]:08X}"
