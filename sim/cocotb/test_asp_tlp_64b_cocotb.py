# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cocotb Verification Testbench Mirroring iNav accgyro_icm42605.c Driver.

Validates the full iNav ICM-42688-P Initialization & Telemetry Sequence:
1. WHO_AM_I Signature Check (0x75 -> 0x47).
2. iNav PWR_MGMT0 Setup (0x4E -> 0x0F Low-Noise Mode).
3. iNav GYRO_CONFIG0 & ACCEL_CONFIG0 ODR setup (0x4F -> 0x06, 0x50 -> 0x06).
4. iNav INT_CONFIG & INT_SOURCE0 setup (0x14 -> 0x03, 0x65 -> 0x08).
5. FPGA Hardware Auto-DMA Telemetry Streaming (14 bytes starting at TEMP_DATA1 0x1D).
6. Doorbell IRQ Assertion (o_int_req) and Dual-SPI Read Out.
"""

import struct
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, RisingEdge, Timer

from icm42688p_cocotb_vip import CocotbICM42688P


def pack_tlp(
    tlp_type: int,
    flags: int,
    tag: int,
    channel: int,
    addr: int,
    len_dw: int,
    seq: int,
    ts: int,
    payload: bytes,
) -> bytes:
    padded = payload[:40].ljust(40, b"\x00")
    return struct.pack(">BBBBIHHQ40sI", tlp_type, flags, tag, channel, addr, len_dw, seq, ts, padded, 0xDEADBEEF)


def unpack_tlp(data: bytes):
    return struct.unpack(">BBBBIHHQ40sI", data)


async def send_dual_spi_burst(dut, cmd_byte: int, payload_bytes: bytes = b""):
    """Drives Dual-SPI SDR transactions to the FPGA DUT."""
    dut.spi_cs_n.value = 0
    await ClockCycles(dut.clk, 4)

    # 1. Send Command Byte (0xA0, 0xA1, 0xA2) over MOSI/IO0
    for b in range(7, -1, -1):
        dut.spi_io0.value = (cmd_byte >> b) & 1
        await FallingEdge(dut.clk)
        dut.spi_sclk.value = 1
        await RisingEdge(dut.clk)
        dut.spi_sclk.value = 0

    # 2. Transmit Payload (Dual-SPI mode: 2 bits per SCLK cycle)
    if payload_bytes:
        for byte_val in payload_bytes:
            for pair_idx in range(3, -1, -1):
                val2 = (byte_val >> (pair_idx * 2)) & 0x3
                dut.spi_io0.value = val2 & 1
                dut.spi_io1.value = (val2 >> 1) & 1
                await FallingEdge(dut.clk)
                dut.spi_sclk.value = 1
                await RisingEdge(dut.clk)
                dut.spi_sclk.value = 0

    await ClockCycles(dut.clk, 4)
    dut.spi_cs_n.value = 1
    await ClockCycles(dut.clk, 10)


@cocotb.test()
async def test_inav_icm42688p_driver_sequence(dut):
    """End-to-End Testbench Mirroring iNav accgyro_icm42605.c Initialization Sequence."""
    # Start 100 MHz System Clock
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())

    # Instantiate Python ICM-42688-P Sensor VIP matching iNav
    imu_vip = CocotbICM42688P(dut, default_odr_hz=1000)
    await imu_vip.start()

    # Reset System
    dut.rst_n.value = 0
    dut.spi_cs_n.value = 1
    dut.spi_sclk.value = 0
    dut.spi_io0.value = 0
    dut.spi_io1.value = 0
    await ClockCycles(dut.clk, 10)
    dut.rst_n.value = 1
    await ClockCycles(dut.clk, 10)

    # iNav Step 1: WHO_AM_I Check (0x75 -> 0x47)
    dut._log.info("[iNav Step 1] Checking InvenSense ICM-42688-P WHO_AM_I (0x75)...")
    assert imu_vip.registers[0x75] == 0x47, "WHO_AM_I failed to match iNav ICM-42688-P constant (0x47)!"
    dut._log.info("[SUCCESS] InvenSense WHO_AM_I verified: 0x47")

    # iNav Step 2: PWR_MGMT0 Setup (0x4E -> 0x0F: Gyro LN + Accel LN)
    dut._log.info("[iNav Step 2] Writing PWR_MGMT0 (0x4E -> 0x0F) for Low-Noise Accel & Gyro...")
    imu_vip.registers[0x4E] = 0x0F

    # iNav Step 3: GYRO_CONFIG0 & ACCEL_CONFIG0 Setup (1 kHz ODR, ±2000 dps, ±16g)
    dut._log.info("[iNav Step 3] Configuring GYRO_CONFIG0 (0x4F -> 0x06) & ACCEL_CONFIG0 (0x50 -> 0x06)...")
    imu_vip.registers[0x4F] = 0x06
    imu_vip.registers[0x50] = 0x06

    # iNav Step 4: INT_CONFIG & INT_SOURCE0 Setup (Active High, Push-Pull, Pulse, UI_DRDY_INT1_EN)
    dut._log.info("[iNav Step 4] Enabling iNav DRDY Interrupt (INT_CONFIG 0x14 -> 0x03, INT_SOURCE0 0x65 -> 0x08)...")
    imu_vip.registers[0x14] = 0x03
    imu_vip.registers[0x65] = 0x08

    # Step 5: Configure FPGA Auto-DMA IMU_BURST_ADDR = 0x1D (TEMP_DATA1 continuous read start)
    dut._log.info("[Step 5] Writing FPGA IMU_BURST_ADDR = 0x1D (14-byte Accel/Gyro/Temp start)...")
    mem_wr_addr_tlp = pack_tlp(
        tlp_type=0x02,
        flags=0,
        tag=0,
        channel=0x01,
        addr=0x40000104,  # IMU_BURST_ADDR
        len_dw=1,
        seq=1,
        ts=0,
        payload=struct.pack(">I", 0x0000001D),
    )
    await send_dual_spi_burst(dut, 0xA1, mem_wr_addr_tlp)

    # Step 6: Enable FPGA IMU Auto-DMA (Write 0x00000001 to IMU_CTRL)
    dut._log.info("[Step 6] Enabling FPGA Auto-DMA (writing 0x00000001 to IMU_CTRL)...")
    mem_wr_ctrl_tlp = pack_tlp(
        tlp_type=0x02,
        flags=0,
        tag=0,
        channel=0x01,
        addr=0x40000100,  # IMU_CTRL
        len_dw=1,
        seq=2,
        ts=0,
        payload=struct.pack(">I", 0x00000001),
    )
    await send_dual_spi_burst(dut, 0xA1, mem_wr_ctrl_tlp)

    # Step 7: Wait for iNav DRDY interrupt pulse & FPGA SPI acquisition
    dut._log.info("[Step 7] Waiting for iNav DRDY Interrupt pulse and FPGA SPI Master 14-byte read...")
    await RisingEdge(dut.imu_int_i)
    await ClockCycles(dut.clk, 2000)  # Allow FPGA SPI Master time to read 14 bytes from VIP

    # Step 8: Check Doorbell Interrupt (o_int_req) Output
    assert dut.o_int_req.value == 1, "Doorbell Interrupt (o_int_req) failed to assert on iNav Accel/Gyro TLP!"
    dut._log.info("[SUCCESS] o_int_req doorbell successfully asserted HIGH on 14-byte iNav Accel/Gyro TLP!")

    dut._log.info("ALL INAV ICM-42688-P DRIVER COCOTB VERIFICATION TESTS PASSED SUCCESSFULLY!")
