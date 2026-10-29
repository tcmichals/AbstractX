# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
asp_pio_wrap: AbstractX's Wishbone B4 classic fabric to the pio2 tile's AXI4-Lite port ([SPEC-TLP-05]).

The master behaves like rtl/asp_wishbone_master.sv: it holds cyc/stb until ack and, on the ack edge,
either moves to the next word of a burst (stb stays high, new address) or ends the cycle.
Checks: single writes and reads, classic bursts with no idle clock between words, every access
answered exactly once (no extra ack, no lost write), 1000 random accesses against a model.
"""
# @verifies [SPEC-TLP-05] docs/DESIGN_SPECIFICATION.md

import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, ReadOnly, RisingEdge, Timer

BASE = 0x40000700
SM_SHIFTCTRL = lambda n: 0x100 + 0x20 * n + 0x08   # noqa: E731  plain read/write registers
SM_PINCTRL = lambda n: 0x100 + 0x20 * n + 0x0C     # noqa: E731
PLAIN = [f(n) for n in range(4) for f in (SM_SHIFTCTRL, SM_PINCTRL)]


async def setup(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    dut.rst_n.value = 0
    dut.i_sys_timestamp.value = 0
    dut.i_gpio.value = 0
    for s in ("wb_cyc_i", "wb_stb_i", "wb_we_i", "wb_adr_i", "wb_dat_i", "s_tlp_tvalid", "s_tlp_tdata"):
        getattr(dut, s).value = 0
    dut.m_tlp_tready.value = 1
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1
    await ClockCycles(dut.clk, 2)
    acks = {"n": 0}

    async def count_acks():
        while True:
            await RisingEdge(dut.clk)
            await ReadOnly()
            if int(dut.wb_ack_o.value):
                acks["n"] += 1
    cocotb.start_soon(count_acks())
    return acks


async def burst(dut, we, offsets, values=None, max_wait=64):
    """One classic cycle of len(offsets) words, like asp_wishbone_master. Returns read data."""
    out = []
    dut.wb_cyc_i.value = 1
    dut.wb_stb_i.value = 1
    dut.wb_we_i.value = we
    for i, off in enumerate(offsets):
        dut.wb_adr_i.value = BASE + off
        if we:
            dut.wb_dat_i.value = values[i]
        for _ in range(max_wait):
            await RisingEdge(dut.clk)
            await ReadOnly()
            if int(dut.wb_ack_o.value):
                break
        else:
            raise AssertionError(f"no ack for offset 0x{off:03X}")
        if not we:
            out.append(int(dut.wb_dat_o.value))
        await Timer(1, unit="ns")          # registered master: next word right after the ack edge
    dut.wb_cyc_i.value = 0
    dut.wb_stb_i.value = 0
    dut.wb_we_i.value = 0
    await RisingEdge(dut.clk)
    await Timer(1, unit="ns")
    return out


@cocotb.test(timeout_time=5, timeout_unit="ms")
async def test_single_and_burst(dut):
    acks = await setup(dut)
    await burst(dut, 1, [SM_PINCTRL(0)], [0x1111_0000])
    assert await burst(dut, 0, [SM_PINCTRL(0)]) == [0x1111_0000]
    offs = PLAIN[:6]
    vals = [0xA000_0000 + i for i in range(6)]
    await burst(dut, 1, offs, vals)                       # 6-word write burst, no idle between words
    assert await burst(dut, 0, offs) == vals              # 6-word read burst
    await ClockCycles(dut.clk, 20)
    assert acks["n"] == 1 + 1 + 6 + 6, f"{acks['n']} acks for 14 accesses (extra or lost answers)"


@cocotb.test(timeout_time=50, timeout_unit="ms")
async def test_random_1000(dut):
    acks = await setup(dut)
    rng = random.Random(11)
    model = {}
    for off in PLAIN:
        model[off] = (await burst(dut, 0, [off]))[0]
    done = len(PLAIN)
    while done < len(PLAIN) + 1000:
        n = rng.randint(1, 4)
        offs = [rng.choice(PLAIN) for _ in range(n)]
        if rng.random() < 0.5:
            vals = [rng.getrandbits(32) for _ in range(n)]
            await burst(dut, 1, offs, vals)
            for o, v in zip(offs, vals):
                model[o] = v
        else:
            got = await burst(dut, 0, offs)
            # a read burst sees the model as it is (all writes before it completed)
            assert got == [model[o] for o in offs], f"read {offs}: {got}"
        done += n
        if rng.random() < 0.3:
            await ClockCycles(dut.clk, rng.randint(1, 4))
    await ClockCycles(dut.clk, 20)
    assert acks["n"] == done, f"{acks['n']} acks for {done} accesses"
