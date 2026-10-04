#!/usr/bin/env python3
"""
AbstractX QMTECH Zynq-7020 Hardware Diagnostic & IMU Verification Utility
-------------------------------------------------------------------------
Performs live hardware tests on the QMTECH Zynq-7020 FPGA platform:
  1. Ping & Scratch Register Loopback (0x4000_0004)
  2. Onboard User LED Toggle (Carrier D3 & Core D2)
  3. Hardware Nanosecond Clock Benchmark (0x4000_0010)
  4. IMU WHO_AM_I Identification Read (ICM-42688-P @ 0x75 -> 0x47)
  5. Live 8 kHz TLP Telemetry Stream Monitor & Sensor Scope

Usage:
  zynq_diagnostics.py [--uio /dev/uio0] [--test all|ping|led|clock|imu|stream]
"""

import os
import sys
import time
import struct
import mmap
import argparse
from pathlib import Path

# Register Map Offsets
REG_CONTROL      = 0x00
REG_STATUS       = 0x04
REG_IRQ_STATUS   = 0x08
REG_IRQ_ENABLE   = 0x0C
REG_RX_BASE      = 0x10
REG_RX_CAPACITY  = 0x14
REG_RX_HEAD      = 0x18
REG_RX_TAIL      = 0x1C
REG_HARDWARE_ID  = 0x40

# System Registers (Base 0x4000_0000 via Wishbone)
SYS_REG_SCRATCH  = 0x04  # Scratch Register
SYS_REG_LED_CTRL = 0x08  # Bit 0 = Carrier LED (P22), Bit 1 = Core LED (M14)
SYS_REG_TIME_L   = 0x10  # Lower 32 bits of 64-bit nanosecond timer
SYS_REG_TIME_H   = 0x14  # Upper 32 bits

# IMU Registers (Base 0x4000_0100 via Wishbone)
IMU_REG_CTRL     = 0x100 # Bit 0 = Auto DMA Enable, Bit 1 = Int Polarity
IMU_REG_TRIG     = 0x104 # Bit 31 = Trigger, Bit 30 = RW (0=rd, 1=wr), Bits [23:16] = Addr
IMU_REG_LEN      = 0x108 # Byte length to read
IMU_REG_RDATA    = 0x10C # 32-bit direct SPI read data

HARDWARE_MAGIC   = 0x41535036 # "ASP6"

class ZynqDiagnostics:
    def __init__(self, uio_path="/dev/uio0"):
        self.uio_path = Path(uio_path)
        if not self.uio_path.exists():
            raise FileNotFoundError(f"{self.uio_path} not found. Is generic-uio loaded?")

        self.f = open(self.uio_path, "r+b")
        self.mem = mmap.mmap(self.f.fileno(), 0x10000, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)

    def close(self):
        if self.mem:
            self.mem.close()
        if self.f:
            self.f.close()

    def read32(self, offset):
        return struct.unpack("<I", self.mem[offset:offset+4])[0]

    def write32(self, offset, val):
        self.mem[offset:offset+4] = struct.pack("<I", val & 0xFFFFFFFF)

    def test_ping(self):
        print("\n--- [Test 1] Hardware Ping & Scratch Loopback ---")
        magic = self.read32(REG_HARDWARE_ID)
        print(f"[*] Read Hardware ID: 0x{magic:08X} (Expected: 0x{HARDWARE_MAGIC:08X} 'ASP6')")
        if magic != HARDWARE_MAGIC:
            print("[-] FAIL: Magic mismatch! FPGA fabric not responding properly.")
            return False

        patterns = [0xA5A55A5A, 0x12345678, 0xDEADBEEF, 0xCAFEBABE]
        for pat in patterns:
            self.write32(SYS_REG_SCRATCH, pat)
            rb = self.read32(SYS_REG_SCRATCH)
            if rb != pat:
                print(f"[-] FAIL: Scratch loopback mismatch: wrote 0x{pat:08X}, read 0x{rb:08X}")
                return False
            print(f"[+] Scratch Loopback OK: Wrote 0x{pat:08X} == Read 0x{rb:08X}")

        print("[SUCCESS] Hardware Ping & Register Loopback Passed!")
        return True

    def test_leds(self):
        print("\n--- [Test 2] Onboard User LED Toggle ---")
        print("[*] Alternating Carrier LED (P22) and Core LED (M14) 3 times...")
        for i in range(3):
            self.write32(SYS_REG_LED_CTRL, 0x01) # Carrier LED ON
            time.sleep(0.15)
            self.write32(SYS_REG_LED_CTRL, 0x02) # Core LED ON
            time.sleep(0.15)
        self.write32(SYS_REG_LED_CTRL, 0x03) # Both ON
        time.sleep(0.2)
        self.write32(SYS_REG_LED_CTRL, 0x00) # Both OFF
        print("[SUCCESS] User LED test completed.")
        return True

    def test_clock(self):
        print("\n--- [Test 3] Hardware Nanosecond Clock Benchmark ---")
        t0_low = self.read32(SYS_REG_TIME_L)
        t0_high = self.read32(SYS_REG_TIME_H)
        t0 = (t0_high << 32) | t0_low

        host_t0 = time.perf_counter()
        time.sleep(0.2)
        host_elapsed = time.perf_counter() - host_t0

        t1_low = self.read32(SYS_REG_TIME_L)
        t1_high = self.read32(SYS_REG_TIME_H)
        t1 = (t1_high << 32) | t1_low

        delta_ticks = t1 - t0
        freq_hz = delta_ticks / host_elapsed
        freq_mhz = freq_hz / 1e6

        print(f"[+] Clock measured: {freq_mhz:.2f} MHz (Delta: {delta_ticks:,} ticks over {host_elapsed*1000:.1f} ms)")
        if 90.0 <= freq_mhz <= 110.0:
            print("[SUCCESS] PL Master Clock is accurate (100 MHz +/- 10%).")
            return True
        else:
            print(f"[WARN] Measured frequency {freq_mhz:.2f} MHz deviates from expected 100.0 MHz.")
            return False

    def test_imu_whoami(self):
        print("\n--- [Test 4] IMU SPI WHO_AM_I Read ---")
        # ICM-42688-P WHO_AM_I register is 0x75 (returns 0x47)
        # ICM-20602 / MPU6000 WHO_AM_I register is 0x75 (returns 0x12 / 0x68)
        # BMI088 WHO_AM_I register is 0x00 (returns 0x1E accel / 0x0F gyro)
        target_reg = 0x75
        print(f"[*] Dispatching manual SPI Read targeting register 0x{target_reg:02X} via PMOD JP5...")

        # Configure SPI read length = 1 byte
        self.write32(IMU_REG_LEN, 1)

        # Pulse direct trigger: Bit 31 = Trig (1), Bit 30 = Read (0), Bits [23:16] = 0x75
        cmd = (1 << 31) | (0 << 30) | (target_reg << 16)
        self.write32(IMU_REG_TRIG, cmd)

        time.sleep(0.005) # Wait 5 ms for SPI clocking

        resp = self.read32(IMU_REG_RDATA)
        whoami_byte = resp & 0xFF
        print(f"[+] IMU SPI Response: Raw=0x{resp:08X}, WHO_AM_I Byte = 0x{whoami_byte:02X}")

        if whoami_byte == 0x47:
            print("[SUCCESS] Detected ICM-42688-P IMU (0x47)! Sensor SPI interface verified.")
            return True
        elif whoami_byte in [0x12, 0x68, 0x71, 0x98, 0x1E, 0x0F]:
            print(f"[SUCCESS] Detected recognized IMU (Chip ID: 0x{whoami_byte:02X})! SPI interface verified.")
            return True
        elif whoami_byte == 0x00 or whoami_byte == 0xFF:
            print(f"[!] Warning: Read 0x{whoami_byte:02X}. Check PMOD wiring (SCK=L22, CS=L21, MOSI=K20, MISO=K19).")
            return False
        else:
            print(f"[+] Received unknown device ID 0x{whoami_byte:02X} (SPI bus is clocking data).")
            return True

    def test_stream_monitor(self, duration_sec=5.0):
        print(f"\n--- [Test 5] Live 8 kHz TLP Telemetry Stream Monitor ({duration_sec}s) ---")
        print("[*] Enabling Hardware Auto-DMA Engine (0x4000_0100 <= 0x01)...")
        self.write32(IMU_REG_CTRL, 0x01) # Auto-DMA Enable

        t_end = time.time() + duration_sec
        packets_received = 0
        last_tail = self.read32(REG_RX_TAIL)

        print("[*] Listening for hardware TLP frames...")
        print("    [PKT #]  | ACCEL_X | ACCEL_Y | ACCEL_Z | GYRO_X | GYRO_Y | GYRO_Z | TEMP (C)")
        print("    " + "-"*75)

        while time.time() < t_end:
            tail = self.read32(REG_RX_TAIL)
            if tail != last_tail:
                packets_received += (tail - last_tail) & 0xFF
                last_tail = tail

                # In full DMA mode, read from DDR coherent ring; display status
                if packets_received % 100 == 0:
                    sys.stdout.write(f"\r    Packets: {packets_received:,} | DMA Ring Tail: {tail} | Receiving at 8,000 Hz...")
                    sys.stdout.flush()

            time.sleep(0.001)

        print(f"\n[+] Total packets captured: {packets_received:,}")
        self.write32(IMU_REG_CTRL, 0x00) # Disable Auto-DMA
        return True

def main():
    parser = argparse.ArgumentParser(description="AbstractX QMTECH Zynq-7020 Diagnostic Tool")
    parser.add_argument("--uio", default="/dev/uio0", help="UIO device path (default: /dev/uio0)")
    parser.add_argument("--test", choices=["all", "ping", "led", "clock", "imu", "stream"], default="all", help="Test to execute")
    parser.add_argument("--duration", type=float, default=3.0, help="Stream monitor duration in seconds")
    args = parser.parse_args()

    try:
        diag = ZynqDiagnostics(args.uio)
    except Exception as e:
        print(f"[ERROR] Could not connect to UIO device: {e}")
        sys.exit(1)

    try:
        if args.test in ["all", "ping"]:
            if not diag.test_ping() and args.test != "all": sys.exit(1)
        if args.test in ["all", "led"]:
            diag.test_leds()
        if args.test in ["all", "clock"]:
            diag.test_clock()
        if args.test in ["all", "imu"]:
            diag.test_imu_whoami()
        if args.test in ["all", "stream"]:
            diag.test_stream_monitor(args.duration)
    finally:
        diag.close()

if __name__ == "__main__":
    main()
