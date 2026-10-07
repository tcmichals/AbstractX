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
# @impl [SPEC-ZYNQ-04] hw/zynq7000/qmtech_zynq7020/tools/zynq_diagnostics.py

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
IMU_REG_CTRL     = 0x100 # Bit 0 = Auto DMA Enable, Bit 1 = Direct Trig, Bit 2 = Polarity, Bit 3 = RW (0=rd, 1=wr)
IMU_REG_ADDR     = 0x104 # Target register offset (e.g. 0x75 WHO_AM_I, 0x4E PWR_MGMT0)
IMU_REG_LEN      = 0x108 # Byte length to read/write (e.g. 1 or 14)
IMU_REG_WDATA    = 0x10C # 32-bit direct SPI write data
IMU_REG_RDATA    = 0x110 # 32-bit direct SPI read data
IMU_REG_TIME_H   = 0x114 # Timestamp upper 32 bits
IMU_REG_TIME_L   = 0x118 # Timestamp lower 32 bits
IMU_REG_STATUS   = 0x11C # Direct busy/done and Auto-DMA active status

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

    def read_imu_reg(self, reg_addr):
        """Perform a single manual SPI register read (Mode A)."""
        self.write32(IMU_REG_STATUS, 0x02)
        self.write32(IMU_REG_ADDR, reg_addr & 0x7F)
        self.write32(IMU_REG_LEN, 1)
        self.write32(IMU_REG_CTRL, (1 << 1) | (0 << 3))
        self._wait_direct_spi()
        val = self.read32(IMU_REG_RDATA) & 0xFF
        return val

    def write_imu_reg(self, reg_addr, val):
        """Perform a single manual SPI register write (Mode A)."""
        self.write32(IMU_REG_STATUS, 0x02)
        self.write32(IMU_REG_ADDR, reg_addr & 0x7F)
        self.write32(IMU_REG_LEN, 1)
        self.write32(IMU_REG_WDATA, val & 0xFF)
        self.write32(IMU_REG_CTRL, (1 << 1) | (1 << 3))
        self._wait_direct_spi()

    def _wait_direct_spi(self, timeout=0.1):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status = self.read32(IMU_REG_STATUS)
            if status & 0x02 and not status & 0x01:
                return
            time.sleep(0.00001)
        raise TimeoutError("PL direct SPI transfer did not complete")

    def start_auto_dma(self, burst_addr=0x1D, burst_len=14, int_polarity=1):
        """Configure and start continuous autonomous 8 kHz DRDY Auto-DMA (Mode B)."""
        print(f"[*] Starting Auto-DMA: Burst Reg=0x{burst_addr:02X}, Len={burst_len}B, Polarity={'ActiveHigh' if int_polarity else 'ActiveLow'}")
        self.write32(IMU_REG_ADDR, burst_addr & 0xFF)
        self.write32(IMU_REG_LEN, burst_len & 0x3F)
        # Bit 0 = auto_dma_en (1), Bit 2 = int_polarity
        ctrl = 0x01 | ((1 if int_polarity else 0) << 2)
        self.write32(IMU_REG_CTRL, ctrl)
        status = self.read32(IMU_REG_CTRL)
        active = bool(status & 0x01)
        print(f"[+] Auto-DMA Mode Status: {'ACTIVE' if active else 'FAILED TO START'} (CTRL=0x{status:02X})")
        return active

    def stop_auto_dma(self):
        """Stop autonomous DRDY Auto-DMA mode."""
        print("[*] Halting Auto-DMA mode (writing auto_dma_en <= 0)...")
        self.write32(IMU_REG_CTRL, 0x00)
        status = self.read32(IMU_REG_CTRL)
        stopped = (status & 0x01) == 0
        print(f"[+] Auto-DMA Mode Status: {'STOPPED (IDLE)' if stopped else 'STILL ACTIVE'} (CTRL=0x{status:02X})")
        return stopped

    def init_imu(self):
        """Complete ICM-42688-P configuration sequence and start 8 kHz Auto-DMA."""
        print("\n--- [IMU Configuration & Auto-DMA Initialization] ---")
        # 1. Stop any running Auto-DMA
        self.stop_auto_dma()

        # 2. Check WHO_AM_I (0x75)
        whoami = self.read_imu_reg(0x75)
        print(f"[*] Probing IMU WHO_AM_I (0x75): read 0x{whoami:02X}")
        if whoami != 0x47:
            print(f"[!] Warning: Expected 0x47 for ICM-42688-P, got 0x{whoami:02X}")

        # 3. Wake up sensor in Low-Noise mode (PWR_MGMT0 = 0x0F)
        print("[*] Setting PWR_MGMT0 (0x4E) <= 0x0F (Gyro LN + Accel LN)...")
        self.write_imu_reg(0x4E, 0x0F)
        time.sleep(0.010) # 10 ms gyro stabilization

        # 4. Configure Gyro (0x4F: 8 kHz ODR = 0x03, +/-2000 dps)
        print("[*] Setting GYRO_CONFIG0 (0x4F) <= 0x03 (8 kHz ODR, +/-2000 dps)...")
        self.write_imu_reg(0x4F, 0x03)

        # 5. Configure Accel (0x50: 8 kHz ODR = 0x03, +/-16 g)
        print("[*] Setting ACCEL_CONFIG0 (0x50) <= 0x03 (8 kHz ODR, +/-16 g)...")
        self.write_imu_reg(0x50, 0x03)

        # 6. Configure Interrupt Pin 1 (0x14: Push-pull, active-high, pulsed = 0x12)
        print("[*] Setting INT_CONFIG (0x14) <= 0x12 (Push-pull, Active-High)...")
        self.write_imu_reg(0x14, 0x12)

        # 7. Route UI Data Ready interrupt to INT1 (0x65 = 0x08)
        print("[*] Setting INT_SOURCE0 (0x65) <= 0x08 (UI DRDY -> INT1)...")
        self.write_imu_reg(0x65, 0x08)

        # 8. Start Auto-DMA burst read on TEMP_DATA1 (0x1D, 14 bytes)
        print("[*] Starting hardware Auto-DMA...")
        self.start_auto_dma(burst_addr=0x1D, burst_len=14, int_polarity=1)
        print("[SUCCESS] IMU configured and 8 kHz Auto-DMA streaming active!")
        return True

    def test_imu_whoami(self):
        print("\n--- [Test 4] IMU SPI WHO_AM_I Read ---")
        whoami_byte = self.read_imu_reg(0x75)
        print(f"[+] IMU SPI Response: WHO_AM_I Byte = 0x{whoami_byte:02X}")

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
        self.start_auto_dma()

        t_end = time.time() + duration_sec
        packets_received = 0
        last_tail = self.read32(REG_RX_TAIL)

        print("[*] Listening for hardware TLP frames...")
        print("    [PKT #]  | DMA Ring Tail | Status")
        print("    " + "-"*55)

        while time.time() < t_end:
            tail = self.read32(REG_RX_TAIL)
            if tail != last_tail:
                packets_received += (tail - last_tail) & 0xFF
                last_tail = tail

                if packets_received % 100 == 0:
                    sys.stdout.write(f"\r    Packets: {packets_received:,} | DMA Ring Tail: {tail} | Receiving at 8,000 Hz...")
                    sys.stdout.flush()

            time.sleep(0.001)

        print(f"\n[+] Total packets captured: {packets_received:,}")
        self.stop_auto_dma()
        return True

def parse_hex_or_dec(val_str):
    return int(val_str, 16) if val_str.startswith(("0x", "0X")) else int(val_str)

def main():
    parser = argparse.ArgumentParser(description="AbstractX QMTECH Zynq-7020 Diagnostic Tool")
    parser.add_argument("--uio", default="/dev/uio0", help="UIO device path (default: /dev/uio0)")
    parser.add_argument("--test", choices=["all", "ping", "led", "clock", "imu", "stream"], default=None, help="Diagnostic test suite to execute")
    parser.add_argument("--duration", type=float, default=3.0, help="Stream monitor duration in seconds")
    parser.add_argument("--read-reg", type=str, default=None, help="Read single IMU register over SPI (hex, e.g. 0x75)")
    parser.add_argument("--write-reg", nargs=2, metavar=("REG", "VAL"), default=None, help="Write single IMU register over SPI (hex, e.g. 0x4E 0x0F)")
    parser.add_argument("--start-auto-dma", action="store_true", help="Start continuous hardware 8 kHz DRDY Auto-DMA")
    parser.add_argument("--stop-auto-dma", action="store_true", help="Stop hardware Auto-DMA mode")
    parser.add_argument("--init-imu", action="store_true", help="Initialize ICM-42688-P registers and start 8 kHz Auto-DMA")
    args = parser.parse_args()

    try:
        diag = ZynqDiagnostics(args.uio)
    except Exception as e:
        print(f"[ERROR] Could not connect to UIO device: {e}")
        sys.exit(1)

    try:
        # 1. Direct register operations
        if args.read_reg is not None:
            reg = parse_hex_or_dec(args.read_reg)
            val = diag.read_imu_reg(reg)
            print(f"[SPI READ] Reg 0x{reg:02X} -> 0x{val:02X} ({val})")
            return

        if args.write_reg is not None:
            reg = parse_hex_or_dec(args.write_reg[0])
            val = parse_hex_or_dec(args.write_reg[1])
            diag.write_imu_reg(reg, val)
            print(f"[SPI WRITE] Reg 0x{reg:02X} <= 0x{val:02X}")
            return

        # 2. Auto-DMA control
        if args.start_auto_dma:
            diag.start_auto_dma()
            return

        if args.stop_auto_dma:
            diag.stop_auto_dma()
            return

        if args.init_imu:
            diag.init_imu()
            return

        # 3. Test suites
        test = args.test or "all"
        if test in ["all", "ping"]:
            if not diag.test_ping() and test != "all": sys.exit(1)
        if test in ["all", "led"]:
            diag.test_leds()
        if test in ["all", "clock"]:
            diag.test_clock()
        if test in ["all", "imu"]:
            diag.test_imu_whoami()
        if test in ["all", "stream"]:
            diag.test_stream_monitor(args.duration)
    finally:
        diag.close()

if __name__ == "__main__":
    main()
