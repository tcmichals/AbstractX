# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared helpers for cores with the simple Wishbone port used by DShot/NeoPixel.
@verifies [SPEC-FPGA-CLK-01] hw/tang_nano20k/SPECIFICATION.md
"""

from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, RisingEdge


def start_clock_at_param(dut):
    """Drive clk at the frequency the core was built for (CLK_FREQ_HZ parameter)."""
    hz = int(dut.CLK_FREQ_HZ.value)
    period_ps = 2 * round(1e12 / hz / 2)  # cocotb needs an even period
    Clock(dut.clk, period_ps, unit="ps").start()
    return hz, period_ps


async def wb_write(dut, addr, data):
    dut.wb_addr.value = addr
    dut.wb_data_i.value = data
    dut.wb_we.value = 1
    dut.wb_cyc.value = 1
    dut.wb_stb.value = 1
    for _ in range(20):
        await RisingEdge(dut.clk)
        if int(dut.wb_ack.value) == 1:
            break
    else:
        raise TimeoutError(f"no ack for write to 0x{addr:08X}")
    dut.wb_cyc.value = 0
    dut.wb_stb.value = 0
    dut.wb_we.value = 0
    await RisingEdge(dut.clk)


async def reset(dut):
    dut.rst_n.value = 0
    dut.wb_cyc.value = 0
    dut.wb_stb.value = 0
    dut.wb_we.value = 0
    dut.wb_addr.value = 0
    dut.wb_data_i.value = 0
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1
    await ClockCycles(dut.clk, 5)
