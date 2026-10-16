#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
trac: run the testTrac / sizeTrac / perfTrac checks and record evidence.

  python3 tools/trac.py size --elf e906=build_e906/apps/gps_imu_app/gps_imu_app [--record]
  python3 tools/trac.py size --fpga tang_nano20k=build_hw/nextpnr.log [--record]
  python3 tools/trac.py perf --fpga tang_nano20k=build_hw/nextpnr.log [--record]
  python3 tools/trac.py report            # latest result and change per check
  python3 tools/trac.py export-benchmark  # github-action-benchmark JSON

Exit codes: 0 all pass, 1 a check failed, 2 nothing could run.
Model and record format: docs/SPECTRACE.md [SPEC-AUDIT-09], docs/TOOLING.md.
"""
# @impl [SPEC-AUDIT-09] docs/SPECTRACE.md

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from trac_lib import ledger  # noqa: E402
from trac_lib import size_perf as sp  # noqa: E402

TARGETS_FILE = ROOT / "tools" / "membrowse-targets.json"


def load_targets() -> dict:
    data = json.loads(TARGETS_FILE.read_text())
    return {t["short"]: t for t in data["targets"] if "short" in t}


def resolve_tool(name: str) -> str:
    """PATH first, then the standard toolchain layout ~/.tools/*/bin (AGENTS.md section 6)."""
    found = shutil.which(name)
    if found:
        return found
    hits = sorted(glob.glob(os.path.expanduser(f"~/.tools/*/bin/{name}")))
    if hits:
        return hits[-1]
    raise SystemExit(f"{name} not found on PATH or in ~/.tools/*/bin")


def pairs(values):
    out = []
    for v in values or []:
        k, _, p = v.partition("=")
        out.append((k, Path(p)))
    return out


def run_size(args, records) -> list[dict]:
    results = []
    targets = load_targets()
    for name, elf in pairs(args.elf):
        t = targets.get(name)
        if t is None:
            raise SystemExit(f"unknown target '{name}' (known: {', '.join(targets)})")
        if not elf.exists():
            results.append(ledger.make_record(ROOT, "size", f"elf:{name}", "skip", {},
                                              t.get("requirements", []), notes=f"{elf} not built"))
            continue
        start = time.monotonic()
        text = subprocess.run([resolve_tool(t["size_tool"]), "-A", str(elf)],
                              capture_output=True, text=True, check=True).stdout
        metrics = sp.size_metrics(sp.parse_size_A(text), t["memory_model"])
        prev = ledger.previous(records, f"elf:{name}")
        result, notes = sp.judge_size(metrics, t, prev["metrics"] if prev else None)
        rec = ledger.make_record(ROOT, "size", f"elf:{name}", result, metrics, t.get("requirements", []),
                                 time.monotonic() - start, t["size_tool"].split("/")[-1], notes)
        results.append(ledger.with_delta(rec, prev))
    for board, log in pairs(args.fpga):
        parsed = sp.parse_nextpnr(log.read_text())
        core = ("LUT4", "DFF", "ALU", "IOB", "BSRAM")
        kept = {k: v for k, v in parsed["util"].items()
                if k in core or (v["used"] > 0 and k not in ("VCC", "GND", "GSR"))}
        metrics = {f"{k.lower()}_pct": v["pct"] for k, v in kept.items()}
        metrics.update({f"{k.lower()}_used": v["used"] for k, v in kept.items()})
        ok = bool(parsed["util"]) and all(v["pct"] <= args.max_util for k, v in parsed["util"].items()
                                          if k in ("LUT4", "DFF", "ALU"))
        prev = ledger.previous(records, f"fpga-fit:{board}")
        rec = ledger.make_record(ROOT, "size", f"fpga-fit:{board}", "pass" if ok else "fail", metrics,
                                 [f"SPEC-TN20K-03"] if board.startswith("tang_nano20k") else [],
                                 tool="nextpnr", notes="" if ok else f"utilisation > {args.max_util}% or no report")
        results.append(ledger.with_delta(rec, prev))
    return results


def run_perf(args, records) -> list[dict]:
    results = []
    for board, log in pairs(args.fpga):
        parsed = sp.parse_nextpnr(log.read_text())
        for clk, f in parsed["fmax"].items():
            check = f"fmax:{board}:{clk}"
            prev = ledger.previous(records, check)
            rec = ledger.make_record(ROOT, "perf", check, "pass" if f["pass"] else "fail",
                                     {"fmax_mhz": f["fmax_mhz"], "target_mhz": f["target_mhz"]},
                                     ["SPEC-TLP-CRC-01"], tool="nextpnr")
            results.append(ledger.with_delta(rec, prev))
    return results


def print_results(results):
    for r in results:
        delta = " ".join(f"{k}{v:+g}" for k, v in r["delta"].items() if v) or "-"
        shown = {k: v for k, v in r["metrics"].items() if not k.endswith("_used")}
        print(f"{r['result'].upper():<5} {r['check']:<34} {json.dumps(shown)}  change: {delta}"
              + (f"  ({r['notes']})" if r["notes"] else ""))


def report(records):
    latest = {}
    for r in records:
        latest[r["check"]] = r
    for check in sorted(latest):
        print_results([latest[check]])
    print(f"\n{len(latest)} checks, {len(records)} records")


def export_benchmark(records, family):
    """github-action-benchmark 'customSmallerIsBetter' / 'customBiggerIsBetter' input."""
    latest = {}
    for r in records:
        if r["family"] == family:
            latest[r["check"]] = r
    out = []
    for check, r in sorted(latest.items()):
        for k, v in r["metrics"].items():
            if isinstance(v, (int, float)) and not k.startswith("target"):
                unit = "MHz" if k.endswith("mhz") else ("%" if k.endswith("pct") else "bytes")
                out.append({"name": f"{check} {k}", "unit": unit, "value": v})
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("family", choices=["size", "perf", "report", "export-benchmark"])
    ap.add_argument("--elf", action="append", help="target=path (target 'short' name from membrowse-targets.json)")
    ap.add_argument("--fpga", action="append", help="board=nextpnr.log")
    ap.add_argument("--max-util", type=int, default=80, help="sizeTrac FPGA LUT/DFF/ALU limit in percent")
    ap.add_argument("--record", action="store_true", help="append results to the evidence ledger")
    ap.add_argument("--of", default="size", choices=["size", "perf"], help="export-benchmark: which family")
    args = ap.parse_args()

    records = ledger.read_all(ROOT)
    if args.family == "report":
        report(records)
        return 0
    if args.family == "export-benchmark":
        print(json.dumps(export_benchmark(records, args.of), indent=1))
        return 0
    results = run_size(args, records) if args.family == "size" else run_perf(args, records)
    if not results:
        print("nothing to check (give --elf or --fpga)", file=sys.stderr)
        return 2
    print_results(results)
    if args.record:
        for r in results:
            path = ledger.append(ROOT, r)
        print(f"recorded {len(results)} record(s) in {path.relative_to(ROOT)}")
    return 1 if any(r["result"] == "fail" for r in results) else 0


if __name__ == "__main__":
    sys.exit(main())
