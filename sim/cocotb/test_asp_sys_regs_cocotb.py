# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, ClockCycles

REG_SYS_ID_REV    = 0x40000000
REG_SYS_VENDOR_ID = 0x40000004
REG_SYS_SCRATCH   = 0x40000008
REG_SYS_LED_CTRL  = 0x4000000C
REG_SYS_TIME_LOW  = 0x40000010
REG_SYS_TIME_HIGH = 0x40000014

SYS_ID_REV_DEFAULT    = 0xABF10164
SYS_VENDOR_ID_DEFAULT = 0x19981ACC


async def reset(dut):
    dut.rst.value = 1
    dut.i_sys_timestamp.value = 0
    dut.wb_adr_i.value = 0
    dut.wb_dat_i.value = 0
    dut.wb_sel_i.value = 0
    dut.wb_we_i.value = 0
    dut.wb_cyc_i.value = 0
    dut.wb_stb_i.value = 0
    await ClockCycles(dut.clk, 5)
    dut.rst.value = 0
    await ClockCycles(dut.clk, 5)


async def wb_read(dut, addr):
    """Execute a single Wishbone read cycle and return the 32-bit result."""
    dut.wb_adr_i.value = addr
    dut.wb_we_i.value = 0
    dut.wb_sel_i.value = 0xF
    dut.wb_cyc_i.value = 1
    dut.wb_stb_i.value = 1

    for _ in range(10):
        await RisingEdge(dut.clk)
        if dut.wb_ack_o.value == 1:
            result = int(dut.wb_dat_o.value)
            dut.wb_cyc_i.value = 0
            dut.wb_stb_i.value = 0
            await RisingEdge(dut.clk)
            return result

    raise TimeoutError(f"Wishbone read at 0x{addr:08X} never acknowledged!")


async def wb_write(dut, addr, data, sel=0xF):
    """Execute a single Wishbone write cycle."""
    dut.wb_adr_i.value = addr
    dut.wb_dat_i.value = data
    dut.wb_sel_i.value = sel
    dut.wb_we_i.value = 1
    dut.wb_cyc_i.value = 1
    dut.wb_stb_i.value = 1

    for _ in range(10):
        await RisingEdge(dut.clk)
        if dut.wb_ack_o.value == 1:
            dut.wb_cyc_i.value = 0
            dut.wb_stb_i.value = 0
            dut.wb_we_i.value = 0
            await RisingEdge(dut.clk)
            return

    raise TimeoutError(f"Wishbone write at 0x{addr:08X} never acknowledged!")


@cocotb.test()
async def test_read_sys_ids(dut):
    """Verify REG_SYS_ID_REV and REG_SYS_VENDOR_ID return standard PCIe identifiers."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    id_rev = await wb_read(dut, REG_SYS_ID_REV)
    assert id_rev == SYS_ID_REV_DEFAULT, f"Expected 0x{SYS_ID_REV_DEFAULT:08X}, got 0x{id_rev:08X}"
    dut._log.info(f"REG_SYS_ID_REV = 0x{id_rev:08X} (Device 0xABF1, Rev 0x01, Arch 0x64) — verified.")

    vendor_id = await wb_read(dut, REG_SYS_VENDOR_ID)
    assert vendor_id == SYS_VENDOR_ID_DEFAULT, f"Expected 0x{SYS_VENDOR_ID_DEFAULT:08X}, got 0x{vendor_id:08X}"
    dut._log.info(f"REG_SYS_VENDOR_ID = 0x{vendor_id:08X} (Subsys 0x1998, Vendor 0x1ACC) — verified.")


@cocotb.test()
async def test_scratch_register_loopback(dut):
    """Write to scratch register at 0x08 and read it back."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    test_values = [0xDEADBEEF, 0x12345678, 0x00000000, 0xFFFFFFFF]
    for val in test_values:
        await wb_write(dut, REG_SYS_SCRATCH, val)
        result = await wb_read(dut, REG_SYS_SCRATCH)
        assert result == val, f"Scratch loopback failed: wrote 0x{val:08X}, read 0x{result:08X}"
        dut._log.info(f"Scratch loopback 0x{val:08X} — OK")


@cocotb.test()
async def test_led_control_register(dut):
    """Write to LED control register and verify active-low LED output bits."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    # Turn ON LED 1 (bit 1 = 0)
    await wb_write(dut, REG_SYS_LED_CTRL, 0x00000000)
    await ClockCycles(dut.clk, 2)
    assert int(dut.o_led_bits.value) == 0x00, f"Expected LEDs all ON (0x00), got 0x{int(dut.o_led_bits.value):02X}"

    # Turn OFF LEDs (0x3F)
    await wb_write(dut, REG_SYS_LED_CTRL, 0x0000003F)
    await ClockCycles(dut.clk, 2)
    assert int(dut.o_led_bits.value) == 0x3F, f"Expected LEDs all OFF (0x3F), got 0x{int(dut.o_led_bits.value):02X}"
    dut._log.info("[SUCCESS] LED control output bits verified!")


@cocotb.test()
async def test_atomic_timestamp_latching(dut):
    """Reading TIME_LOW must atomically latch the 64-bit high word into TIME_HIGH shadow register."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    # Set 64-bit nanosecond counter
    dut.i_sys_timestamp.value = 0x0000000A_12345678
    await ClockCycles(dut.clk, 2)

    # 1. Read TIME_LOW: latches high bits (0x0000000A)
    time_low = await wb_read(dut, REG_SYS_TIME_LOW)
    assert time_low == 0x12345678, f"Expected low word 0x12345678, got 0x{time_low:08X}"

    # 2. Advance timestamp past rollover before reading TIME_HIGH
    dut.i_sys_timestamp.value = 0x0000000B_00000010
    await ClockCycles(dut.clk, 2)

    # 3. Read TIME_HIGH: must return latched shadow 0x0000000A, NOT live 0x0000000B!
    time_high = await wb_read(dut, REG_SYS_TIME_HIGH)
    assert time_high == 0x0000000A, f"Atomic latch failed! Expected 0x0000000A, got 0x{time_high:08X}"
    dut._log.info("[SUCCESS] 64-bit Atomic Shadow Timestamp latching verified across rollover!")

