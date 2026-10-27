# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""ESC 4-way passthrough wire as a PIO program ([SPEC-PIO-13]) on asp_pio_tile.

Loads rtl/pio/programs/esc_serial.pio, sends bytes as 8N1 at 19200 on the motor wire, checks the
one-bit guard before the line is released, receives a modelled ESC's answer (also with its clock
2 % off), checks that nothing sent is reported back, and drives the bootloader break through
SMx_INSTR as the host does. Reference behaviour: rt-fc-offloader wb_esc_uart / wb_serial_dshot_mux.

@verifies [SPEC-PIO-13] rtl/pio/SPECIFICATION.md
"""

import os
import sys
from pathlib import Path

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, RisingEdge, Timer
from cocotb.utils import get_sim_time

from test_asp_pio_cocotb import (wb_write, wb_read, REG_CTRL, IRAM_BASE, SM_CLKDIV,
                                 SM_EXECCTRL, SM_SHIFTCTRL, SM_PINCTRL, SM_TXF, SM_RXF)

sys.path.insert(0, str(Path(os.environ.get("ABSTRACTX_ROOT", Path(__file__).resolve().parents[2])) / "tools"))
from pio_programs import load_program  # noqa: E402

REG_FIFO_STAT = 0x08
SM_INSTR = lambda n: 0x100 + 0x20 * n + 0x18  # noqa: E731
PIN = 2                                   # any tile pin: the program only uses PINCTRL bases
BAUD = 19200
BIT_NS = 1e9 / BAUD
FABRIC_HZ = 27_000_000


class Pad:
    def __init__(self, dut):
        self.dut = dut
        self.esc = None

    async def run(self):
        while True:
            await FallingEdge(self.dut.clk)
            if (int(self.dut.o_gpio_pindir.value) >> PIN) & 1:
                level = (int(self.dut.o_gpio_out.value) >> PIN) & 1
            else:
                level = 1 if self.esc is None else self.esc
            self.dut.i_gpio.value = level << PIN

    def level(self):
        return (int(self.dut.i_gpio.value) >> PIN) & 1

    def driven_by_fpga(self):
        return (int(self.dut.o_gpio_pindir.value) >> PIN) & 1


async def setup(dut):
    period_ps = 2 * round(1e12 / FABRIC_HZ / 2)
    cocotb.start_soon(Clock(dut.clk, period_ps, unit="ps").start())
    dut.rst_n.value = 0
    dut.i_sys_timestamp.value = 0
    dut.i_gpio.value = 1 << PIN
    for sig in ("wb_cyc_i", "wb_stb_i", "wb_we_i", "wb_adr_i", "wb_dat_i"):
        getattr(dut, sig).value = 0
    dut.m_tlp_tready.value = 1
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1
    await ClockCycles(dut.clk, 2)
    pad = Pad(dut)
    cocotb.start_soon(pad.run())

    p = load_program("esc_serial")
    for i, w in enumerate(p["instructions"]):
        await wb_write(dut, IRAM_BASE + 4 * i, w)
    div = FABRIC_HZ / (BAUD * 32)
    div_int, div_frac = int(div), int(round((div - int(div)) * 256)) & 0xFF
    await wb_write(dut, SM_CLKDIV(0), (div_int << 16) | (div_frac << 8))
    await wb_write(dut, SM_EXECCTRL(0), (PIN << 24) | (p["wrap"] << 12) | (p["wrap_target"] << 7))
    await wb_write(dut, SM_SHIFTCTRL(0), (1 << 18) | (1 << 19))   # in/out shift right
    await wb_write(dut, SM_PINCTRL(0), (PIN << 15) | (1 << 26) | (PIN << 5) | (1 << 20) | PIN)
    await wb_write(dut, REG_CTRL, 1 << 4)
    await wb_write(dut, SM_INSTR(0), 0x0000)                       # jmp 0
    await wb_write(dut, REG_CTRL, 1)
    return pad


async def uart_rx_line(dut, pad, n):
    """Decode n 8N1 bytes from the line by timing (start edge, then bit centres)."""
    out = []
    for _ in range(n):
        while pad.level() != 1:
            await RisingEdge(dut.clk)
        while pad.level() != 0:
            await RisingEdge(dut.clk)
        t0 = get_sim_time("ns")
        b = 0
        for i in range(8):
            await Timer(round(t0 + (1.5 + i) * BIT_NS - get_sim_time("ns")), unit="ns")
            b |= pad.level() << i
        await Timer(round(t0 + 9.5 * BIT_NS - get_sim_time("ns")), unit="ns")
        assert pad.level() == 1, "stop bit not high"
        out.append(b)
    return out, get_sim_time("ns")


async def esc_send(pad, data, bit_scale=1.0):
    bit = round(BIT_NS * bit_scale)
    for b in data:
        for level in [0] + [(b >> i) & 1 for i in range(8)] + [1]:
            pad.esc = level
            await Timer(bit, unit="ns")
    pad.esc = None


async def rx_words(dut, n, timeout_us):
    words = []
    t_end = get_sim_time("ns") + timeout_us * 1000
    while len(words) < n and get_sim_time("ns") < t_end:
        if not (await wb_read(dut, REG_FIFO_STAT) >> 28) & 1:
            words.append(await wb_read(dut, SM_RXF(0)) >> 24)
        else:
            await Timer(5000, unit="ns")
    return words


@cocotb.test()
async def test_send_guard_release_and_receive(dut):
    pad = await setup(dut)
    await ClockCycles(dut.clk, 50)
    assert not pad.driven_by_fpga(), "the wire must be an input while idle"

    sent = [0x00, 0x55, 0xFF, 0x1A]
    for b in sent:                                # 4 words fit the TX FIFO
        await wb_write(dut, SM_TXF(0), b)
    got, t_last = await uart_rx_line(dut, pad, len(sent))
    assert got == sent, [hex(b) for b in got]

    # guard: still driven high for about one bit after the last stop bit, then released
    stop_end = t_last + 0.5 * BIT_NS
    await Timer(round(stop_end + 0.5 * BIT_NS - get_sim_time("ns")), unit="ns")
    assert pad.driven_by_fpga() and pad.level() == 1, "released before the guard bit"
    await Timer(round(1.5 * BIT_NS), unit="ns")
    assert not pad.driven_by_fpga(), "line not released after the guard bit"

    # no echo: our own bytes are not reported
    assert await rx_words(dut, 1, 50) == []

    # the ESC answers, nominal clock and 2 % off
    for scale, data in ((1.0, [0x31, 0xC4, 0x00]), (1.02, [0xA5, 0xFF]), (0.98, [0x7E])):
        await esc_send(pad, data, scale)
        assert await rx_words(dut, len(data), 200) == data, f"ESC clock x{scale}"


@cocotb.test()
async def test_glitch_is_not_a_byte(dut):
    pad = await setup(dut)
    await ClockCycles(dut.clk, 50)
    pad.esc = 0
    await Timer(round(BIT_NS / 8), unit="ns")    # 1/8 bit low pulse
    pad.esc = None
    assert await rx_words(dut, 1, 1000) == []


@cocotb.test()
async def test_host_break(dut):
    """The host forces the wire low for the bootloader break, then hands it back."""
    pad = await setup(dut)
    await wb_write(dut, REG_CTRL, 0)                                   # stop the SM
    await wb_write(dut, SM_INSTR(0), 0xE000)                           # set pins, 0
    await wb_write(dut, SM_INSTR(0), 0xE081)                           # set pindirs, 1
    await Timer(1000, unit="ns")
    assert pad.driven_by_fpga() and pad.level() == 0
    await wb_write(dut, SM_INSTR(0), 0xE001)                           # set pins, 1
    await wb_write(dut, SM_INSTR(0), 0xE080)                           # set pindirs, 0
    await wb_write(dut, REG_CTRL, 1)
    await Timer(1000, unit="ns")
    assert not pad.driven_by_fpga()
    await wb_write(dut, SM_TXF(0), 0x42)
    got, _ = await uart_rx_line(dut, pad, 1)
    assert got == [0x42]
