#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
uio_mmio.py - Safe 32-bit register access to a Linux UIO map (stdlib only).

* The device is found by its UIO name (/sys/class/uio/uioN/name), not assumed to be uio0.
* The map size comes from /sys/class/uio/uioN/maps/mapM/size; map M is at offset M * page size.
* Every access is one aligned 32-bit load or store through memoryview.cast('I'). mmap slices and
  struct.*_from go through memcpy, whose access width is not guaranteed. asp_axi_lite_bridge.sv
  ignores wstrb, and its FIFO ports (TLP_IN_PORT 0x10, TLP_OUT_PORT 0x14) have side effects, so a
  byte-wise copy would push or pop four times.
"""

import glob
import mmap
import os
from pathlib import Path


def find_uio(name: str) -> Path:
    """/dev/uioN whose sysfs name matches, e.g. 'abstractx'."""
    for d in sorted(glob.glob("/sys/class/uio/uio[0-9]*")):
        try:
            if Path(d, "name").read_text().strip() == name:
                return Path("/dev") / os.path.basename(d)
        except OSError:
            pass
    raise FileNotFoundError(f"no UIO device named '{name}' (is the overlay loaded?)")


def map_size(uio: Path, index: int = 0) -> int:
    p = Path("/sys/class/uio") / uio.name / "maps" / f"map{index}" / "size"
    if not p.exists():
        raise FileNotFoundError(f"{uio} has no map{index} ({p} missing)")
    return int(p.read_text().strip(), 0)


class UioRegion:
    """One UIO map with 32-bit accessors. Use as a context manager."""

    def __init__(self, uio: Path, index: int = 0):
        self.uio = Path(uio)
        self.size = map_size(self.uio, index)
        self._fd = os.open(self.uio, os.O_RDWR | os.O_SYNC)
        self._mm = None
        self._words = None
        try:
            self._mm = mmap.mmap(self._fd, self.size, mmap.MAP_SHARED,
                                 mmap.PROT_READ | mmap.PROT_WRITE,
                                 offset=index * mmap.PAGESIZE)
            self._words = memoryview(self._mm).cast("I")
        except BaseException:
            self.close()
            raise

    @property
    def fd(self) -> int:
        return self._fd

    def _index(self, offset: int) -> int:
        if offset % 4 or not 0 <= offset <= self.size - 4:
            raise ValueError(f"bad register offset 0x{offset:X} for a {self.size}-byte map")
        return offset // 4

    def read32(self, offset: int) -> int:
        return self._words[self._index(offset)]

    def write32(self, offset: int, value: int) -> None:
        self._words[self._index(offset)] = value & 0xFFFFFFFF

    def close(self) -> None:
        if self._words is not None:
            self._words.release()
            self._words = None
        if self._mm is not None:
            self._mm.close()
            self._mm = None
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
