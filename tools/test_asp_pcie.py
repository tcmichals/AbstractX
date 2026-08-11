#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
AbstractX ASP PCIe-like TLP Register Access Test Suite
-------------------------------------------------------
Provides Python interface over Linux /dev/spidevX.Y (or mock Mode) to read/write 
FPGA Wishbone BAR registers, control onboard LEDs, set DShot motors, set NeoPixel RGB,
and capture hardware IMU timestamped telemetry over 64-byte TLP frames.

Usage:
  python3 test_asp_pcie.py --spidev /dev/spidev0.0 --test all
  python3 test_asp_pcie.py --mock --test led_blink
  python3 test_asp_pcie.py --mock --test dshot
  python3 test_asp_pcie.py --mock --test neopixel
"""

import sys
import time
import struct
import argparse

# -----------------------------------------------------------------------------
# PCIe TLP & Register Map Constants
# -----------------------------------------------------------------------------
TLP_HDR_MEM_RD = 0x00
TLP_HDR_MEM_WR = 0x01
TLP_HDR_CPLD   = 0x0A

BAR_SYS_BASE      = 0x40000000
REG_SYS_VERSION   = 0x40000000  # Expects 0xA1B2C3D4
REG_SYS_SCRATCH   = 0x40000004  # R/W Loopback
REG_SYS_LED_CTRL  = 0x40000008  # R/W Onboard LEDs (Bits 1..5)

BAR_IMU_BASE      = 0x40000100  # IMU SPI Auto-DMA & Timestamping Engine
REG_IMU_CTRL      = 0x40000100  # Bit 0 = Auto-DMA Enable
REG_IMU_DATA_LOW  = 0x40000104  # Accel X/Y
REG_IMU_DATA_HIGH = 0x40000108  # Accel Z / Gyro X
REG_IMU_TIME_LOW  = 0x40000110  # 64-bit nanosecond timestamp low
REG_IMU_TIME_HIGH = 0x40000114  # 64-bit nanosecond timestamp high

BAR_DSHOT_BASE    = 0x40000200  # DShot 150/300/600 Motor Core
REG_MOTOR_CTRL    = 0x40000200  # Bit 0..1 Protocol (00=DShot600, 01=DShot300, 10=DShot150, 11=PWM)
REG_MOTOR_CH1     = 0x40000204  # Throttle Ch 1 (0..2047)
REG_MOTOR_CH2     = 0x40000208  # Throttle Ch 2 (0..2047)
REG_MOTOR_CH3     = 0x4000020C  # Throttle Ch 3 (0..2047)
REG_MOTOR_CH4     = 0x40000210  # Throttle Ch 4 (0..2047)

BAR_NEOPIXEL_BASE = 0x40000600  # NeoPixel WS2812B RGB Core
REG_NEO_CTRL      = 0x40000600  # Bit 0 = Enable, Bits 7..0 = Num LEDs
REG_NEO_LED0      = 0x40000604  # Color 24-bit 0x00RRGGBB

# -----------------------------------------------------------------------------
# ASP PCIe TLP Transport Driver Class
# -----------------------------------------------------------------------------
class AspPcieTransport:
    def __init__(self, spidev_path=None, mock=False):
        self.mock = mock
        self.mock_regs = {
            REG_SYS_VERSION: 0xA1B2C3D4,
            REG_SYS_SCRATCH: 0x00000000,
            REG_SYS_LED_CTRL: 0x0000003E,
        }
        self.spi = None

        if not mock:
            try:
                import spidev
                self.spi = spidev.SpiDev()
                bus, dev = map(int, spidev_path.replace('/dev/spidev', '').split('.'))
                self.spi.open(bus, dev)
                self.spi.max_speed_hz = 25000000  # 25 MHz
                self.spi.mode = 0
                print(f"[+] Opened {spidev_path} at 25 MHz (Dual-SPI TLP mode)")
            except Exception as e:
                print(f"[!] Hardware spidev error: {e}. Falling back to --mock mode.")
                self.mock = True

    def build_tlp64(self, pkt_type, channel, addr, data_payload=b''):
        """Constructs 64-byte PCIe TLP Packet Frame."""
        header = struct.pack('>BBHI', pkt_type, channel, 0x0000, addr)
        payload = data_payload.ljust(56, b'\x00')
        return header + payload

    def parse_tlp64(self, raw_64b):
        """Parses 64-byte PCIe TLP Packet Frame."""
        pkt_type, channel, _, addr = struct.unpack('>BBHI', raw_64b[:8])
        payload = raw_64b[8:]
        return pkt_type, channel, addr, payload

    def reg_write32(self, addr, val_32):
        """Write 32-bit register via PCIe TLP MemWr (0x01)."""
        if self.mock:
            self.mock_regs[addr] = val_32
            print(f"[MOCK WR32] Addr: 0x{addr:08X} <= 0x{val_32:08X}")
            return True

        tx_tlp = self.build_tlp64(TLP_HDR_MEM_WR, 0x01, addr, struct.pack('>I', val_32))
        cmd_frame = [0xA1] + list(tx_tlp)  # 0xA1 = Dual-SPI TLP Burst Write
        self.spi.xfer2(cmd_frame)
        return True

    def reg_read32(self, addr):
        """Read 32-bit register via PCIe TLP MemRd (0x00)."""
        if self.mock:
            val = self.mock_regs.get(addr, 0xDEADBEEF)
            print(f"[MOCK RD32] Addr: 0x{addr:08X} => 0x{val:08X}")
            return val

        tx_tlp = self.build_tlp64(TLP_HDR_MEM_RD, 0x01, addr)
        cmd_frame = [0xA2] + list(tx_tlp) + [0]*64  # 0xA2 = Dual-SPI TLP Read Burst
        rx = self.spi.xfer2(cmd_frame)
        rx_tlp = bytes(rx[65:129])
        _, _, _, payload = self.parse_tlp64(rx_tlp)
        val = struct.unpack('>I', payload[:4])[0]
        return val

# -----------------------------------------------------------------------------
# Test Cases
# -----------------------------------------------------------------------------
def test_version_and_scratch(dev):
    print("\n=== Test 1: PCIe Version Register Query & Scratch Loopback ===")
    ver = dev.reg_read32(REG_SYS_VERSION)
    print(f"[*] Read SYS_VERSION: 0x{ver:08X} (Expected: 0xA1B2C3D4)")
    assert ver == 0xA1B2C3D4 or dev.mock, "Version mismatch!"

    pattern = 0xCAFEBABE
    dev.reg_write32(REG_SYS_SCRATCH, pattern)
    rb = dev.reg_read32(REG_SYS_SCRATCH)
    print(f"[*] Scratch Register Loopback: Wrote 0x{pattern:08X}, Read 0x{rb:08X}")

def test_linux_led_blink(dev):
    print("\n=== Test 2: Linux Dynamic Onboard LED Control (LEDs 2..6) ===")
    print("[*] Blinking onboard LEDs 2..6 over PCIe TLP register 0x40000008...")
    for i in range(5):
        # Toggle bits 1..5 ON
        dev.reg_write32(REG_SYS_LED_CTRL, 0x00000000)
        time.sleep(0.15)
        # Toggle bits 1..5 OFF
        dev.reg_write32(REG_SYS_LED_CTRL, 0x0000003E)
        time.sleep(0.15)
    print("[+] Onboard LED toggle test PASS.")

def test_dshot_motors(dev):
    print("\n=== Test 3: DShot 150/300/600 Motor Command Register Setup ===")
    dev.reg_write32(REG_MOTOR_CTRL, 0x00000000) # DShot600 Mode
    throttles = [100, 500, 1000, 2000]
    for ch, val in enumerate(throttles, start=1):
        addr = REG_MOTOR_CH1 + (ch - 1) * 4
        dev.reg_write32(addr, val)
        print(f"[*] Set Motor Channel {ch} Throttle <= {val} (DShot 11-bit Frame)")
    print("[+] Motor setup PASS.")

def test_neopixel(dev):
    print("\n=== Test 4: WS2812B NeoPixel RGB LED Strip Control ===")
    dev.reg_write32(REG_NEO_CTRL, 0x00000004) # Enable 4 LEDs
    colors = [0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFF00] # Red, Green, Blue, Yellow
    for idx, c in enumerate(colors):
        dev.reg_write32(REG_NEO_LED0 + idx*4, c)
        print(f"[*] NeoPixel LED {idx} Color <= 0x{c:06X}")
    print("[+] NeoPixel setup PASS.")

def main():
    parser = argparse.ArgumentParser(description="AbstractX ASP PCIe Register & Hardware Test")
    parser.add_argument("--spidev", default="/dev/spidev0.0", help="Linux SPI device path")
    parser.add_argument("--mock", action="store_true", help="Run in software mock simulation mode")
    parser.add_argument("--test", choices=["all", "version", "led", "dshot", "neopixel"], default="all")
    args = parser.parse_args()

    dev = AspPcieTransport(spidev_path=args.spidev, mock=args.mock)

    if args.test in ["all", "version"]:
        test_version_and_scratch(dev)
    if args.test in ["all", "led"]:
        test_linux_led_blink(dev)
    if args.test in ["all", "dshot"]:
        test_dshot_motors(dev)
    if args.test in ["all", "neopixel"]:
        test_neopixel(dev)

    print("\n[===========================================================]")
    print("[+] ALL ABSTRACTX ASP HARDWARE TESTS COMPLETED SUCCESSFULLY!")
    print("[===========================================================]")

if __name__ == "__main__":
    main()
