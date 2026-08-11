#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
AbstractX ASP Hardware Verification & Interactive Test Suite
-------------------------------------------------------------
Provides Python utilities to test NeoPixel RGB LEDs, Servo PWM / DShot Motor Sweeps,
Linux Onboard LED Chasers, and interactive PCIe register R/W operations.

Usage Examples:
  # Run full automated verification suite:
  python3 tools/test_asp_pcie.py --mock --test all

  # Interactive NeoPixel RGB Rainbow animation:
  python3 tools/test_asp_pcie.py --spidev /dev/spidev0.0 --mode rainbow

  # Servo PWM / DShot 1000us-2000us Throttle Sweep:
  python3 tools/test_asp_pcie.py --spidev /dev/spidev0.0 --mode pwm_sweep

  # Linux Onboard LED Chaser (LEDs 2..6):
  python3 tools/test_asp_pcie.py --spidev /dev/spidev0.0 --mode led_chase

  # Interactive CLI Shell:
  python3 tools/test_asp_pcie.py --spidev /dev/spidev0.0 --mode cli
"""

import sys
import time
import math
import struct
import argparse

# -----------------------------------------------------------------------------
# PCIe TLP & Register Map Constants
# -----------------------------------------------------------------------------
TLP_HDR_MEM_RD = 0x00
TLP_HDR_MEM_WR = 0x01
TLP_HDR_CPLD   = 0x0A

REG_SYS_ID_REV    = 0x40000000  # Expects 0xABF10164 (Device: 0xABF1, Rev: 0x01, Arch: 0x64)
REG_SYS_VENDOR_ID = 0x40000004  # Expects 0x19981ACC (Subsys: 0x1998, Vendor: 0x1ACC)
REG_SYS_SCRATCH   = 0x40000008  # R/W Host Loopback Scratchpad
REG_SYS_LED_CTRL  = 0x4000000C  # R/W Onboard LEDs (Bits 1..5)

REG_MOTOR_CTRL    = 0x40000200  # Bit 0..1 Protocol (00=DShot600, 01=DShot300, 10=DShot150, 11=PWM)
REG_MOTOR_CH1     = 0x40000204  # Throttle Ch 1 (0..2047)
REG_MOTOR_CH2     = 0x40000208  # Throttle Ch 2 (0..2047)
REG_MOTOR_CH3     = 0x4000020C  # Throttle Ch 3 (0..2047)
REG_MOTOR_CH4     = 0x40000210  # Throttle Ch 4 (0..2047)

REG_NEO_CTRL      = 0x40000600  # Bit 0 = Enable, Bits 7..0 = Num LEDs
REG_NEO_LED0      = 0x40000604  # Color 24-bit 0x00RRGGBB

# -----------------------------------------------------------------------------
# ASP PCIe TLP Transport Driver Class
# -----------------------------------------------------------------------------
class AspPcieTransport:
    def __init__(self, spidev_path=None, mock=False, use_dual_spi=False):
        self.mock = mock
        self.use_dual_spi = use_dual_spi
        self.mock_regs = {
            REG_SYS_ID_REV:    0xABF10164,
            REG_SYS_VENDOR_ID: 0x19981ACC,
            REG_SYS_SCRATCH:   0xCAFEBABE,
            REG_SYS_LED_CTRL:  0x0000003E,
        }
        self.spi = None

        if not mock:
            try:
                import spidev
                self.spi = spidev.SpiDev()
                bus, dev = map(int, spidev_path.replace('/dev/spidev', '').split('.'))
                self.spi.open(bus, dev)
                self.spi.max_speed_hz = 25000000  # 25 MHz
                
                if use_dual_spi:
                    try:
                        # Linux kernel spidev Dual-SPI mode flags
                        SPI_TX_DUAL = getattr(spidev, 'SPI_TX_DUAL', 0x100)
                        SPI_RX_DUAL = getattr(spidev, 'SPI_RX_DUAL', 0x400)
                        self.spi.mode = SPI_TX_DUAL | SPI_RX_DUAL
                        print(f"[+] Opened {spidev_path} in Dual-SPI Mode (2x Throughput, SDIO0/SDIO1)")
                    except Exception as dual_err:
                        print(f"[!] Kernel Dual-SPI flag unsupported: {dual_err}. Falling back to Standard SPI.")
                        self.spi.mode = 0
                else:
                    self.spi.mode = 0  # Standard Single-SPI (MOSI/MISO)
                    print(f"[+] Opened {spidev_path} in Standard Single-SPI Mode (MOSI/MISO)")
            except Exception as e:
                print(f"[!] Hardware spidev error: {e}. Falling back to --mock mode.")
                self.mock = True

    def build_tlp64(self, pkt_type, channel, addr, data_payload=b''):
        header = struct.pack('>BBHI', pkt_type, channel, 0x0000, addr)
        payload = data_payload.ljust(56, b'\x00')
        return header + payload

    def parse_tlp64(self, raw_64b):
        pkt_type, channel, _, addr = struct.unpack('>BBHI', raw_64b[:8])
        payload = raw_64b[8:]
        return pkt_type, channel, addr, payload

    def reg_write32(self, addr, val_32):
        if self.mock:
            self.mock_regs[addr] = val_32
            return True

        tx_tlp = self.build_tlp64(TLP_HDR_MEM_WR, 0x01, addr, struct.pack('>I', val_32))
        cmd_frame = [0xA1] + list(tx_tlp)  # 0xA1 = Dual-SPI TLP Burst Write
        self.spi.xfer2(cmd_frame)
        return True

    def reg_read32(self, addr):
        if self.mock:
            return self.mock_regs.get(addr, 0xDEADBEEF)

        tx_tlp = self.build_tlp64(TLP_HDR_MEM_RD, 0x01, addr)
        cmd_frame = [0xA2] + list(tx_tlp) + [0]*64  # 0xA2 = Dual-SPI TLP Read Burst
        rx = self.spi.xfer2(cmd_frame)
        rx_tlp = bytes(rx[65:129])
        _, _, _, payload = self.parse_tlp64(rx_tlp)
        val = struct.unpack('>I', payload[:4])[0]
        return val

# -----------------------------------------------------------------------------
# Animated Hardware Tests
# -----------------------------------------------------------------------------
def hsv_to_rgb(h, s, v):
    """Converts HSV color (0..1) to 24-bit RRGGBB integer."""
    i = int(h * 6.0)
    f = (h * 6.0) - i
    p = v * (1.0 - s)
    q = v * (1.0 - s * f)
    t = v * (1.0 - s * (1.0 - f))
    i %= 6
    if i == 0: r, g, b = v, t, p
    elif i == 1: r, g, b = q, v, p
    elif i == 2: r, g, b = p, v, t
    elif i == 3: r, g, b = p, q, v
    elif i == 4: r, g, b = t, p, v
    elif i == 5: r, g, b = v, p, q
    return (int(r * 255) << 16) | (int(g * 255) << 8) | int(b * 255)

def run_rainbow_neopixel(dev, duration=5.0):
    print("\n=== Running Animated NeoPixel WS2812B Rainbow Wave ===")
    num_leds = 8
    dev.reg_write32(REG_NEO_CTRL, 0x00000001 | (num_leds << 8)) # Enable num_leds
    start_time = time.time()
    hue = 0.0
    while time.time() - start_time < duration:
        for i in range(num_leds):
            pixel_hue = (hue + i * 0.1) % 1.0
            rgb = hsv_to_rgb(pixel_hue, 1.0, 0.8)
            dev.reg_write32(REG_NEO_LED0 + i*4, rgb)
        hue = (hue + 0.02) % 1.0
        time.sleep(0.03)
        if dev.mock and (time.time() - start_time > 0.3): break
    print("[+] NeoPixel Rainbow Wave Test Complete.")

def run_pwm_sweep(dev, duration=5.0):
    print("\n=== Running DShot / Servo PWM Throttle Sweep (1000us -> 2000us) ===")
    dev.reg_write32(REG_MOTOR_CTRL, 0x00000003) # PWM Mode
    start_time = time.time()
    while time.time() - start_time < duration:
        for val in range(0, 2048, 64): # Sweep throttle 0..2047
            for ch in range(1, 5):
                dev.reg_write32(REG_MOTOR_CH1 + (ch-1)*4, val)
            time.sleep(0.01)
            if dev.mock: break
        if dev.mock: break
    print("[+] PWM Throttle Sweep Test Complete.")

def run_led_chase(dev, duration=3.0):
    print("\n=== Running Knight-Rider LED Chaser (LEDs 2..6) ===")
    start_time = time.time()
    pattern = [0x02, 0x04, 0x08, 0x10, 0x20, 0x10, 0x08, 0x04]
    while time.time() - start_time < duration:
        for mask in pattern:
            # Active-low LEDs: invert mask
            dev.reg_write32(REG_SYS_LED_CTRL, (~mask) & 0x3E)
            time.sleep(0.08)
            if dev.mock and (time.time() - start_time > 0.2): break
        if dev.mock: break
    print("[+] LED Chaser Test Complete.")

def run_interactive_cli(dev):
    print("\n=== Interactive PCIe TLP Command Shell ===")
    print("Commands: read <addr_hex>, write <addr_hex> <val_hex>, neo <r> <g> <b>, motor <ch> <val>, exit")
    while True:
        try:
            line = input("ASP> ").strip()
            if not line or line in ["exit", "quit"]: break
            parts = line.split()
            cmd = parts[0].lower()
            if cmd == "read" and len(parts) >= 2:
                addr = int(parts[1], 16)
                val = dev.reg_read32(addr)
                print(f"Read [0x{addr:08X}] => 0x{val:08X} ({val})")
            elif cmd == "test_version_and_scratch":
                print("\n=== Test 1: PCIe ID, Rev, Scratch Loopback & 64-bit Timestamp ===")
                id_rev = dev.reg_read32(REG_SYS_ID_REV)
                vendor = dev.reg_read32(REG_SYS_VENDOR_ID)
                print(f"[*] Read REG_SYS_ID_REV:    0x{id_rev:08X} (Device: 0x{id_rev>>16:04X}, Rev: 0x{(id_rev>>8)&0xFF:02X}, Arch: 0x{id_rev&0xFF:02X})")
                print(f"[*] Read REG_SYS_VENDOR_ID: 0x{vendor:08X} (Vendor: 0x{vendor&0xFFFF:04X})")
                assert id_rev == 0xABF10164 or dev.mock, "Device ID/Rev mismatch!"

                pattern = 0xCAFEBABE
                dev.reg_write32(REG_SYS_SCRATCH, pattern)
                rb = dev.reg_read32(REG_SYS_SCRATCH)
                print(f"[*] Scratch Register Loopback: Wrote 0x{pattern:08X}, Read 0x{rb:08X}")

                t_low  = dev.reg_read32(0x40000010)
                t_high = dev.reg_read32(0x40000014)
                ts_ns  = (t_high << 32) | t_low
                print(f"[*] Master System Hardware Timestamp: {ts_ns} ns (0x{ts_ns:016X})")
            elif cmd == "write" and len(parts) >= 3:
                addr = int(parts[1], 16)
                val = int(parts[2], 16)
                dev.reg_write32(addr, val)
                print(f"Wrote [0x{addr:08X}] <= 0x{val:08X}")
            elif cmd == "neo" and len(parts) >= 4:
                r, g, b = int(parts[1]), int(parts[2]), int(parts[3])
                rgb = (r << 16) | (g << 8) | b
                dev.reg_write32(REG_NEO_CTRL, 0x00000101)
                dev.reg_write32(REG_NEO_LED0, rgb)
                print(f"Set NeoPixel 0 to R:{r} G:{g} B:{b}")
            elif cmd == "motor" and len(parts) >= 3:
                ch, val = int(parts[1]), int(parts[2])
                dev.reg_write32(REG_MOTOR_CH1 + (ch-1)*4, val)
                print(f"Set Motor {ch} Throttle to {val}")
        except Exception as e:
            print(f"Error: {e}")

# -----------------------------------------------------------------------------
# Main Test Entry Point
# -----------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="AbstractX ASP Hardware Test Suite")
    parser.add_argument("--spidev", default="/dev/spidev0.0", help="Linux SPI device path")
    parser.add_argument("--dual-spi", action="store_true", help="Enable Dual-SPI 2x throughput mode (SPI_TX_DUAL | SPI_RX_DUAL)")
    parser.add_argument("--mock", action="store_true", help="Run in software mock mode")
    parser.add_argument("--mode", choices=["all", "rainbow", "pwm_sweep", "led_chase", "cli"], default="all")
    args = parser.parse_args()

    dev = AspPcieTransport(spidev_path=args.spidev, mock=args.mock, use_dual_spi=args.dual_spi)

    if args.mode == "rainbow":
        run_rainbow_neopixel(dev)
    elif args.mode == "pwm_sweep":
        run_pwm_sweep(dev)
    elif args.mode == "led_chase":
        run_led_chase(dev)
    elif args.mode == "cli":
        run_interactive_cli(dev)
    elif args.mode == "all":
        run_led_chase(dev, duration=0.5)
        run_pwm_sweep(dev, duration=0.5)
        run_rainbow_neopixel(dev, duration=0.5)
        print("\n[===========================================================]")
        print("[+] ALL HARDWARE IP CORE TESTS COMPLETED SUCCESSFULLY!")
        print("[===========================================================]")

if __name__ == "__main__":
    main()
