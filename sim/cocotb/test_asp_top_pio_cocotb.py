# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""asp_top, all-PIO ([SPEC-TLP-07]), driven over Dual-SPI exactly like the host.

1. After reset every output port sits at its idle level (motors 0, UART TX 1, IMU CS_n 1).
2. A control MemRead of SYS_ID returns 0xABF10164 (link, asp_router, asp_wishbone_master, sys regs).
3. A control MemRead of an address no slave decodes (old IMU core 0x40000100) completes with 0.
4. A PIO program loaded over control TLPs takes motor 0; data TLPs on a PIO channel (not CONTROL)
   reach the tile through the router and the program drives the pin from them.

@verifies [SPEC-TLP-07] [SPEC-TLP-05] docs/DESIGN_SPECIFICATION.md
"""

import struct
import zlib

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, RisingEdge

PIO = 0x40000700
CH_CONTROL = 0x01
CH_PIO_IN = 0x40


def pack_tlp(tlp_type, flags, tag, channel, addr, len_dw, seq, ts, payload=b""):
    padded = payload[:40].ljust(40, b"\x00")
    data = struct.pack(">BBBBIHHQ40s", tlp_type, flags, tag, channel, addr, len_dw, seq, ts, padded)
    return data + zlib.crc32(data).to_bytes(4, "big")


def unpack_tlp(data):
    return struct.unpack(">BBBBIHHQ40sI", data)


async def sclk_pulse(dut):
    await ClockCycles(dut.clk, 2)
    dut.spi_sclk.value = 1
    await ClockCycles(dut.clk, 4)
    dut.spi_sclk.value = 0
    await ClockCycles(dut.clk, 2)


async def send_tlp(dut, tlp):
    """Command 0xA1 + 64 bytes, Dual-SPI (2 bits per SCLK), as test_asp_spi_frontend does."""
    dut.spi_cs_n.value = 0
    await ClockCycles(dut.clk, 10)
    for b in range(7, -1, -1):
        dut.spi_io0.value = (0xA1 >> b) & 1
        dut.spi_io1.value = 0
        await sclk_pulse(dut)
    for byte_val in tlp:
        for pair in range(3, -1, -1):
            v = (byte_val >> (2 * pair)) & 3
            dut.spi_io0.value = v & 1
            dut.spi_io1.value = v >> 1
            await sclk_pulse(dut)
    await ClockCycles(dut.clk, 10)
    dut.spi_cs_n.value = 1
    await ClockCycles(dut.clk, 20)


async def read_tlp(dut):
    dut.spi_cs_n.value = 0
    await ClockCycles(dut.clk, 10)
    for b in range(7, -1, -1):
        dut.spi_io0.value = (0xA2 >> b) & 1
        dut.spi_io1.value = 0
        await sclk_pulse(dut)
    rx = bytearray()
    for _ in range(64):
        val = 0
        for _ in range(4):
            await ClockCycles(dut.clk, 2)
            dut.spi_sclk.value = 1
            await ClockCycles(dut.clk, 4)
            dut.spi_sclk.value = 0
            await ClockCycles(dut.clk, 1)
            val = (val << 2) | (int(dut.spi_io1_o.value) << 1) | int(dut.spi_io0_o.value)
            await ClockCycles(dut.clk, 1)
        rx.append(val)
    await ClockCycles(dut.clk, 10)
    dut.spi_cs_n.value = 1
    await ClockCycles(dut.clk, 20)
    return bytes(rx)


async def mem_write(dut, addr, value, seq=0):
    await send_tlp(dut, pack_tlp(0x02, 0, 0, CH_CONTROL, addr, 1, seq, 0, struct.pack(">I", value)))


async def mem_read(dut, addr, tag=0x5A):
    await send_tlp(dut, pack_tlp(0x01, 0, tag, CH_CONTROL, addr, 1, 0, 0))
    for _ in range(5000):
        if int(dut.o_int_req.value):
            break
        await RisingEdge(dut.clk)
    else:
        raise AssertionError(f"no completion for MemRead 0x{addr:08X} (doorbell never rose)")
    f = unpack_tlp(await read_tlp(dut))
    assert f[0] == 0x03 and f[2] == tag, f"not our completion: type 0x{f[0]:02X} tag 0x{f[2]:02X}"
    return struct.unpack(">I", f[8][:4])[0]


async def reset(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    dut.rst_n.value = 0
    dut.spi_cs_n.value = 1
    dut.spi_sclk.value = 0
    dut.spi_io0.value = 0
    dut.spi_io1.value = 0
    dut.imu_miso.value = 0
    dut.imu_int_i.value = 0
    dut.i_uart_rx.value = 1
    dut.i_pwm_pins.value = 0
    await ClockCycles(dut.clk, 10)
    dut.rst_n.value = 1
    await ClockCycles(dut.clk, 10)


@cocotb.test(timeout_time=20, timeout_unit="ms")
async def test_idle_levels_and_control_path(dut):
    await reset(dut)
    assert int(dut.o_motor_pins.value) == 0, "motors must idle low"
    assert int(dut.o_uart_tx.value) == 1, "UART TX must idle high"
    assert int(dut.imu_cs_n.value) == 1, "IMU CS_n must idle deselected"
    assert int(dut.imu_sclk.value) == 0 and int(dut.o_neopixel_pin.value) == 0
    assert await mem_read(dut, 0x40000000) == 0xABF10164
    assert await mem_read(dut, 0x40000100, tag=0x33) == 0, "unmapped address must answer 0"


@cocotb.test(timeout_time=40, timeout_unit="ms")
async def test_pio_program_drives_motor_from_pio_channel(dut):
    await reset(dut)
    # SM0: set pindirs, 1 ; pull block ; out pins, 1 ; jmp 1   (OUT/SET base GPIO0 = motor 0)
    prog = [0xE081, 0x80A0, 0x6001, 0x0001]
    for i, w in enumerate(prog):
        await mem_write(dut, PIO + 0x10 + 4 * i, w)
    await mem_write(dut, PIO + 0x104, (3 << 12))                 # EXECCTRL: wrap 0..3
    await mem_write(dut, PIO + 0x10C, (1 << 20) | (1 << 26))     # PINCTRL: OUT_COUNT 1, SET_COUNT 1, base 0
    await mem_write(dut, PIO + 0x108, 1 << 19)                   # SHIFTCTRL: OUT_SHIFTDIR right (bit 0 first)
    await mem_write(dut, PIO + 0x100, 1 << 16)                   # CLKDIV 1.0
    await mem_write(dut, PIO + 0x094, 1 | (CH_PIO_IN << 8))      # IN_CFG: ingress on channel 0x40
    await mem_write(dut, PIO + 0x000, 0x1)                       # CTRL: SM0 on
    await ClockCycles(dut.clk, 50)
    assert int(dut.o_motor_pins.value) & 1 == 0

    for level in (1, 0, 1):
        # Data TLP on the PIO channel: flags[3:0] = SM mask, one word
        await send_tlp(dut, pack_tlp(0x02, 0x01, 0, CH_PIO_IN, PIO, 1, 0, 0, struct.pack(">I", level)))
        await ClockCycles(dut.clk, 100)
        assert int(dut.o_motor_pins.value) & 1 == level, f"motor 0 should be {level}"
    assert int(dut.o_motor_pins.value) >> 1 == 0, "motors 1..3 stay idle"
