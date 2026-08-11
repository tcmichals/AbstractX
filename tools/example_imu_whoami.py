#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
AbstractX IMU WHO_AM_I Identification Read Example
---------------------------------------------------
Demonstrates reading the ICM-42688-P IMU WHO_AM_I register (0x75 -> returns 0x47)
over AbstractX PCIe-like 64-byte TLPs (asp-tlp-64b).

Usage:
  python3 tools/example_imu_whoami.py --mock
  python3 tools/example_imu_whoami.py --spidev /dev/spidev0.0
"""

import sys
import struct
import argparse

# -----------------------------------------------------------------------------
# BAR Register Addresses
# -----------------------------------------------------------------------------
REG_SYS_ID_REV    = 0x40000000  # Expects 0xABF10164 (FPGA Device ID)
REG_IMU_CTRL      = 0x40000100  # Bit 0 = Auto DMA Enable
REG_IMU_START_REG = 0x40000104  # IMU SPI Register Address (e.g., 0x75 = WHO_AM_I)
REG_IMU_BURST_LEN = 0x40000108  # SPI Read Byte Count

def read_imu_whoami_demo(spidev_path="/dev/spidev0.0", mock=False):
    print("================================================================")
    print(" AbstractX IMU WHO_AM_I Register Read Walkthrough")
    print("================================================================")

    if mock:
        print("[+] Operating in MOCK mode (Simulation)")
        # 1. Read FPGA PCIe Identity Register (0x40000000)
        fpga_id = 0xABF10164
        print(f"Step 1: Read FPGA Device ID [0x40000000] -> 0x{fpga_id:08X}")
        print("        -> FPGA is alive & responding over TLP bus!")

        # 2. Configure IMU SPI Register Address to 0x75 (WHO_AM_I)
        whoami_reg_addr = 0x75
        print(f"Step 2: Write IMU Register Address [0x40000104] <= 0x{whoami_reg_addr:02X} (WHO_AM_I)")

        # 3. Read IMU WHO_AM_I Register
        whoami_val = 0x47  # ICM-42688-P Magic Device ID Byte
        print(f"Step 3: Read IMU WHO_AM_I Register -> Returned 0x{whoami_val:02X} ({whoami_val} dec)")
        
        if whoami_val == 0x47:
            print("\n[SUCCESS] ICM-42688-P IMU Silicon Verified! (WHO_AM_I = 0x47)")
        else:
            print(f"\n[FAILURE] Unexpected WHO_AM_I: 0x{whoami_val:02X}")
        return

    # Real Hardware Execution via spidev
    try:
        import spidev
        spi = spidev.SpiDev()
        bus, dev = map(int, spidev_path.replace('/dev/spidev', '').split('.'))
        spi.open(bus, dev)
        spi.max_speed_hz = 25000000
        spi.mode = 0
        print(f"[+] Opened {spidev_path} at 25 MHz")

        # Step 1: Build 64-byte MemRd TLP for FPGA Device ID (0x40000000)
        # TLP Header: Type=0x00 (MemRd), Channel=0x01 (CTRL), Tag=0x0000, Addr=0x40000000
        tlp_req = struct.pack('>BBHI', 0x00, 0x01, 0x0000, REG_SYS_ID_REV) + b'\x00'*56
        cmd_frame = [0xA2] + list(tlp_req) + [0]*64  # 0xA2 = TLP Read Burst
        rx = spi.xfer2(cmd_frame)
        fpga_id = struct.unpack('>I', bytes(rx[73:77]))[0]
        print(f"Step 1: Read FPGA Device ID [0x40000000] -> 0x{fpga_id:08X}")

        # Step 2: Configure IMU Start Register = 0x75 (WHO_AM_I)
        # TLP Header: Type=0x01 (MemWr), Channel=0x01 (CTRL), Addr=0x40000104, Payload=0x00000075
        tlp_wr = struct.pack('>BBHI', 0x01, 0x01, 0x0000, REG_IMU_START_REG) + struct.pack('>I', 0x75).ljust(56, b'\x00')
        spi.xfer2([0xA1] + list(tlp_wr))
        print("Step 2: Configured IMU Start Register [0x40000104] <= 0x75 (WHO_AM_I)")

        # Step 3: Read IMU WHO_AM_I
        tlp_rd = struct.pack('>BBHI', 0x00, 0x01, 0x0000, REG_IMU_START_REG) + b'\x00'*56
        rx = spi.xfer2([0xA2] + list(tlp_rd) + [0]*64)
        whoami_val = struct.unpack('>I', bytes(rx[73:77]))[0] & 0xFF
        print(f"Step 3: Read IMU WHO_AM_I -> Returned 0x{whoami_val:02X}")

        if whoami_val == 0x47:
            print("\n[SUCCESS] ICM-42688-P IMU Silicon Verified! (WHO_AM_I = 0x47)")
        else:
            print(f"\n[FAILURE] Unexpected WHO_AM_I: 0x{whoami_val:02X}")

    except Exception as e:
        print(f"[!] Hardware Error: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="AbstractX IMU WHO_AM_I Read Example")
    parser.add_argument("--spidev", default="/dev/spidev0.0", help="Linux SPI device path")
    parser.add_argument("--mock", action="store_true", help="Run in software mock mode")
    args = parser.parse_args()
    read_imu_whoami_demo(spidev_path=args.spidev, mock=args.mock)
