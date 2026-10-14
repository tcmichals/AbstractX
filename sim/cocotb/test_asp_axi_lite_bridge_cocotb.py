# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Cocotb tests for rtl/asp_axi_lite_bridge.sv (Zynq PS AXI4-Lite <-> 64B TLP).
# Covers the CSRs, the egress buffer hand-off (including back-to-back packet
# reads), level-type IRQ with write-1-to-clear, and ingress TLP assembly.

import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, ReadOnly, RisingEdge

REG_CONTROL      = 0x00
REG_STATUS       = 0x04
REG_IRQ_STATUS   = 0x08
REG_IRQ_ENABLE   = 0x0C
REG_TLP_IN_PORT  = 0x10
REG_TLP_OUT_PORT = 0x14
REG_EGR_COUNT    = 0x18
REG_ING_FREE     = 0x1C
REG_HARDWARE_ID  = 0x40

HARDWARE_MAGIC = 0x41535036  # "ASP6"
FIFO_DEPTH = 128


async def reset(dut):
    cocotb.start_soon(Clock(dut.s_axi_aclk, 10, unit="ns").start())
    dut.s_axi_aresetn.value = 0
    for sig in ("s_axi_awaddr", "s_axi_awprot", "s_axi_awvalid", "s_axi_wdata",
                "s_axi_wvalid", "s_axi_araddr", "s_axi_arprot", "s_axi_arvalid",
                "s_egr_tdata", "s_egr_tvalid", "m_tlp_tready"):
        getattr(dut, sig).value = 0
    dut.s_axi_wstrb.value = 0xF
    dut.s_axi_bready.value = 1
    dut.s_axi_rready.value = 1
    await ClockCycles(dut.s_axi_aclk, 5)
    dut.s_axi_aresetn.value = 1
    await ClockCycles(dut.s_axi_aclk, 2)


async def wait_high(dut, sig, limit=50):
    """Wait until sig is high, checking the current timestep first.

    With bready/rready held high, bvalid/rvalid are one-cycle pulses that rise
    on the same edge the transaction executes, so the caller must not skip
    straight to the next edge.
    """
    await ReadOnly()
    if int(sig.value) == 1:
        return
    for _ in range(limit):
        await RisingEdge(dut.s_axi_aclk)
        await ReadOnly()
        if int(sig.value) == 1:
            return
    raise TimeoutError(f"{sig._name} never asserted")


async def axi_write(dut, addr, data):
    await RisingEdge(dut.s_axi_aclk)
    dut.s_axi_awaddr.value = addr
    dut.s_axi_wdata.value = data
    dut.s_axi_awvalid.value = 1
    dut.s_axi_wvalid.value = 1
    await wait_high(dut, dut.s_axi_awready)
    await RisingEdge(dut.s_axi_aclk)  # write executes on this edge
    dut.s_axi_awvalid.value = 0
    dut.s_axi_wvalid.value = 0
    await wait_high(dut, dut.s_axi_bvalid)
    await RisingEdge(dut.s_axi_aclk)


async def axi_read(dut, addr):
    await RisingEdge(dut.s_axi_aclk)
    dut.s_axi_araddr.value = addr
    dut.s_axi_arvalid.value = 1
    await wait_high(dut, dut.s_axi_arready)
    await RisingEdge(dut.s_axi_aclk)  # read executes on this edge
    dut.s_axi_arvalid.value = 0
    await wait_high(dut, dut.s_axi_rvalid)
    value = int(dut.s_axi_rdata.value)
    await RisingEdge(dut.s_axi_aclk)
    return value


async def push_egress(dut, tlp):
    await RisingEdge(dut.s_axi_aclk)
    dut.s_egr_tdata.value = tlp
    dut.s_egr_tvalid.value = 1
    await wait_high(dut, dut.s_egr_tready)
    await RisingEdge(dut.s_axi_aclk)
    dut.s_egr_tvalid.value = 0


def words(tlp):
    return [(tlp >> (32 * i)) & 0xFFFFFFFF for i in range(16)]


async def read_tlp(dut):
    return [await axi_read(dut, REG_TLP_OUT_PORT) for _ in range(16)]


@cocotb.test()
async def test_csr_reset_values(dut):
    await reset(dut)
    assert await axi_read(dut, REG_HARDWARE_ID) == HARDWARE_MAGIC
    assert await axi_read(dut, REG_CONTROL) == 1
    assert await axi_read(dut, REG_IRQ_ENABLE) == 1
    assert await axi_read(dut, REG_EGR_COUNT) == 0
    assert await axi_read(dut, REG_ING_FREE) == FIFO_DEPTH
    assert int(dut.irq_f2p.value) == 0


@cocotb.test()
async def test_egress_packet_irq_and_w1c(dut):
    await reset(dut)
    tlp = random.getrandbits(512)
    await push_egress(dut, tlp)
    await ClockCycles(dut.s_axi_aclk, 4)

    assert int(dut.irq_f2p.value) == 1, "IRQ must assert while a packet is pending"
    assert await axi_read(dut, REG_EGR_COUNT) == 1
    assert await read_tlp(dut) == words(tlp)
    assert await axi_read(dut, REG_EGR_COUNT) == 0
    assert await axi_read(dut, REG_STATUS) & 0x1 == 0

    # Status bit is latched until software clears it.
    assert await axi_read(dut, REG_IRQ_STATUS) & 0x1 == 1
    await axi_write(dut, REG_IRQ_STATUS, 0x1)
    assert await axi_read(dut, REG_IRQ_STATUS) & 0x1 == 0
    await ClockCycles(dut.s_axi_aclk, 2)
    assert int(dut.irq_f2p.value) == 0


@cocotb.test()
async def test_back_to_back_egress_packets(dut):
    """Reading packet N+1 immediately after N must return N+1, not zeros."""
    await reset(dut)
    tlps = [random.getrandbits(512) for _ in range(3)]
    for tlp in tlps:
        await push_egress(dut, tlp)
    await ClockCycles(dut.s_axi_aclk, 4)

    for tlp in tlps:
        assert await read_tlp(dut) == words(tlp)
    assert await axi_read(dut, REG_EGR_COUNT) == 0
    assert await axi_read(dut, REG_TLP_OUT_PORT) == 0, "empty port reads as zero"


@cocotb.test()
async def test_w1c_does_not_clear_pending_irq(dut):
    """IRQ is level-type: clearing while a packet is still queued re-asserts it."""
    await reset(dut)
    await push_egress(dut, random.getrandbits(512))
    await ClockCycles(dut.s_axi_aclk, 4)

    await axi_write(dut, REG_IRQ_STATUS, 0x1)
    assert await axi_read(dut, REG_IRQ_STATUS) & 0x1 == 1
    await read_tlp(dut)
    await axi_write(dut, REG_IRQ_STATUS, 0x1)
    assert await axi_read(dut, REG_IRQ_STATUS) & 0x1 == 0


@cocotb.test()
async def test_ingress_packet_assembly(dut):
    await reset(dut)
    tlp = random.getrandbits(512)
    for w in words(tlp):
        await axi_write(dut, REG_TLP_IN_PORT, w)

    assert await axi_read(dut, REG_ING_FREE) == FIFO_DEPTH - 1
    await ReadOnly()
    assert int(dut.m_tlp_tvalid.value) == 1
    assert int(dut.m_tlp_tdata.value) == tlp

    await RisingEdge(dut.s_axi_aclk)
    dut.m_tlp_tready.value = 1
    await RisingEdge(dut.s_axi_aclk)
    dut.m_tlp_tready.value = 0
    await ClockCycles(dut.s_axi_aclk, 2)
    assert int(dut.m_tlp_tvalid.value) == 0
    assert await axi_read(dut, REG_ING_FREE) == FIFO_DEPTH
