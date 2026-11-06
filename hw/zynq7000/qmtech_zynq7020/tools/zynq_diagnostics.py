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
SYS (0x4000_00xx) and PIO (0x4000_07xx) registers are reachable only through CONTROL TLPs
([SPEC-ZYNQ-03], little-endian [SPEC-TLP-01]), which this tool builds (mem_read32 / mem_write32).
The tool takes an exclusive lock on the UIO device: stop the AbstractX host first (EL-1027-22).
"""
# @impl [SPEC-ZYNQ-01] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md

import collections
import fcntl
import os
import struct
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


# AXI4-Lite addresses behind the control plane (CONTROL TLPs, [SPEC-TLP-04])
SYS_REG_SCRATCH  = 0x40000008  # Scratch Register (asp_sys_regs.sv 0x08)
SYS_REG_LED_CTRL = 0x4000000C  # Bit 0 = Carrier LED (P22), Bit 1 = Core LED (M14) (asp_sys_regs.sv 0x0C)
SYS_REG_TIME_L   = 0x40000010  # Lower 32 bits of 64-bit nanosecond timer
SYS_REG_TIME_H   = 0x40000014  # Upper 32 bits


HARDWARE_MAGIC   = 0x41535036  # "ASP6"

TLP_MEM_RD, TLP_MEM_WR, TLP_CPL_D = 0x01, 0x02, 0x03
CH_CONTROL = 0x01
TLP_FMT = "<BBBBIHHQ40sI"     # asp_tlp64_t, little-endian ([SPEC-TLP-01])


# @impl [SPEC-ZYNQ-03] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md (word order, little-endian)
def tlp_words(typ, tag, addr, words, flags=0, channel=CH_CONTROL, seq=0, ts=0):
    """The 16 DWORDs of a TLP for REG_TLP_IN_PORT, DW0 first: DW k = bytes 4k..4k+3 little-endian,
    bits [32k+31:32k] of the router vector ([SPEC-ZYNQ-03], rtl/asp_tlp_pkg.sv)."""
    payload = b"".join(struct.pack("<I", w & 0xFFFFFFFF) for w in words[:10])
    raw = struct.pack(TLP_FMT, typ, flags, tag, channel, addr & 0xFFFFFFFF, max(len(words), 1),
                      seq & 0xFFFF, ts, payload, 0)
    return list(struct.unpack("<16I", raw))


def parse_tlp_words(dwords):
    """Inverse of tlp_words for 16 DWORDs read from REG_TLP_OUT_PORT."""
    typ, flags, tag, ch, addr, n, seq, ts, payload, _ = struct.unpack(TLP_FMT, struct.pack("<16I", *dwords))
    return {"type": typ, "flags": flags, "tag": tag, "channel": ch, "addr": addr, "len": n, "seq": seq,
            "ts": ts, "words": [struct.unpack_from("<I", payload, 4 * k)[0] for k in range(min(n, 10))]}

class ZynqDiagnostics:
    def __init__(self, uio_path=None):
        self.uio_path = Path(uio_path) if uio_path else find_uio(UIO_NAME)
        if not self.uio_path.exists():
            raise FileNotFoundError(f"{self.uio_path} not found. Is generic-uio loaded?")
        self.region = UioRegion(self.uio_path, 0)  # size from sysfs; closes its fd on failure
        try:
            # One user of the TLP ports ([SPEC-ZYNQ-03]): same lock as ZynqUioBridge::open_uio
            fcntl.flock(self.region.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.region.close()
            raise RuntimeError(f"{self.uio_path} is in use (another tool or the AbstractX host holds it)")
        self.tag_counter = 1
        self.unclaimed = collections.deque(maxlen=256)    # TLPs read while waiting for a completion

    def close(self):
        self.region.close()

    def read_csr(self, offset):
        return self.region.read32(offset)

    def write_csr(self, offset, val):
        self.region.write32(offset, val)

    def write_tlp(self, words):
        """Write 16 DWORDs to REG_TLP_IN_PORT to assemble and commit a 64B TLP (DW0 first)."""
        for w in words:
            self.write_csr(REG_TLP_IN_PORT, w)

    def read_tlp(self):
        """Read 16 DWORDs from REG_TLP_OUT_PORT to unpack and consume a 64B TLP."""
        return [self.read_csr(REG_TLP_OUT_PORT) for _ in range(16)]

    def _next_tag(self):
        tag = self.tag_counter & 0xFF
        self.tag_counter = (self.tag_counter % 255) + 1          # 1..255; 0 is "no completion"
        return tag

    def mem_write32(self, addr, val):
        """Posted MemWrite CONTROL TLP ([SPEC-TLP-04]); no completion."""
        self.write_tlp(tlp_words(TLP_MEM_WR, self._next_tag(), addr, [val]))

    # @impl [SPEC-ZYNQ-03] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md (completion by tag, others kept)
    def mem_read32(self, addr, timeout=0.1):
        """MemRead CONTROL TLP; waits for the CplD with its tag. Other TLPs read meanwhile are kept in
        self.unclaimed (EL-1027-22), not dropped. A CplD with flags[1:0] != 0 is an AXI error."""
        tag = self._next_tag()
        self.write_tlp(tlp_words(TLP_MEM_RD, tag, addr, []))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.read_csr(REG_EGR_COUNT) > 0:
                t = parse_tlp_words(self.read_tlp())
                if t["type"] == TLP_CPL_D and t["tag"] == tag:
                    if t["flags"] & 3:
                        raise IOError(f"read 0x{addr:08X}: AXI {'SLVERR' if t['flags'] & 3 == 2 else 'DECERR'}")
                    return t["words"][0]
                self.unclaimed.append(t)
                continue
            time.sleep(0.0001)
        raise TimeoutError(f"read from 0x{addr:08X} timed out awaiting CplD tag {tag}")

    def read32(self, offset):
        return self.mem_read32(offset)

    def write32(self, offset, val):
        self.mem_write32(offset, val)

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
                self.read_tlp()
                packets_received += 1
                egr_cnt = self.read_csr(REG_EGR_COUNT)

                if packets_received % 100 == 0:
                    sys.stdout.write(f"\r    Packets: {packets_received:,} | Egress Available: {egr_cnt}")
                    sys.stdout.flush()

            time.sleep(0.001)

        packets_received += len(self.unclaimed)        # kept while waiting for completions
        self.unclaimed.clear()
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
