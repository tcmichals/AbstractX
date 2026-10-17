#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-AUDIT-09] docs/SPECTRACE.md
"""
trac: run the testTrac / sizeTrac / perfTrac checks and record evidence.

  python3 tools/trac.py size --elf e906=build_e906/apps/gps_imu_app/gps_imu_app [--record]
  python3 tools/trac.py size --elf e906/debug=build_e906_debug/... --elf e906/lto=build_e906_lto/...
  python3 tools/trac.py size --fpga tang_nano20k=build_hw/nextpnr.log [--record]
  python3 tools/trac.py perf --fpga tang_nano20k=build_hw/nextpnr.log [--record]
  python3 tools/trac.py report            # latest result and change per check
  python3 tools/trac.py export-benchmark  # github-action-benchmark JSON

Exit codes: 0 all pass, 1 a check failed, 2 nothing could run.
Model and record format: docs/SPECTRACE.md [SPEC-AUDIT-09], docs/TOOLING.md.
"""

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
from trac_lib import test_checks as tc  # noqa: E402

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
        # "e906/debug" = target e906, build variant debug; each variant is its own
        # trend line (elf:e906/debug). A bare name is the Release build.
        t = targets.get(name.split("/", 1)[0])
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


def run_test(args, records) -> list[dict]:
    """testTrac: correctness checks; each becomes one record with counts."""
    results = []

    def add(check, ok, metrics, reqs, duration, tool, notes=""):
        prev = ledger.previous(records, check)
        rec = ledger.make_record(ROOT, "test", check, "pass" if ok else "fail", metrics, reqs,
                                 duration, tool, notes)
        results.append(ledger.with_delta(rec, prev))

    for label, build in pairs(args.ctest):
        code, out, dur = tc.run(["ctest", "--test-dir", str(build), "-j8", "--timeout", "300"], ROOT)
        m = tc.parse_ctest(out)
        add(f"ctest:{label}", code == 0 and bool(m) and m["failed"] == 0, m, [], dur, "ctest",
            "" if m else "no ctest summary")
    if args.cocotb:
        code, out, dur = tc.run(["python3", "tools/run_cocotb.py"], ROOT, tc.oss_env())
        m = tc.parse_cocotb(out)
        add("cocotb:all", code == 0 and bool(m) and m["targets"] == m["targets_passed"], m, [], dur,
            "cocotb", "" if m else "no summary (oss-cad-suite missing?)")
    if args.rtl:
        m, issues, dur = tc.run_rtl_lint(ROOT)
        add("rtl:lint", not issues, m, [], dur, "verilator", "; ".join(issues[:5]))
        m, bad, dur = tc.run_rtl_synth(ROOT)
        add("rtl:synth-check", not bad, m, ["SPEC-ZYNQ-01"], dur, "yosys", "; ".join(bad[:5]))
    if args.matrix:
        code, out, dur = tc.run(["python3", "tools/spectrace_matrix.py", "--matrix"], ROOT)
        m = tc.parse_matrix(out)
        add("spectrace:matrix", bool(m) and m.get("broken", 0) == 0, m, ["SPEC-AUDIT-07"], dur,
            "spectrace_matrix", "")
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


def sparkline(values: list[float], length: int = 10) -> str:
    if not values:
        return ""
    vals = values[-length:]
    mn, mx = min(vals), max(vals)
    if mn == mx:
        return "▅" * len(vals)
    bars = " ▂▃▄▅▆▇█"
    return "".join(bars[min(7, max(0, int((v - mn) / (mx - mn) * 7)))] for v in vals)


def progress_bar(val: float, max_val: float, width: int = 16) -> str:
    if max_val <= 0:
        return "░" * width
    pct = min(1.0, max(0.0, val / max_val))
    filled = int(round(pct * width))
    return "█" * filled + "░" * (width - filled)


def report(records):
    latest = {}
    history: dict[str, list[dict]] = {}
    for r in records:
        latest[r["check"]] = r
        history.setdefault(r["check"], []).append(r)

    print("=" * 78)
    print("AbstractX SpecTrace Evidence & Quality Report")
    print("=" * 78)
    print(f"Total Ledger Records: {len(records)} across {len(latest)} checks\n")

    print(f"{'STATUS':<6} {'CHECK':<30} {'LATEST METRICS':<28} {'DELTA':<10} {'TREND'}")
    print("-" * 78)
    for check in sorted(latest):
        r = latest[check]
        delta_str = " ".join(f"{k}{v:+g}" for k, v in r["delta"].items() if v) or "-"
        shown = {k: v for k, v in r["metrics"].items() if not k.endswith("_used")}
        shown_str = json.dumps(shown)
        if len(shown_str) > 27:
            shown_str = shown_str[:24] + "..."

        # Compute sparkline for primary numerical metric
        spark = ""
        past = history.get(check, [])
        for primary in ("ram_bytes", "flash_bytes", "passed", "lut4_pct", "fmax_mhz", "verified"):
            vals = [p["metrics"][primary] for p in past if primary in p.get("metrics", {})]
            if len(vals) >= 2:
                spark = f"[{sparkline(vals)}]"
                break

        res_str = r['result'].upper()
        print(f"{res_str:<6} {check:<30} {shown_str:<28} {delta_str:<10} {spark}")

    # Target Sizing & Memory Limits
    targets = load_targets()
    print("\n" + "=" * 78)
    print("Silicon Memory Utilization & Budgets")
    print("=" * 78)
    for check in sorted(latest):
        if not check.startswith("elf:"):
            continue
        r = latest[check]
        variant = check[4:]
        tgt_name = variant.split("/")[0]
        t = targets.get(tgt_name)
        if not t:
            continue
        ram_budget = t.get("ram_budget_bytes", 0)
        flash_budget = t.get("flash_budget_bytes", 0)
        ram = r["metrics"].get("ram_bytes", 0)
        flash = r["metrics"].get("flash_bytes", 0)
        if ram_budget and ram:
            pct = 100.0 * ram / ram_budget
            pbar = progress_bar(ram, ram_budget, 16)
            past_ram = [p["metrics"]["ram_bytes"] for p in history.get(check, []) if "ram_bytes" in p.get("metrics", {})]
            spark = f" trend: [{sparkline(past_ram)}]" if len(past_ram) >= 2 else ""
            print(f"  {check:<20} RAM  : {ram:>7,d} / {ram_budget:>7,d} B [{pbar}] {pct:>5.1f}%{spark}")
        if flash_budget and flash:
            pct = 100.0 * flash / flash_budget
            pbar = progress_bar(flash, flash_budget, 16)
            past_fl = [p["metrics"]["flash_bytes"] for p in history.get(check, []) if "flash_bytes" in p.get("metrics", {})]
            spark = f" trend: [{sparkline(past_fl)}]" if len(past_fl) >= 2 else ""
            print(f"  {check:<20} Flash: {flash:>7,d} / {flash_budget:>7,d} B [{pbar}] {pct:>5.1f}%{spark}")

    # Engineering Log Lessons Breakdown
    eng_log = ROOT / "engineering_log.md"
    if eng_log.exists():
        categories = ["product-bug", "tool-bug", "false-alarm", "process-gap", "hardware-fact"]
        cat_counts = {c: 0 for c in categories}
        text = eng_log.read_text(encoding="utf-8", errors="ignore")
        for line in text.splitlines():
            for c in categories:
                if f"`{c}`" in line or f"**Category**: {c}" in line or f"category: `{c}`" in line:
                    cat_counts[c] += 1
        total_lessons = sum(cat_counts.values())
        print("\n" + "=" * 78)
        print(f"Engineering Log Lessons & Feedback Loop ({total_lessons} recorded)")
        print("=" * 78)
        for c in categories:
            bar_c = progress_bar(cat_counts[c], max(total_lessons, 1), 16)
            print(f"  {c:<15}: {cat_counts[c]:>3} [{bar_c}]")
    print()


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
    ap.add_argument("family", choices=["test", "size", "perf", "report", "export-benchmark"])
    ap.add_argument("--ctest", action="append", help="test: label=build_dir (e.g. debug=build_host)")
    ap.add_argument("--cocotb", action="store_true", help="test: run every registered cocotb target")
    ap.add_argument("--rtl", action="store_true", help="test: Verilator lint + Yosys driver check on rtl/")
    ap.add_argument("--matrix", action="store_true", help="test: SpecTrace matrix verdict counts")
    ap.add_argument("--elf", action="append", help="target[/variant]=path (target 'short' name from membrowse-targets.json; variant e.g. debug, lto)")
    ap.add_argument("--fpga", action="append", help="board=nextpnr.log")
    ap.add_argument("--max-util", type=int, default=80, help="sizeTrac FPGA LUT/DFF/ALU limit in percent")
    ap.add_argument("--record", action="store_true", help="append results to the evidence ledger")
    ap.add_argument("--of", default="size", choices=["test", "size", "perf"], help="export-benchmark: which family")
    args = ap.parse_args()

    records = ledger.read_all(ROOT)
    if args.family == "report":
        report(records)
        return 0
    if args.family == "export-benchmark":
        print(json.dumps(export_benchmark(records, args.of), indent=1))
        return 0
    runners = {"test": run_test, "size": run_size, "perf": run_perf}
    results = runners[args.family](args, records)
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
