#!/usr/bin/env python3
# @verifies [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Every RPMsg DMA pool an E906 overlay gives remoteproc (shared-dma-pool) must be one of the host
windows the firmware accepts in translate_buf() ([SPEC-E906-IPC-02]). 2026-10-28: the firmware
accepted only the SRAM pool while the board's flight-stack overlay uses vdev@48000000, so the
vrings were refused and no channel was announced; host tests set their own window and passed.
Plain python3 or pytest.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RPMSG = ROOT / "targets/allwinner_e906/src/rpmsg.cpp"
OVERLAYS = [
    "apps/e906_repl_app/cubie-a5e-flight-stack-e906.dtso",
    "apps/gps_imu_app/platforms/allwinner_e906/cubie-a5e-abstractx-rpmsg-sram.dtso",
    "apps/gps_imu_app/platforms/allwinner_e906/cubie_a5e_e906_overlay.dtso",
]


def firmware_windows(src):
    """(host, size) of each riscv HostWindow entry in rpmsg.cpp."""
    m = re.search(r"#if defined\(__riscv\)(.*?)#else", src, re.S)
    assert m, "riscv window table not found"
    return [(int(h, 16), int(sz, 16))
            for h, _core, sz in re.findall(r"\{\s*(0x[0-9a-fA-F]+)u?\s*,\s*(0x[0-9a-fA-F]+)u?\s*,\s*(0x[0-9a-fA-F]+)u?\s*\}", m.group(1))]


def overlay_pools(text):
    """(base, size) of each shared-dma-pool node."""
    out = []
    for m in re.finditer(r"compatible\s*=\s*\"shared-dma-pool\"\s*;\s*reg\s*=\s*<\s*0x0\s+(0x[0-9a-fA-F]+)\s+0x0\s+(0x[0-9a-fA-F]+)\s*>", text):
        out.append((int(m.group(1), 16), int(m.group(2), 16)))
    return out


def test_every_overlay_pool_is_an_accepted_window():
    wins = firmware_windows(RPMSG.read_text())
    missing = []
    for rel in OVERLAYS:
        for base, size in overlay_pools((ROOT / rel).read_text()):
            if not any(h <= base and base + size <= h + sz for h, sz in wins):
                missing.append(f"{rel}: pool 0x{base:08x}+0x{size:x}")
    assert not missing, "firmware refuses these RPMsg pools: " + "; ".join(missing)


def test_parsers_find_something():
    assert firmware_windows(RPMSG.read_text())
    assert overlay_pools((ROOT / OVERLAYS[0]).read_text()) == [(0x48000000, 0x100000)]


if __name__ == "__main__":
    test_parsers_find_something()
    test_every_overlay_pool_is_an_accepted_window()
    print("[PASS] every E906 overlay RPMsg pool is a firmware window")
