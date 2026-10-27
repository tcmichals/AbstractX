#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Third-party PIO programs for conformance tests ([SPEC-PIO-14]).

  python3 tools/pio_examples.py --vendor DIR   # copy pico-examples .pio files (BSD-3) from a checkout
  python3 tools/pio_examples.py --list         # every program the tests can load, and its size

pico-examples sources are vendored in rtl/pio/programs/examples/pico/ (images by
tools/pio_programs.py). Betaflight (GPL-3.0) is never copied here: programs are read from a
checkout at $BETAFLIGHT_DIR (default ../flightcode/betaflight next to this repository).
"""
# @impl [SPEC-PIO-14] rtl/pio/SPECIFICATION.md

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pio_programs import ROOT, assemble, find_pioasm, load_program  # noqa: E402

PICO_DIR = ROOT / "rtl" / "pio" / "programs" / "examples" / "pico"
PICO_COMMIT = "0d62f75bafc2c8120d3276c3343d1a9195e909e9"     # raspberrypi/pico-examples, 2026-09-03
BF_COMMIT = "49679ccdfa4abc2ca5ab4822d08f51a5bcdaaf5e"       # betaflight/betaflight master, 2026-10-27
BF_PIO = ["src/platform/PICO/dshot.pio", "src/platform/PICO/osd/osd_tx.pio"]
BF_C = {   # program name -> (file, array name, wrap_target, wrap, sideset {size incl. opt, optional, pindirs})
    "bf_uart_tx": ("src/platform/PICO/uart/uart_tx_program.c", "uart_tx_program_instructions", None, None,
                   {"size": 3, "optional": True, "pindirs": False}),
    "bf_uart_rx": ("src/platform/PICO/uart/uart_rx_program.c", "uart_rx_program_instructions", None, None,
                   {"size": 0, "optional": False, "pindirs": False}),
    "bf_ws2812": ("src/platform/PICO/light_ws2811strip_pico.c", "ws2812_program_instructions", None, None,
                  {"size": 1, "optional": False, "pindirs": False}),
}


def betaflight_dir() -> Path:
    return Path(os.environ.get("BETAFLIGHT_DIR", ROOT.parent / "flightcode" / "betaflight"))


def betaflight_commit():
    d = betaflight_dir()
    try:
        return subprocess.run(["git", "-C", str(d), "rev-parse", "HEAD"], capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def parse_c_program(text: str, array: str):
    """Instruction words and .wrap_target / .wrap positions from a pioasm-generated C array."""
    m = re.search(r"%s\[\]\s*=\s*\{(.*?)\};" % re.escape(array), text, re.S)
    if not m:
        raise KeyError(array)
    words, wrap_target, wrap = [], 0, None
    for line in m.group(1).splitlines():
        if ".wrap_target" in line:
            wrap_target = len(words)
        elif re.search(r"//\s*\.wrap\b", line):
            wrap = len(words) - 1
        w = re.match(r"\s*0x([0-9a-fA-F]{4}),", line)
        if w:
            words.append(int(w.group(1), 16))
    return words, wrap_target, (len(words) - 1 if wrap is None else wrap)


def load_betaflight(name: str) -> dict:
    """A Betaflight program from the checkout; raises FileNotFoundError when it is missing."""
    d = betaflight_dir()
    if not (d / "src" / "platform" / "PICO").is_dir():
        raise FileNotFoundError(f"no Betaflight checkout at {d} (set BETAFLIGHT_DIR)")
    if name in BF_C:
        f, array, _, _, sideset = BF_C[name]
        words, wt, wr = parse_c_program((d / f).read_text(), array)
        return {"name": name, "instructions": words, "wrap_target": wt, "wrap": wr, "origin": -1,
                "sideset": sideset, "labels": {}}
    pioasm = find_pioasm()
    if not pioasm:
        raise FileNotFoundError("pioasm not found")
    for rel in BF_PIO:
        for p in assemble(pioasm, d / rel, root=d)["programs"]:
            if p["name"] == name:
                return p
    raise KeyError(name)


def vendor(src_root: Path):
    srcs = sorted(src_root.rglob("*.pio"))
    if PICO_DIR.exists():
        shutil.rmtree(PICO_DIR)
    for s in srcs:
        dst = PICO_DIR / s.relative_to(src_root)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(s, dst)
    shutil.copyfile(src_root / "LICENSE.TXT", PICO_DIR / "LICENSE.TXT")
    print(f"vendored {len(srcs)} .pio files into {PICO_DIR.relative_to(ROOT)}; "
          f"run tools/pio_programs.py to assemble them")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--vendor", metavar="PICO_EXAMPLES_DIR")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if a.vendor:
        vendor(Path(a.vendor))
        return 0
    if a.list:
        import json
        for img in sorted(PICO_DIR.rglob("*.json")):
            for p in json.loads(img.read_text())["programs"]:
                print(f"pico  {p['name']:30s} {len(p['instructions']):2d} words")
        c = betaflight_commit()
        names = list(BF_C) + ["dshot_600", "dshot_600_bidir", "osd_tx_pal", "osd_tx_ntsc", "osd_count_sync"]
        for n in names:
            try:
                p = load_betaflight(n)
                print(f"bf    {n:30s} {len(p['instructions']):2d} words")
            except (FileNotFoundError, KeyError) as e:
                print(f"bf    {n:30s} not available: {e}")
        print(f"betaflight checkout: {betaflight_dir()} @ {c or 'missing'} (tests pinned to {BF_COMMIT[:12]})")
        return 0
    ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())


__all__ = ["load_program", "load_betaflight", "betaflight_commit", "BF_COMMIT", "PICO_COMMIT"]
