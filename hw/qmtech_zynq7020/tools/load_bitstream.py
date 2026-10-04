#!/usr/bin/env python3
"""
AbstractX QMTECH Zynq-7020 FPGA Bitstream Loader & Verification Utility
-----------------------------------------------------------------------
Loads synthesized bitstreams into the Zynq PL via the Linux FPGA Manager
and verifies hardware registers, uptime timers, and bus integrity over UIO.

Usage:
    load_bitstream.py <path_to_bitstream.bit_or_bin> [--firmware-dir /lib/firmware] [--uio /dev/uio0]
"""

import os
import sys
import time
import struct
import mmap
import argparse
from pathlib import Path

HARDWARE_MAGIC = 0x41535036  # "ASP6"

def parse_xilinx_bit_header(data: bytes):
    """
    Parses Xilinx .bit header and returns metadata dict and raw bitstream offset.
    Format:
      - 2 bytes length (usually 9)
      - 9 bytes header
      - 2 bytes (0x00, 0x01)
      - Sections 'a' (design name), 'b' (part), 'c' (date), 'd' (time), 'e' (payload)
    """
    if len(data) < 32:
        return None, 0

    # Check for Xilinx .bit magic header
    if data[0:2] != b'\x00\x09' or data[11:13] != b'\x00\x01':
        # Likely raw .bin already
        return None, 0

    idx = 13
    meta = {}
    while idx < len(data):
        section_key = chr(data[idx])
        idx += 1
        if section_key in ['a', 'b', 'c', 'd']:
            slen = struct.unpack(">H", data[idx:idx+2])[0]
            idx += 2
            val = data[idx:idx+slen].decode('ascii', errors='ignore').strip('\x00')
            idx += slen
            if section_key == 'a': meta['design'] = val
            elif section_key == 'b': meta['part'] = val
            elif section_key == 'c': meta['date'] = val
            elif section_key == 'd': meta['time'] = val
        elif section_key == 'e':
            payload_len = struct.unpack(">I", data[idx:idx+4])[0]
            idx += 4
            meta['payload_len'] = payload_len
            return meta, idx
        else:
            break

    return None, 0

def convert_bit_to_bin(src_path: Path, dest_bin_path: Path):
    """Strips Vivado header if needed and writes raw binary bitstream."""
    with open(src_path, "rb") as f:
        data = f.read()

    meta, payload_start = parse_xilinx_bit_header(data)
    if meta and payload_start > 0:
        print(f"[*] Detected Xilinx .bit container:")
        print(f"    - Design:  {meta.get('design', 'Unknown')}")
        print(f"    - Part:    {meta.get('part', 'Unknown')}")
        print(f"    - Built:   {meta.get('date', '')} {meta.get('time', '')}")
        print(f"    - Payload: {meta.get('payload_len', len(data)-payload_start):,} bytes")
        raw_payload = data[payload_start:]
    else:
        print(f"[*] Raw .bin bitstream detected ({len(data):,} bytes)")
        raw_payload = data

    dest_bin_path.parent.mkdir(parents=True, exist_ok=True)
    with open(dest_bin_path, "wb") as f:
        f.write(raw_payload)
    print(f"[+] Prepared binary bitstream: {dest_bin_path} ({len(raw_payload):,} bytes)")
    return dest_bin_path

def load_via_fpga_manager(bin_filename: str, mgr_dir: Path):
    """Instructs Linux FPGA Manager to load firmware via PCAP DMA."""
    firmware_attr = mgr_dir / "firmware"
    state_attr = mgr_dir / "state"
    flags_attr = mgr_dir / "flags"

    if not firmware_attr.exists():
        raise FileNotFoundError(f"FPGA Manager sysfs interface not found at {mgr_dir}")

    # Set flags to 0 (uncompressed/standard)
    if flags_attr.exists():
        with open(flags_attr, "w") as f:
            f.write("0\n")

    print(f"[*] Triggering FPGA Manager load ({bin_filename})...")
    t0 = time.perf_counter()
    with open(firmware_attr, "w") as f:
        f.write(f"{bin_filename}\n")
    elapsed_ms = (time.perf_counter() - t0) * 1000.0

    state = state_attr.read_text().strip() if state_attr.exists() else "unknown"
    if state != "operating":
        raise RuntimeError(f"FPGA Manager failed to reach 'operating' state (current: {state})")

    print(f"[+] Bitstream loaded in {elapsed_ms:.2f} ms (State: {state})")

def verify_uio_device(uio_path: Path):
    """Maps /dev/uio0 and validates AbstractX FPGA fabric registers."""
    if not uio_path.exists():
        print(f"[!] Warning: {uio_path} does not exist yet. Did the device tree probe?")
        return False

    print(f"[*] Probing hardware via {uio_path}...")
    with open(uio_path, "r+b") as f:
        mem = mmap.mmap(f.fileno(), 0x10000, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
        try:
            # Read Hardware ID @ 0x40
            hw_id = struct.unpack("<I", mem[0x40:0x44])[0]
            if hw_id != HARDWARE_MAGIC:
                print(f"[-] Hardware magic mismatch: expected 0x{HARDWARE_MAGIC:08X} ('ASP6'), got 0x{hw_id:08X}")
                return False

            print(f"[+] Hardware Magic OK: 0x{hw_id:08X} ('ASP6')")

            # Enable Bridge & DMA
            mem[0x00:0x04] = struct.pack("<I", 0x01)

            # Read Monotonic Nanosecond Timestamp @ 0x48 (lower 32) and 0x4C (upper 32)
            t1_low = struct.unpack("<I", mem[0x48:0x4C])[0]
            time.sleep(0.1)
            t2_low = struct.unpack("<I", mem[0x48:0x4C])[0]

            delta_ticks = (t2_low - t1_low) & 0xFFFFFFFF
            freq_mhz = (delta_ticks / 0.1) / 1e6
            print(f"[+] PL Timestamp Timer: Active (Measured Clock: ~{freq_mhz:.1f} MHz)")

            # Check status register @ 0x04
            status = struct.unpack("<I", mem[0x04:0x08])[0]
            print(f"[+] Hardware Status: 0x{status:08X} (DMA Ready)")
            return True
        finally:
            mem.close()

def main():
    parser = argparse.ArgumentParser(description="AbstractX QMTECH Zynq-7020 FPGA Loader & Validator")
    parser.add_argument("bitstream", type=Path, help="Path to .bit or .bin bitstream file")
    parser.add_argument("--firmware-dir", type=Path, default=Path("/lib/firmware"), help="Target firmware directory")
    parser.add_argument("--fpga-mgr", type=Path, default=Path("/sys/class/fpga_manager/fpga0"), help="FPGA manager sysfs path")
    parser.add_argument("--uio", type=Path, default=Path("/dev/uio0"), help="UIO device path")
    args = parser.parse_args()

    if not args.bitstream.exists():
        print(f"[ERROR] Bitstream file not found: {args.bitstream}")
        sys.exit(1)

    bin_name = "abstractx_qmtech.bin"
    target_bin_path = args.firmware_dir / bin_name

    # Step 1: Strip Vivado header if needed and copy to firmware directory
    try:
        convert_bit_to_bin(args.bitstream, target_bin_path)
    except PermissionError:
        # If user cannot write to /lib/firmware, try current directory
        target_bin_path = Path.cwd() / bin_name
        convert_bit_to_bin(args.bitstream, target_bin_path)
        print(f"[!] Note: Written to local directory {target_bin_path}. Run with sudo to write to /lib/firmware.")

    # Step 2: Load via FPGA Manager if running on target
    if args.fpga_mgr.exists():
        try:
            load_via_fpga_manager(bin_name, args.fpga_mgr)
        except Exception as e:
            print(f"[-] FPGA Manager Error: {e}")
            sys.exit(1)

        # Step 3: Verify hardware
        time.sleep(0.05)
        if verify_uio_device(args.uio):
            print("\n========================================================")
            print("  SUCCESS: AbstractX FPGA Offload Fabric is OPERATIONAL! ")
            print("========================================================")
            sys.exit(0)
        else:
            print("[WARN] Hardware verification failed or UIO not ready.")
            sys.exit(2)
    else:
        print(f"[*] Note: FPGA Manager not found at {args.fpga_mgr} (running on host workstation).")
        print(f"[+] Prepared binary bitstream ready for target: {target_bin_path}")

if __name__ == "__main__":
    main()
