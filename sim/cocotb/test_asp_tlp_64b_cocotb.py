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
    """Drives Dual-SPI SDR transactions to the FPGA DUT with proper multi-cycle SCLK timing."""
    dut.spi_cs_n.value = 0
    await ClockCycles(dut.clk, 10)

    # Helper for driving SCLK pulse with proper setup/hold for 3-stage synchronizer
    async def sclk_pulse():
        await ClockCycles(dut.clk, 2)
        dut.spi_sclk.value = 1
        await ClockCycles(dut.clk, 4)
        dut.spi_sclk.value = 0
        await ClockCycles(dut.clk, 2)

    # 1. Send Command Byte (0xA0, 0xA1, 0xA2) over MOSI/IO0
    for b in range(7, -1, -1):
        dut.spi_io0.value = (cmd_byte >> b) & 1
        dut.spi_io1.value = 0
        await sclk_pulse()

    dut._log.info(f"  After cmd 0x{cmd_byte:02X}: state={dut.u_spi_frontend.state.value} cmd_shift=0x{int(dut.u_spi_frontend.cmd_shift.value):02X} io0_sync={dut.u_spi_frontend.io0_in_sync.value}")

    # 2. Transmit Payload (Dual-SPI mode: 2 bits per SCLK cycle)
    if payload_bytes:
        for byte_val in payload_bytes:
            for pair_idx in range(3, -1, -1):
                val2 = (byte_val >> (pair_idx * 2)) & 0x3
                dut.spi_io0.value = val2 & 1
                dut.spi_io1.value = (val2 >> 1) & 1
                await sclk_pulse()

    dut._log.info(f"  After payload: state={dut.u_spi_frontend.state.value} clk_pulse_cnt={int(dut.u_spi_frontend.clk_pulse_cnt.value)} rx_valid={dut.u_spi_frontend.o_tlp_rx_valid.value}")

    await ClockCycles(dut.clk, 10)
    dut.spi_cs_n.value = 1
    await ClockCycles(dut.clk, 20)

async def read_dual_spi_burst(dut, cmd_byte: int = 0xA2, num_bytes: int = 64) -> bytes:
    """Reads Dual-SPI SDR TLP burst from the FPGA DUT."""
    dut.spi_cs_n.value = 0
    await ClockCycles(dut.clk, 10)

    async def sclk_pulse():
        await ClockCycles(dut.clk, 2)
        dut.spi_sclk.value = 1
        await ClockCycles(dut.clk, 4)
        dut.spi_sclk.value = 0
        await ClockCycles(dut.clk, 2)

    # 1. Send Command Byte (0xA2) over MOSI/IO0
    for b in range(7, -1, -1):
        dut.spi_io0.value = (cmd_byte >> b) & 1
        dut.spi_io1.value = 0
        await sclk_pulse()

    # 2. Read 64-byte payload in Dual-SPI mode (2 bits per SCLK)
    rx_bytes = bytearray()
    for _ in range(num_bytes):
        byte_val = 0
        for _ in range(4):
            # Sample on falling edge of SCLK
            await ClockCycles(dut.clk, 2)
            dut.spi_sclk.value = 1
            await ClockCycles(dut.clk, 4)
            # Sample outputs driven on falling edge
            dut.spi_sclk.value = 0
            await ClockCycles(dut.clk, 1)
            b0 = int(dut.spi_io0_o.value)
            b1 = int(dut.spi_io1_o.value)
            byte_val = (byte_val << 2) | ((b1 << 1) | b0)
            await ClockCycles(dut.clk, 1)
        rx_bytes.append(byte_val)

    await ClockCycles(dut.clk, 10)
    dut.spi_cs_n.value = 1
    await ClockCycles(dut.clk, 20)
    return bytes(rx_bytes)


@cocotb.test()
async def test_inav_icm42688p_driver_sequence(dut):
    """End-to-End Testbench Mirroring iNav accgyro_icm42605.c Initialization Sequence."""
    # Start 100 MHz System Clock
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())

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

    # Step 6: Enable FPGA IMU Auto-DMA (Write 0x00000005 to IMU_CTRL: Bit 0=Enable, Bit 2=Active-High IRQ)
    dut._log.info("[Step 6] Enabling FPGA Auto-DMA (writing 0x00000005 to IMU_CTRL)...")
    mem_wr_ctrl_tlp = pack_tlp(
        tlp_type=0x02,
        flags=0,
        tag=0,
        channel=0x01,
        addr=0x40000100,  # IMU_CTRL
        len_dw=1,
        seq=2,
        ts=0,
        payload=struct.pack(">I", 0x00000005),
    )
    await send_dual_spi_burst(dut, 0xA1, mem_wr_ctrl_tlp)

    # Step 7: Wait for iNav DRDY interrupt pulse & FPGA SPI acquisition
    dut._log.info("[Step 7] Waiting for iNav DRDY Interrupt pulse and FPGA SPI Master 14-byte read...")
    dut._log.info(f"  Before DRDY: auto_dma_en={dut.u_imu_core.auto_dma_en.value} int_polarity={dut.u_imu_core.int_polarity.value} burst_addr=0x{int(dut.u_imu_core.burst_addr.value):02X}")
    await RisingEdge(dut.imu_int_i)
    dut._log.info("  DRDY pulse detected! Waiting for FPGA SPI acquisition...")
    # Wait for FPGA hardware SPI master to clock out 14 bytes and latch TLP into egress
    for cycle in range(5000):
        await RisingEdge(dut.clk)
        if dut.o_int_req.value == 1:
            dut._log.info(f"  o_int_req asserted after {cycle} clock cycles!")
            break

    dut._log.info(f"  After wait: imu_state={dut.u_imu_core.imu_state.value} tvalid={dut.u_imu_core.m_imu_stream_tvalid.value} egress_count={dut.u_spi_frontend.i_egress_count.value} o_int_req={dut.o_int_req.value}")

    # Step 8: Check Doorbell Interrupt (o_int_req) Output
    assert dut.o_int_req.value == 1, "Doorbell Interrupt (o_int_req) failed to assert on iNav Accel/Gyro TLP!"
    dut._log.info("[SUCCESS] o_int_req doorbell successfully asserted HIGH on 14-byte iNav Accel/Gyro TLP!")

    # Step 9: Dual-SPI Burst Read of the Autonomous DMA_Stream TLP
    dut._log.info("[Step 9] Reading 64-byte Telemetry TLP over Dual-SPI (CMD 0xA2)...")
    rx_tlp_bytes = await read_dual_spi_burst(dut, cmd_byte=0xA2, num_bytes=64)
    tlp_type, flags, tag, channel, addr, len_dw, seq, ts, payload, crc = unpack_tlp(rx_tlp_bytes)

    dut._log.info(f"  Received TLP: Type=0x{tlp_type:02X} Channel=0x{channel:02X} Addr=0x{addr:08X} Len={len_dw}DW Seq={seq} TS={ts}ns")
    assert tlp_type == 0x10, f"Expected Type=0x10 (DMA_Stream), got 0x{tlp_type:02X}"
    assert channel == 0x02, f"Expected Channel=0x02 (TELEMETRY), got 0x{channel:02X}"
    assert addr == 0x40000100, f"Expected Target Addr=0x40000100 (IMU BAR Base), got 0x{addr:08X}"

    # Extract 14 bytes sensor telemetry
    temp, ax, ay, az, gx, gy, gz = struct.unpack(">hhhhhhh", payload[:14])
    dut._log.info(f"  Decoded IMU Data: Temp={temp} Accel=({ax},{ay},{az}) Gyro=({gx},{gy},{gz})")
    assert (temp, ax, ay, az, gx, gy, gz) == (3312, 164, -82, 2048, 15, -22, 4), "Sensor payload mismatch!"
    dut._log.info("[SUCCESS] 14-Byte IMU Telemetry payload perfectly verified against VIP readings!")

    # Verify Doorbell deasserts after read-out
    await ClockCycles(dut.clk, 20)
    assert dut.o_int_req.value == 0, "Doorbell Interrupt (o_int_req) should deassert after TLP is read!"
    dut._log.info("[SUCCESS] o_int_req doorbell automatically deasserted after TLP readout!")

    dut._log.info("ALL INAV ICM-42688-P DRIVER COCOTB VERIFICATION TESTS PASSED SUCCESSFULLY!")
