# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Python Cocotb VIP Model matching iNav accgyro_icm42605.c Driver.

Emulates the exact register set and behavior of the InvenSense / TDK ICM-42688-P
as initialized by the iNav flight controller codebase:
1. WHO_AM_I (0x75 -> 0x47).
2. PWR_MGMT0 (0x4E -> 0x0F for Low-Noise Accel + Gyro).
3. GYRO_CONFIG0 (0x4F) & ACCEL_CONFIG0 (0x50) for ODR setup.
4. INT_CONFIG (0x14 -> 0x03) & INT_SOURCE0 (0x65 -> 0x08) for DRDY pulse generation.
5. Continuous 14-byte telemetry readout starting at TEMP_DATA1 (0x1D).
"""

from __future__ import annotations

import struct
import cocotb
from cocotb.triggers import FallingEdge, RisingEdge, Timer


class CocotbICM42688P:
    """Python Cocotb VIP for ICM-42688-P matching iNav driver specifications."""

    def __init__(self, dut, default_odr_hz: int = 1000):
        self.dut = dut
        self.sample_period_ns = int(1_000_000_000 / default_odr_hz)

        # ICM-42688-P Register Storage (User Bank 0)
        self.registers = [0] * 128
        self.registers[0x75] = 0x47  # WHO_AM_I signature (0x47 for ICM-42688-P)
        self.registers[0x14] = 0x03  # INT_CONFIG (Active High, Push-Pull, Pulsed)
        self.registers[0x4E] = 0x0F  # PWR_MGMT0 (Accel LN + Gyro LN)
        self.registers[0x4F] = 0x06  # GYRO_CONFIG0 (±2000 dps, 1 kHz)
        self.registers[0x50] = 0x06  # ACCEL_CONFIG0 (±16g, 1 kHz)
        self.registers[0x65] = 0x08  # INT_SOURCE0 (UI_DRDY_INT1_EN)

        # Simulated iNav Telemetry Readings (Signed 16-bit)
        self.temp = 3312  # ~25 deg C ((3312/132.48) + 25)
        self.accel_x = 164  # +0.01g
        self.accel_y = -82  # -0.005g
        self.accel_z = 2048  # +1.00g (Gravity vector under ±16g scale: 2048 LSB/g)
        self.gyro_x = 15  # +0.9 dps
        self.gyro_y = -22  # -1.3 dps
        self.gyro_z = 4  # +0.2 dps

        self._update_telemetry_window()
        self._running = True

    def _update_telemetry_window(self):
        """Packs sensor values into 14-byte window starting at TEMP_DATA1 (0x1D)."""
        raw_bytes = struct.pack(
            ">hhhhhhh",
            self.temp,
            self.accel_x,
            self.accel_y,
            self.accel_z,
            self.gyro_x,
            self.gyro_y,
            self.gyro_z,
        )
        for i, b in enumerate(raw_bytes):
            self.registers[0x1D + i] = b

    async def start(self):
        """Starts background tasks for DRDY interrupts and SPI slave handling."""
        cocotb.start_soon(self._drdy_generator())
        cocotb.start_soon(self._spi_slave_loop())

    async def _drdy_generator(self):
        """Generates DRDY interrupt pulses when enabled in INT_SOURCE0 (0x65)."""
        while self._running:
            await Timer(self.sample_period_ns, units="ns")
            if self.registers[0x65] & 0x08:  # DRDY_INT1_EN
                self.dut.imu_int_i.value = 1
                await Timer(100, units="ns")
                self.dut.imu_int_i.value = 0

    async def _spi_slave_loop(self):
        """Handles SPI bus clocking and MISO bit output matching iNav driver."""
        while self._running:
            await FallingEdge(self.dut.imu_cs_n)

            # Read 8-bit Command Byte over MOSI
            cmd_addr = 0
            for _ in range(8):
                await RisingEdge(self.dut.imu_sclk)
                cmd_addr = (cmd_addr << 1) | int(self.dut.imu_mosi.value)

            is_read = bool(cmd_addr & 0x80)
            reg_addr = cmd_addr & 0x7F

            # Shift out Data over MISO during Read Operations
            if is_read:
                while int(self.dut.imu_cs_n.value) == 0:
                    byte_val = self.registers[reg_addr & 0x7F]
                    reg_addr += 1
                    for bit_idx in range(7, -1, -1):
                        await FallingEdge(self.dut.imu_sclk)
                        self.dut.imu_miso.value = (byte_val >> bit_idx) & 1
            else:
                # Handle Register Writes from iNav Host
                while int(self.dut.imu_cs_n.value) == 0:
                    write_val = 0
                    for _ in range(8):
                        await RisingEdge(self.dut.imu_sclk)
                        write_val = (write_val << 1) | int(self.dut.imu_mosi.value)
                    self.registers[reg_addr & 0x7F] = write_val
                    reg_addr += 1

            self.dut.imu_miso.value = 0
