# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
#
# DShot600 pin timing at the clock the core was built for.
# Built with NUM_CHANNELS=1 (one pin to watch; channels are identical) and run
# per clock: SIM_PARAMS="-GNUM_CHANNELS=1 -GCLK_FREQ_HZ=27000000" and the 50 MHz default.
# @verifies [SPEC-FPGA-CLK-01] hw/tang_nano20k/SPECIFICATION.md

import cocotb
from cocotb.triggers import RisingEdge
from cocotb.utils import get_sim_time

from wb_util import reset, start_clock_at_param, wb_write

REG_ENABLE = 0x04
REG_THROTTLE0 = 0x10

# DShot600 nominal timing (ns) and allowed error: one clock + 3% (ESCs accept more)
BIT_NS, T0H_NS, T1H_NS = 1e9 / 600_000, 625.0, 1250.0


def within(value, nominal, clk_ns):
    return abs(value - nominal) <= clk_ns + 0.03 * nominal


async def wait_level(dut, level):
    """Sample bit 0 of o_motor_pins each clock until it equals `level`.
    (Edge triggers need a scalar; the output is a packed vector.)"""
    while (int(dut.o_motor_pins.value) & 1) != level:
        await RisingEdge(dut.clk)
    return get_sim_time("ns")


async def capture_frame(dut):
    """Edge times of one 16-bit frame: returns (rises, falls) in ns."""
    rises, falls = [], []
    await wait_level(dut, 0)
    for _ in range(16):
        rises.append(await wait_level(dut, 1))
        falls.append(await wait_level(dut, 0))
    return rises, falls


@cocotb.test()
async def test_dshot600_timing_and_frame(dut):
    hz, period_ps = start_clock_at_param(dut)
    clk_ns = period_ps / 1000
    await reset(dut)

    throttle = 1046  # 11-bit value; telemetry bit 0
    await wb_write(dut, REG_THROTTLE0, throttle)
    await wb_write(dut, REG_ENABLE, 0x1)

    rises, falls = await capture_frame(dut)

    packet12 = (throttle & 0x7FF) << 1
    crc = (packet12 ^ (packet12 >> 4) ^ (packet12 >> 8)) & 0xF
    expected = (packet12 << 4) | crc
    decoded = 0
    for i in range(16):
        high = falls[i] - rises[i]
        one = high > (T0H_NS + T1H_NS) / 2
        assert within(high, T1H_NS if one else T0H_NS, clk_ns), f"{hz} Hz bit {i}: high {high:.1f} ns"
        decoded = (decoded << 1) | int(one)
        if i < 15:
            period = rises[i + 1] - rises[i]
            assert within(period, BIT_NS, clk_ns), f"{hz} Hz bit {i}: period {period:.1f} ns, want {BIT_NS:.1f}"
    assert decoded == expected, f"frame 0x{decoded:04X} != 0x{expected:04X}"

    # The next frame starts 1 ms after this one (1 kHz frame rate)
    frame_period = await wait_level(dut, 1) - rises[0]
    dut._log.info(f"{hz} Hz: bit {rises[1]-rises[0]:.1f} ns, frame {frame_period/1e3:.2f} us")
    assert abs(frame_period - 1e6) <= 2 * clk_ns + 0.001 * 1e6, f"frame period {frame_period:.0f} ns, want 1 ms"
