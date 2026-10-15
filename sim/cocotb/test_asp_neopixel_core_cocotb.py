# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
#
# WS2812 pin timing at the clock the core was built for.
# Run per clock: SIM_PARAMS=-GCLK_FREQ_HZ=27000000 (and default 50 MHz).
# @verifies [SPEC-FPGA-CLK-01] hw/tang_nano20k/SPECIFICATION.md

import cocotb
from cocotb.triggers import FallingEdge, RisingEdge
from cocotb.utils import get_sim_time

from wb_util import reset, start_clock_at_param, wb_write

REG_LED0 = 0x04

# WS2812 nominal timing (ns); datasheet tolerance is +-150 ns
BIT_NS, T0H_NS, T1H_NS, RESET_MIN_NS = 1250.0, 350.0, 700.0, 50_000.0
TOL_NS = 150.0


@cocotb.test()
async def test_ws2812_timing_and_data(dut):
    hz, period_ps = start_clock_at_param(dut)
    await reset(dut)
    grb = 0xA5C33C
    await wb_write(dut, REG_LED0, grb)

    pin = dut.o_neopixel_pin
    # Synchronise to the start of a frame: the first rise after a >= 50 us low
    await FallingEdge(pin)
    while True:
        low_start = get_sim_time("ns")
        await RisingEdge(pin)
        if get_sim_time("ns") - low_start >= RESET_MIN_NS:
            break
        await FallingEdge(pin)

    rise = get_sim_time("ns")
    decoded = 0
    periods = []
    for i in range(24):
        await FallingEdge(pin)
        high = get_sim_time("ns") - rise
        one = high > (T0H_NS + T1H_NS) / 2
        assert abs(high - (T1H_NS if one else T0H_NS)) <= TOL_NS, f"{hz} Hz bit {i}: high {high:.1f} ns"
        decoded = (decoded << 1) | int(one)
        await RisingEdge(pin)
        now = get_sim_time("ns")
        periods.append(now - rise)
        rise = now
    # After 24 bits the line stays low for the reset latch, then repeats
    for p in periods[:23]:
        assert abs(p - BIT_NS) <= TOL_NS, f"{hz} Hz: bit period {p:.1f} ns"
    assert periods[23] >= RESET_MIN_NS, f"{hz} Hz: reset latch {periods[23]:.0f} ns < 50 us"
    assert decoded == grb, f"GRB 0x{decoded:06X} != 0x{grb:06X}"
    dut._log.info(f"{hz} Hz: bit {periods[0]:.1f} ns, latch {periods[23]/1e3:.1f} us")
