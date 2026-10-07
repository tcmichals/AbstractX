# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Focused regression tests for the PL IMU SPI and Auto-DMA engine."""
# @impl [SPEC-ZYNQ-IMU-POC-07] sim/cocotb/test_asp_imu_auto_dma_cocotb.py

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import (
    ClockCycles,
    FallingEdge,
    RisingEdge,
    Timer,
    with_timeout,
)
from cocotb.utils import get_sim_time


IMU_BASE = 0x40000100
REG_CTRL = 0x00
REG_ADDR = 0x04
REG_LEN = 0x08
REG_RDATA = 0x10
REG_STATUS = 0x1C
REG_SPI_HALF_PERIOD = 0x38
REG_DRDY_OVERRUN_COUNT = 0x3C


async def wb_write(dut, offset, value):
    dut.wb_adr_i.value = IMU_BASE + offset
    dut.wb_dat_i.value = value
    dut.wb_we_i.value = 1
    dut.wb_cyc_i.value = 1
    dut.wb_stb_i.value = 1
    await RisingEdge(dut.clk)
    await Timer(1, unit="ns")
    assert int(dut.wb_ack_o.value) == 1
    dut.wb_cyc_i.value = 0
    dut.wb_stb_i.value = 0
    await RisingEdge(dut.clk)
    await Timer(1, unit="ns")


async def wb_read(dut, offset):
    dut.wb_adr_i.value = IMU_BASE + offset
    dut.wb_we_i.value = 0
    dut.wb_cyc_i.value = 1
    dut.wb_stb_i.value = 1
    await RisingEdge(dut.clk)
    await Timer(1, unit="ns")
    assert int(dut.wb_ack_o.value) == 1
    value = int(dut.wb_dat_o.value)
    dut.wb_cyc_i.value = 0
    dut.wb_stb_i.value = 0
    await RisingEdge(dut.clk)
    await Timer(1, unit="ns")
    return value


async def pulse_drdy(dut):
    dut.i_imu_int.value = 1
    await ClockCycles(dut.clk, 4)
    dut.i_imu_int.value = 0
    await ClockCycles(dut.clk, 4)


def tlp_bytes(dut):
    return int(dut.m_imu_stream_tdata.value).to_bytes(64, "big")


@cocotb.test()
async def direct_spi_and_auto_dma_contract(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    dut.rst_n.value = 0
    dut.i_sys_timestamp.value = 0
    dut.i_imu_int.value = 0
    dut.i_imu_miso.value = 1
    dut.wb_cyc_i.value = 0
    dut.wb_stb_i.value = 0
    dut.wb_we_i.value = 0
    dut.wb_adr_i.value = 0
    dut.wb_dat_i.value = 0
    dut.m_imu_stream_tready.value = 0
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1
    await ClockCycles(dut.clk, 2)

    assert await wb_read(dut, REG_SPI_HALF_PERIOD) == 4
    assert await wb_read(dut, REG_ADDR) == 0x1D
    assert await wb_read(dut, REG_LEN) == 14
    assert await wb_read(dut, REG_STATUS) & 0x0F == 0
    await wb_write(dut, REG_SPI_HALF_PERIOD, 2)
    assert await wb_read(dut, REG_SPI_HALF_PERIOD) == 2
    await wb_write(dut, REG_SPI_HALF_PERIOD, 1)
    assert await wb_read(dut, REG_SPI_HALF_PERIOD) == 2

    await wb_write(dut, REG_STATUS, 0x02)
    await wb_write(dut, REG_ADDR, 0x75)
    await wb_write(dut, REG_LEN, 1)
    await wb_write(dut, REG_CTRL, 0x06)
    await FallingEdge(dut.o_imu_cs_n)
    await Timer(1, unit="ns")
    assert int(dut.o_imu_sclk.value) == 0
    assert int(dut.o_imu_mosi.value) == 1
    await RisingEdge(dut.o_imu_sclk)
    rise_time = get_sim_time(unit="ns")
    await FallingEdge(dut.o_imu_sclk)
    assert get_sim_time(unit="ns") - rise_time == 20

    for _ in range(200):
        status = await wb_read(dut, REG_STATUS)
        if status & 0x02:
            break
    else:
        raise AssertionError("direct SPI operation did not assert done")
    assert not status & 0x09
    assert await wb_read(dut, REG_RDATA) & 0xFF == 0xFF
    assert int(dut.o_imu_cs_n.value) == 1

    await wb_write(dut, REG_ADDR, 0x1D)
    await wb_write(dut, REG_LEN, 14)
    await wb_write(dut, REG_CTRL, 0x05)
    for _ in range(4):
        await RisingEdge(dut.clk)
        if await wb_read(dut, REG_STATUS) & 0x04:
            break
    else:
        raise AssertionError("Auto-DMA did not become active")

    await pulse_drdy(dut)
    await ClockCycles(dut.clk, 12)
    await pulse_drdy(dut)
    await with_timeout(RisingEdge(dut.m_imu_stream_tvalid), 20, "us")
    first_frame = tlp_bytes(dut)
    assert first_frame[0:4] == bytes((0x10, 0, 0, 2))
    assert int.from_bytes(first_frame[10:12], "big") == 0
    assert first_frame[20:34] == bytes([0xFF] * 14)
    assert first_frame[34:36] == b"\x00\x00"
    await ClockCycles(dut.clk, 5)
    assert int(dut.m_imu_stream_tvalid.value) == 1
    assert tlp_bytes(dut) == first_frame

    dut.m_imu_stream_tready.value = 1
    await RisingEdge(dut.clk)
    await Timer(1, unit="ns")
    assert int(dut.m_imu_stream_tvalid.value) == 0
    dut.m_imu_stream_tready.value = 0
    assert await wb_read(dut, REG_DRDY_OVERRUN_COUNT) == 1

    await pulse_drdy(dut)
    await with_timeout(RisingEdge(dut.m_imu_stream_tvalid), 20, "us")
    second_frame = tlp_bytes(dut)
    assert int.from_bytes(second_frame[10:12], "big") == 2
    dut.m_imu_stream_tready.value = 1
    await RisingEdge(dut.clk)
    await Timer(1, unit="ns")
    dut.m_imu_stream_tready.value = 0

    await wb_write(dut, REG_CTRL, 0)
    for _ in range(5):
        status = await wb_read(dut, REG_STATUS)
        if not status & 0x0C:
            break
    else:
        raise AssertionError("Auto-DMA did not stop and return the SPI engine to idle")
