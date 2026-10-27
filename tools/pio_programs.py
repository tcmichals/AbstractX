#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
PIO program library: assemble rtl/pio/programs/**/*.pio with the Raspberry Pi
pioasm and check the generated images in next to the sources ([SPEC-PIO-11]).

  python3 tools/pio_programs.py           # (re)generate <name>.json for every .pio
  python3 tools/pio_programs.py --check   # exit 1 if a checked-in image differs from pioasm
  python3 tools/pio_programs.py --list    # program names and sizes

pioasm search order: $PIOASM, ~/.tools/pioasm/pioasm, ~/.pico-sdk/tools/*/pioasm/pioasm.
Without pioasm, --check prints "not checked" and exits 2 (never a silent pass).

Tests and host software load images with load_program(name).
"""
# @impl [SPEC-PIO-11] rtl/pio/SPECIFICATION.md

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROGRAMS = ROOT / "rtl" / "pio" / "programs"


def find_pioasm():
    cands = [os.environ.get("PIOASM", ""), str(Path.home() / ".tools/pioasm/pioasm")]
    cands += sorted(glob.glob(str(Path.home() / ".pico-sdk/tools/*/pioasm/pioasm")), reverse=True)
    cands.append(shutil.which("pioasm") or "")
    for c in cands:
        if c and os.access(c, os.X_OK):
            return c
    return None


def assemble(pioasm, src: Path, root: Path = ROOT) -> dict:
    """pioasm JSON for one .pio file, reduced to what a loader needs."""
    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / "out.json"
        r = subprocess.run([pioasm, "-o", "json", str(src), str(out)], capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(f"pioasm failed on {src.relative_to(root)}:\n{r.stdout}{r.stderr}")
        raw = json.loads(out.read_text())
    progs = []
    for p in raw["programs"]:
        progs.append({
            "name": p["name"],
            "instructions": [int(i["hex"], 16) for i in p["instructions"]],
            "wrap_target": p["wrapTarget"],
            "wrap": p["wrap"],
            "origin": p["origin"],
            "sideset": p["sideset"],  # size includes the opt bit (RP2040 SIDESET_COUNT)
            "labels": p.get("publicLabels", {}),
        })
    return {"source": str(src.relative_to(root)), "assembler": "pioasm (Pico SDK)", "programs": progs}


def image_path(src: Path) -> Path:
    return src.with_suffix(".json")


def sources():
    return sorted(PROGRAMS.rglob("*.pio"))


def load_program(name: str) -> dict:
    """Checked-in image of program `name` (searched in every image under rtl/pio/programs)."""
    for img in sorted(PROGRAMS.rglob("*.json")):
        for p in json.loads(img.read_text())["programs"]:
            if p["name"] == name:
                return p
    raise KeyError(f"PIO program '{name}' not in {PROGRAMS.relative_to(ROOT)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    if args.list:
        for src in sources():
            for p in json.loads(image_path(src).read_text())["programs"]:
                print(f"{p['name']:24s} {len(p['instructions']):2d} words  {src.relative_to(ROOT)}")
        return 0

    pioasm = find_pioasm()
    if not pioasm:
        print("pioasm not found ($PIOASM, ~/.tools/pioasm/pioasm, ~/.pico-sdk): not checked")
        return 2

    bad = 0
    for src in sources():
        img = assemble(pioasm, src)
        text = json.dumps(img, indent=2) + "\n"
        dst = image_path(src)
        if args.check:
            if not dst.exists() or dst.read_text() != text:
                print(f"[FAIL] {dst.relative_to(ROOT)} differs from pioasm output")
                bad += 1
        else:
            dst.write_text(text)
            for p in img["programs"]:
                if len(p["instructions"]) > 32:
                    print(f"[FAIL] {p['name']}: {len(p['instructions'])} words > 32-word instruction RAM")
                    bad += 1
    print(f"{len(sources())} source(s), {bad} problem(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
