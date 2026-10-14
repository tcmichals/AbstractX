#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail the build if an E906 firmware ELF loads outside what remoteproc accepts.

sunxi_rproc_da_to_va() only translates the two E906 SRAM windows
(E906_SRAM_SPACE{0,1}_DA in drivers/remoteproc/sunxi_rproc.h); anything else
must fall in a DRAM carveout declared in the device tree. A segment outside
these makes remoteproc refuse the firmware at load time on the board, so catch
it at link time instead.

@impl [SPEC-E906-MEM-01] targets/allwinner_e906/SPECIFICATION.md
"""

import argparse
import re
import subprocess
import sys

# (name, start, end) in E906 core addresses, end exclusive.
WINDOWS = [
    # Space 0 minus the 64 KB shared tail at 0x3FFF0000 (host IPC, crash dump).
    ("SRAM Space 0 (firmware)", 0x3FFC0000, 0x3FFF0000),
    # Lower half of Space 1; the upper half (0x40020000) is the Linux RPMsg pool.
    ("SRAM Space 1 (firmware)", 0x40000000, 0x40020000),
    # rproc_dram: dram@48100000 in the cubie-a5e flight-stack overlay.
    ("DRAM carveout", 0x48100000, 0x48200000),
]

LOAD_RE = re.compile(
    r"^\s*LOAD\s+0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)\s+0x[0-9a-fA-F]+\s+"
    r"0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)")


def load_segments(readelf, elf):
    out = subprocess.run([readelf, "-lW", elf], capture_output=True, text=True,
                         check=True).stdout
    for line in out.splitlines():
        m = LOAD_RE.match(line)
        if m:
            vaddr, memsz = int(m.group(1), 16), int(m.group(2), 16)
            if memsz:
                yield vaddr, memsz


def window_for(vaddr, memsz):
    for name, start, end in WINDOWS:
        if start <= vaddr and vaddr + memsz <= end:
            return name
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--readelf", default="riscv-none-elf-readelf")
    parser.add_argument("elf")
    args = parser.parse_args()

    bad = 0
    for vaddr, memsz in load_segments(args.readelf, args.elf):
        name = window_for(vaddr, memsz)
        span = f"0x{vaddr:08X}-0x{vaddr + memsz - 1:08X}"
        if name:
            print(f"  [ok]   {span} in {name}")
        else:
            print(f"  [FAIL] {span} is outside every remoteproc-loadable window")
            bad += 1
    if bad:
        print(f"{args.elf}: {bad} segment(s) cannot be loaded by sunxi_rproc", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
