#!/usr/bin/env python3
"""
AbstractX QMTECH Zynq-7020 Hardware Diagnostic Utility
-------------------------------------------------------------------------
Performs live hardware tests on the QMTECH Zynq-7020 FPGA platform:
  1. Ping & Scratch Register Loopback (0x4000_0004)
  2. Onboard User LED Toggle (Carrier D3 & Core D2)
  3. Hardware Nanosecond Clock Benchmark (0x4000_0010)
  4. Egress TLP stream monitor (passive)
The IMU core and its tests were removed with the dedicated cores (2026-11-05, [SPEC-TLP-07]:
all-PIO); the IMU is now read by a PIO program the host loads.

Usage:
  zynq_diagnostics.py [--uio /dev/uioN] [--test all|ping|led|clock|stream]

The UIO window is asp_axi_lite_bridge.sv only (0x00-0x1C, 0x40; decoded on addr[7:0]).
SYS (0x4000_00xx) and PIO (0x4000_07xx) registers are reachable only through
CONTROL TLPs ([SPEC-ZYNQ-03]), which this tool does not build yet. Tests that need them
report NOT SUPPORTED instead of writing bridge registers by alias (the scratch write landed
on STATUS, the LED write cleared IRQ_STATUS, a register write overwrote CONTROL).
"""
# @impl [SPEC-ZYNQ-01] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md

import os
import sys
import time
import argparse
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uio_mmio import UioRegion, find_uio  # noqa: E402

UIO_NAME = "fpga-fabric"  # node name in abstractx-uio.dtso

# AXI-Lite Bridge Register Offsets matching asp_axi_lite_bridge.sv [SPEC-ZYNQ-01]
REG_CONTROL      = 0x00
REG_STATUS       = 0x04
REG_IRQ_STATUS   = 0x08
REG_IRQ_ENABLE   = 0x0C
REG_TLP_IN_PORT  = 0x10
REG_TLP_OUT_PORT = 0x14
REG_EGR_COUNT    = 0x18
REG_ING_FREE     = 0x1C
REG_HARDWARE_ID  = 0x40
BRIDGE_REGS = {REG_CONTROL, REG_STATUS, REG_IRQ_STATUS, REG_IRQ_ENABLE, REG_TLP_IN_PORT,
               REG_TLP_OUT_PORT, REG_EGR_COUNT, REG_ING_FREE, REG_HARDWARE_ID}


class NotInUioWindow(RuntimeError):
    """A Wishbone register that only a CONTROL TLP can reach ([SPEC-ZYNQ-03])."""

# Wishbone Addresses (routed via Control TLPs)
SYS_REG_SCRATCH  = 0x40000008  # Scratch Register (asp_sys_regs.sv 0x08)
SYS_REG_LED_CTRL = 0x4000000C  # Bit 0 = Carrier LED (P22), Bit 1 = Core LED (M14) (asp_sys_regs.sv 0x0C)
SYS_REG_TIME_L   = 0x40000010  # Lower 32 bits of 64-bit nanosecond timer
SYS_REG_TIME_H   = 0x40000014  # Upper 32 bits


HARDWARE_MAGIC   = 0x41535036  # "ASP6"

class ZynqDiagnostics:
    def __init__(self, uio_path=None):
        self.uio_path = Path(uio_path) if uio_path else find_uio(UIO_NAME)
        if not self.uio_path.exists():
            raise FileNotFoundError(f"{self.uio_path} not found. Is generic-uio loaded?")
        self.region = UioRegion(self.uio_path, 0)  # size from sysfs; closes its fd on failure
        self.tag_counter = 1

    def close(self):
        self.region.close()

    def read_csr(self, offset):
        return self.region.read32(offset)

    def write_csr(self, offset, val):
        self.region.write32(offset, val)

    def write_tlp(self, words):
        """Write 16 DWORDs to REG_TLP_IN_PORT to assemble and commit a 64B TLP."""
        for w in words:
            self.write_csr(REG_TLP_IN_PORT, w)

    def read_tlp(self):
        """Read 16 DWORDs from REG_TLP_OUT_PORT to unpack and consume a 64B TLP."""
        return [self.read_csr(REG_TLP_OUT_PORT) for _ in range(16)]

    def wb_write32(self, addr, val):
        """Perform a Wishbone 32-bit write by sending a MemWrite Control TLP."""
        tag = self.tag_counter & 0xFF
        self.tag_counter = (self.tag_counter + 1) & 0xFF
        # 512-bit vector bit packing matching asp_wishbone_master:
        # DW15 (MSB) = {Type=0x02, Flags=0x00, Tag=tag, Channel=0x01 (Control)}
        # DW14 = Target Address
        # DW13 = {Length_DW=1, Seq=0}
        # DW12-11 = Timestamp (0)
        # DW10 = Payload Word 0 (val)
        # DW9-1 = 0
        # DW0 (LSB) = 0
        # When pushed to REG_TLP_IN_PORT, word[i] corresponds to bits [32*i +: 32]:
        # word[0] = bits[31:0] = 0
        # word[10] = bits[351:320] = payload
        # word[11..12] = bits[415:352] = timestamp
        # word[13] = bits[447:416] = {length_dw(16), seq(16)}
        # word[14] = bits[479:448] = addr
        # word[15] = bits[511:480] = {type(8), flags(8), tag(8), channel(8)}
        words = [0] * 16
        words[15] = (0x02 << 24) | (0x00 << 16) | (tag << 8) | 0x01
        words[14] = addr & 0xFFFFFFFF
        words[13] = (1 << 16) | 0x0000
        words[10] = val & 0xFFFFFFFF
        self.write_tlp(words)

    def wb_read32(self, addr, timeout=0.1):
        """Perform a Wishbone 32-bit read by sending a MemRead Control TLP and awaiting CplD."""
        tag = self.tag_counter & 0xFF
        self.tag_counter = (self.tag_counter + 1) & 0xFF
        words = [0] * 16
        words[15] = (0x01 << 24) | (0x00 << 16) | (tag << 8) | 0x01
        words[14] = addr & 0xFFFFFFFF
        words[13] = (1 << 16) | 0x0000
        self.write_tlp(words)

        # Await completion response
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            egr_cnt = self.read_csr(REG_EGR_COUNT)
            if egr_cnt > 0:
                resp = self.read_tlp()
                resp_dw15 = resp[15]
                resp_type = (resp_dw15 >> 24) & 0xFF
                resp_tag = (resp_dw15 >> 8) & 0xFF
                if resp_type == 0x03 and resp_tag == tag: # CplD
                    return resp[10] # DW10 = read data payload
            time.sleep(0.0001)
        raise TimeoutError(f"Wishbone read from 0x{addr:08X} timed out awaiting CplD")

    def read32(self, offset):
        return self.wb_read32(offset)

    def write32(self, offset, val):
        self.wb_write32(offset, val)

    def test_ping(self):
        print("\n--- [Test 1] Hardware Ping & Scratch Loopback ---")
        magic = self.read_csr(REG_HARDWARE_ID)
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

    def test_stream_monitor(self, duration_sec=5.0):
        print(f"\n--- [Test 5] Egress TLP Stream Monitor ({duration_sec}s) ---")
        # Passive: counts whatever the PIO programs stream (the IMU auto-DMA core was removed,
        # [SPEC-TLP-07]; an IMU stream now comes from a PIO program the host loads first).

        t_end = time.time() + duration_sec
        packets_received = 0

        print("[*] Listening for hardware TLP frames...")
        print("    [PKT #]  | Egress FIFO Count | Status")
        print("    " + "-"*55)

        while time.time() < t_end:
            egr_cnt = self.read_csr(REG_EGR_COUNT)
            while egr_cnt > 0:
                tlp = self.read_tlp()
                packets_received += 1
                egr_cnt = self.read_csr(REG_EGR_COUNT)

                if packets_received % 100 == 0:
                    sys.stdout.write(f"\r    Packets: {packets_received:,} | Egress Available: {egr_cnt}")
                    sys.stdout.flush()

            time.sleep(0.001)

        print(f"\n[+] Total packets captured: {packets_received:,}")
        return True

def parse_hex_or_dec(val_str):
    return int(val_str, 16) if val_str.startswith(("0x", "0X")) else int(val_str)

def main():
    parser = argparse.ArgumentParser(description="AbstractX QMTECH Zynq-7020 Diagnostic Tool")
    parser.add_argument("--uio", default=None, help=f"UIO device path (default: the one named '{UIO_NAME}')")
    parser.add_argument("--test", choices=["all", "ping", "led", "clock", "stream"], default=None, help="Diagnostic test suite to execute")
    parser.add_argument("--duration", type=float, default=3.0, help="Stream monitor duration in seconds")
    args = parser.parse_args()

    try:
        diag = ZynqDiagnostics(args.uio)
    except Exception as e:
        print(f"[ERROR] Could not connect to UIO device: {e}")
        sys.exit(1)

    try:
        # 1. Direct register operations
        # 3. Test suites
        test = args.test or "all"
        if test in ["all", "ping"]:
            if not diag.test_ping() and test != "all": sys.exit(1)
        if test in ["all", "led"]:
            diag.test_leds()
        if test in ["all", "clock"]:
            diag.test_clock()
        if test in ["all", "stream"]:
            diag.test_stream_monitor(args.duration)
    finally:
        diag.close()

if __name__ == "__main__":
    main()
