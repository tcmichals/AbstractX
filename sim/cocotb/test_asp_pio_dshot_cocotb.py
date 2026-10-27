# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bidirectional DShot600 as a PIO program ([SPEC-PIO-12]) on asp_pio_tile.

Loads rtl/pio/programs/dshot_bidir.pio (pioasm image) the way the host will, checks the frame
timing against the asp_dshot_core criterion (one fabric clock + 3 %) at 12, 27 and 50 MHz, then
models an ESC answering on the same wire and decodes the captured samples with tools/dshot_ref.py.

@verifies [SPEC-PIO-12] rtl/pio/SPECIFICATION.md
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
import dshot_ref  # noqa: E402

REG_FIFO_STAT = 0x08
SM_INSTR = lambda n: 0x100 + 0x20 * n + 0x18  # noqa: E731
PIN = 0                                   # tile pin of the motor (board pin map in real use)
SM_HZ = 600_000 * 16                      # 16 SM cycles per DShot600 bit
BIT_NS, T0H_NS, T1H_NS = 1e9 / 600_000, 625.0, 1250.0
REPLY_BIT_NS = 1e9 / 750_000              # eRPM reply: 5/4 of the DShot rate
SAMPLE_NS = 5 * 1e9 / SM_HZ


def within(value, nominal, clk_ns):
    return abs(value - nominal) <= clk_ns + 0.03 * nominal


class Pad:
    """Motor pad: driven by the tile when pindir is 1, else by the ESC, else pulled up."""

    def __init__(self, dut):
        self.dut = dut
        self.esc = None                   # None = ESC not driving

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


async def setup(dut, hz):
    period_ps = 2 * round(1e12 / hz / 2)              # cocotb needs an even period
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

    p = load_program("dshot_bidir")
    for i, w in enumerate(p["instructions"]):
        await wb_write(dut, IRAM_BASE + 4 * i, w)
    div = hz / SM_HZ                                       # host computes this from the board clock
    div_int, div_frac = int(div), int(round((div - int(div)) * 256))
    if div_frac == 256:
        div_int, div_frac = div_int + 1, 0
    await wb_write(dut, SM_CLKDIV(0), (div_int << 16) | (div_frac << 8))
    await wb_write(dut, SM_EXECCTRL(0), (PIN << 24) | (p["wrap"] << 12) | (p["wrap_target"] << 7))
    await wb_write(dut, SM_SHIFTCTRL(0), 1 << 16)          # autopush at 32, shift left
    await wb_write(dut, SM_PINCTRL(0), (PIN << 15) | (1 << 26) | (PIN << 5) | (1 << 20) | PIN)
    await wb_write(dut, REG_CTRL, 1 << 4)                  # restart
    await wb_write(dut, SM_INSTR(0), 0x0000)               # jmp 0
    await wb_write(dut, REG_CTRL, 1)
    return pad, period_ps / 1000


async def capture_frame(dut, pad, clk_ns):
    """Time one inverted frame: returns the 16 bits and checks low widths and bit periods."""
    falls, rises = [], []
    for _ in range(16):
        while pad.level() != 0:
            await RisingEdge(dut.clk)
        falls.append(get_sim_time("ns"))
        while pad.level() != 1:
            await RisingEdge(dut.clk)
        rises.append(get_sim_time("ns"))
    bits = 0
    for i in range(16):
        low = rises[i] - falls[i]
        one = low > (T0H_NS + T1H_NS) / 2
        assert within(low, T1H_NS if one else T0H_NS, clk_ns), f"bit {i}: low {low:.1f} ns"
        if i < 15:
            assert within(falls[i + 1] - falls[i], BIT_NS, clk_ns), f"bit {i}: period {falls[i+1]-falls[i]:.1f} ns"
        bits = (bits << 1) | int(one)
    return bits


async def esc_reply(pad, payload, bit_scale=1.0):
    """ESC: 30 us after the frame, drive the 21 reply bits, then release the wire."""
    await Timer(30_000, unit="ns")
    for b in dshot_ref.reply_line_bits(payload):
        pad.esc = b
        await Timer(round(REPLY_BIT_NS * bit_scale), unit="ns")
    pad.esc = None


async def read_rx(dut, n, timeout_us=200):
    words = []
    t_end = get_sim_time("ns") + timeout_us * 1000
    while len(words) < n and get_sim_time("ns") < t_end:
        if not (await wb_read(dut, REG_FIFO_STAT) >> 28) & 1:
            words.append(await wb_read(dut, SM_RXF(0)))
        else:
            await Timer(1000, unit="ns")
    return words


async def one_cycle(dut, pad, clk_ns, value, period_us, bit_scale=1.0):
    frame = dshot_ref.dshot_frame(value, telemetry=False, bidir=True)
    await wb_write(dut, SM_TXF(0), frame)
    bits = await capture_frame(dut, pad, clk_ns)
    assert bits == frame, f"frame 0x{bits:04X} != 0x{frame:04X}"
    payload = dshot_ref.erpm_payload(period_us)
    cocotb.start_soon(esc_reply(pad, payload, bit_scale))
    words = await read_rx(dut, 2)
    assert len(words) == 2, f"no capture words: {words}"
    got = dshot_ref.decode_samples(dshot_ref.samples_from_words(words), REPLY_BIT_NS * bit_scale / SAMPLE_NS)
    assert got == payload >> 4, f"decoded {got} != {payload >> 4} (words {[hex(w) for w in words]})"


async def dshot_at(dut, hz):
    pad, clk_ns = await setup(dut, hz)
    await one_cycle(dut, pad, clk_ns, 1046, 2000)
    await one_cycle(dut, pad, clk_ns, 48, 40000, bit_scale=1.03)     # ESC clock 3 % slow
    await one_cycle(dut, pad, clk_ns, 2047, 150, bit_scale=0.97)     # 3 % fast


@cocotb.test()
async def test_dshot600_bidir_27mhz(dut):
    await dshot_at(dut, 27_000_000)


@cocotb.test()
async def test_dshot600_bidir_50mhz(dut):
    await dshot_at(dut, 50_000_000)


@cocotb.test()
async def test_dshot600_bidir_12mhz(dut):
    await dshot_at(dut, 12_000_000)


@cocotb.test()
async def test_no_reply_gives_two_zero_words(dut):
    """An ESC that does not answer: two 0 words, then the next frame goes out normally."""
    pad, clk_ns = await setup(dut, 27_000_000)
    frame = dshot_ref.dshot_frame(100)
    await wb_write(dut, SM_TXF(0), frame)
    assert await capture_frame(dut, pad, clk_ns) == frame
    assert await read_rx(dut, 2) == [0, 0]
    await wb_write(dut, SM_TXF(0), frame)
    assert await capture_frame(dut, pad, clk_ns) == frame


@cocotb.test()
async def test_no_frame_no_signal(dut):
    """Nothing in the TX FIFO: no edges on the wire (the ESC sees signal loss)."""
    pad, clk_ns = await setup(dut, 27_000_000)
    start = get_sim_time("ns")
    while get_sim_time("ns") - start < 200_000:
        await ClockCycles(dut.clk, 50)
        assert pad.level() == 1, "the program drove a frame without data"
