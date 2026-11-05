# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""asp_router, all-PIO fabric ([SPEC-TLP-07]): CONTROL to the control plane, every other channel to
the PIO tile; egress completions first, a presented TLP held until accepted.

@verifies [SPEC-TLP-07] docs/DESIGN_SPECIFICATION.md
"""

import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, ReadOnly, RisingEdge

CH_CONTROL = 0x01


def make_tlp(channel: int, payload_val: int = 0x12345678) -> int:
    dw0 = (0x02 << 24) | (channel & 0xFF)
    return (dw0 << 480) | (payload_val << 320)


async def reset(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    dut.rst_n.value = 0
    for s in ("s_tlp_tvalid", "s_tlp_tdata", "s_cpl_tvalid", "s_cpl_tdata",
              "s_pio_stream_tvalid", "s_pio_stream_tdata"):
        getattr(dut, s).value = 0
    for s in ("m_ctrl_tready", "m_pio_tready", "m_egr_tready"):
        getattr(dut, s).value = 1
    await ClockCycles(dut.clk, 3)
    dut.rst_n.value = 1
    await ClockCycles(dut.clk, 2)


@cocotb.test(timeout_time=100, timeout_unit="us")
async def test_control_and_everything_else(dut):
    """Channel 0x01 to control; 0x00, 0x02, 0x05, 0x40, 0xFE to the PIO tile; never both."""
    await reset(dut)
    for ch in (CH_CONTROL, 0x00, 0x02, 0x05, 0x40, 0xFE):
        await FallingEdge(dut.clk)
        tlp = make_tlp(ch, 0xAABB0000 | ch)
        dut.s_tlp_tdata.value = tlp
        dut.s_tlp_tvalid.value = 1
        await ReadOnly()
        to_ctrl = ch == CH_CONTROL
        assert int(dut.m_ctrl_tvalid.value) == int(to_ctrl), hex(ch)
        assert int(dut.m_pio_tvalid.value) == int(not to_ctrl), hex(ch)
        out = dut.m_ctrl_tdata if to_ctrl else dut.m_pio_tdata
        assert int(out.value) == tlp
    await FallingEdge(dut.clk)
    dut.s_tlp_tvalid.value = 0


@cocotb.test(timeout_time=100, timeout_unit="us")
async def test_ingress_backpressure_per_destination(dut):
    await reset(dut)
    for ch, stall in ((CH_CONTROL, "m_ctrl_tready"), (0x40, "m_pio_tready")):
        await FallingEdge(dut.clk)
        getattr(dut, stall).value = 0
        dut.s_tlp_tdata.value = make_tlp(ch)
        dut.s_tlp_tvalid.value = 1
        await ReadOnly()
        assert int(dut.s_tlp_tready.value) == 0, f"stall of {stall} not seen"
        await FallingEdge(dut.clk)
        getattr(dut, stall).value = 1
        await ReadOnly()
        assert int(dut.s_tlp_tready.value) == 1
    await FallingEdge(dut.clk)
    dut.s_tlp_tvalid.value = 0


@cocotb.test(timeout_time=100, timeout_unit="us")
async def test_egress_completion_first(dut):
    await reset(dut)
    await FallingEdge(dut.clk)
    cpl, pio = make_tlp(CH_CONTROL, 0x11111111), make_tlp(0x40, 0x22222222)
    dut.s_cpl_tdata.value = cpl
    dut.s_cpl_tvalid.value = 1
    dut.s_pio_stream_tdata.value = pio
    dut.s_pio_stream_tvalid.value = 1
    await ReadOnly()
    assert int(dut.m_egr_tdata.value) == cpl and int(dut.s_cpl_tready.value) == 1
    assert int(dut.s_pio_stream_tready.value) == 0
    await FallingEdge(dut.clk)
    dut.s_cpl_tvalid.value = 0
    await ReadOnly()
    assert int(dut.m_egr_tdata.value) == pio and int(dut.s_pio_stream_tready.value) == 1


@cocotb.test(timeout_time=100, timeout_unit="us")
async def test_presented_tlp_held_until_accepted(dut):
    """A PIO TLP waiting on tready is not replaced by a completion that arrives meanwhile
    (the old fixed-priority mux switched tdata under tvalid)."""
    await reset(dut)
    await FallingEdge(dut.clk)
    pio = make_tlp(0x40, 0x22222222)
    cpl = make_tlp(CH_CONTROL, 0x11111111)
    dut.m_egr_tready.value = 0
    dut.s_pio_stream_tdata.value = pio
    dut.s_pio_stream_tvalid.value = 1
    await FallingEdge(dut.clk)                    # PIO presented, not accepted
    dut.s_cpl_tdata.value = cpl
    dut.s_cpl_tvalid.value = 1                 # completion arrives while it waits
    for _ in range(5):
        await ReadOnly()
        assert int(dut.m_egr_tvalid.value) == 1 and int(dut.m_egr_tdata.value) == pio, \
            "egress tdata changed before tready"
        await FallingEdge(dut.clk)
    dut.m_egr_tready.value = 1
    await ReadOnly()
    assert int(dut.s_pio_stream_tready.value) == 1          # PIO handshake completes first
    await FallingEdge(dut.clk)
    dut.s_pio_stream_tvalid.value = 0
    await ReadOnly()
    assert int(dut.m_egr_tdata.value) == cpl and int(dut.s_cpl_tready.value) == 1


@cocotb.test(timeout_time=2, timeout_unit="ms")
async def test_random_egress_every_tlp_once_in_order(dut):
    """Two sources with random valid gaps, random tready: every TLP leaves exactly once, each
    source in order, and egress data never changes while it waits."""
    await reset(dut)
    n = 300
    srcs = {"cpl": [make_tlp(CH_CONTROL, i) for i in range(n)],
            "pio": [make_tlp(0x40, 0x10000 + i) for i in range(n)]}
    got = []

    async def source(name, valid, data, ready):
        for t in srcs[name]:
            while random.random() < 0.3:
                await FallingEdge(dut.clk)
            data.value = t
            valid.value = 1
            while True:
                await RisingEdge(dut.clk)
                if int(ready.value):
                    break
            await FallingEdge(dut.clk)
            valid.value = 0

    async def sink():
        held = None
        while len(got) < 2 * n:
            await FallingEdge(dut.clk)
            dut.m_egr_tready.value = int(random.random() < 0.5)
            await ReadOnly()
            v, d, r = int(dut.m_egr_tvalid.value), int(dut.m_egr_tdata.value), int(dut.m_egr_tready.value)
            if held is not None:
                assert v and d == held, "egress changed before accepted"
            if v and r:
                got.append(d)
                held = None
            else:
                held = d if v else None

    cocotb.start_soon(source("cpl", dut.s_cpl_tvalid, dut.s_cpl_tdata, dut.s_cpl_tready))
    cocotb.start_soon(source("pio", dut.s_pio_stream_tvalid, dut.s_pio_stream_tdata, dut.s_pio_stream_tready))
    await sink()
    assert [t for t in got if (t >> 480) & 0xFF == CH_CONTROL] == srcs["cpl"]
    assert [t for t in got if (t >> 480) & 0xFF == 0x40] == srcs["pio"]
