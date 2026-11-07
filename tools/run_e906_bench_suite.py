#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run every Part 4 row on the Cubie A5E in one command ([SPEC-E906-BENCH-08]).

  python3 tools/run_e906_bench_suite.py --buildroot-out ~/projects/cubie/bld.a5e [--ip IP] [--build]
          [--count 60000] [--runs 1] [--only SUBSTR] [--record] [--dry-run]

Board from --ip or ABSTRACTX_BOARD_IP; toolchain from --buildroot-out or ABSTRACTX_BUILDROOT_OUT.
Exit: 0 every row passed, 1 a row failed, 2 could not run.
"""
# @impl [SPEC-E906-BENCH-08] apps/e906_bench/SPECIFICATION.md
import argparse
import dataclasses
import datetime as dt
import hashlib
import json
import os
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import board_target  # noqa: E402
from trac_lib import ledger  # noqa: E402

REMOTE_DIR = "/root/abstractx_board_bench"
REMOTE_RESULTS = "/tmp/part4"
RPROC = "/sys/class/remoteproc/remoteproc0"
BENCH_TOOL = "apps/e906_repl_app/tests/board/bench_e906_runtime.py"
READER_SRC = "apps/e906_repl_app/tests/board/e906_oneway_reader.c"
TOOLS = [BENCH_TOOL, "tools/e906_rpmsg_bridge.py", "tools/abstractx_wire.py"]
READER_BIN = ROOT / "build_e906" / "e906_oneway_reader"
STRESS = "stress-ng --cpu 8 --vm 2 --vm-bytes 256M --sock 2"
HIST_US = 20000
ECHO_S = 15  # the echo and 'R' suite, roughly


@dataclasses.dataclass(frozen=True)
class Firmware:
    name: str     # names the rows and the file in /lib/firmware
    app: str
    mode: str     # FreeRTOS timing build with this timer mode, "" for the default build
    rows: tuple
    step_profile: bool = False  # [SPEC-E906-BENCH-09] never a timing row

    @property
    def build_dir(self) -> Path:
        name = f"build_e906_timer_{self.mode.lower()}" if self.mode else "build_e906"
        return ROOT / (f"{name}_step_profile" if self.step_profile else name)

    @property
    def cmake_args(self) -> list:
        # [SPEC-E906-BENCH-06] timing rows on FreeRTOS carry no debug cost
        args = [f"-DABX_FREERTOS_TIMER={self.mode}", "-DABX_FREERTOS_DEBUG=OFF"] if self.mode else []
        return args + (["-DABX_STEP_PROFILE=ON"] if self.step_profile else [])

    @property
    def elf(self) -> Path:
        return self.build_dir / "apps" / self.app / self.app


ONEWAY = ("oneway_c_rt", "oneway_c_other", "oneway_py_rt", "oneway_py_other")
FIRMWARES = (
    Firmware("abstractx", "e906_bench_app", "", ("bench", "cyclic") + ONEWAY),
    Firmware("freertos_wait", "e906_freertos_app", "WAIT", ("bench", "cyclic", "native_cyclic") + ONEWAY),
    Firmware("freertos_xtimer", "e906_freertos_app", "XTIMER", ("cyclic",)),
    Firmware("freertos_tick_hook", "e906_freertos_app", "TICK_HOOK", ("cyclic",)),
    Firmware("freertos_debug", "e906_freertos_app", "", ("bench", "cyclic")),  # the default build: idle numbers, debug cost
    Firmware("abstractx_step_profile", "e906_bench_app", "", ("bench",), step_profile=True),
    Firmware("freertos_step_profile", "e906_freertos_app", "WAIT", ("bench",), step_profile=True),
)


@dataclasses.dataclass(frozen=True)
class Row:
    name: str
    firmware: str   # "" for the Linux rows: nothing to load
    load: bool
    command: str    # what runs on the board, the load wrapper included
    seconds: int
    stdout: bool    # cyclictest: the result is its output, not a JSON file


def row_command(kind: str, count: int, remote_json: str) -> tuple:
    """(command, seconds, result is stdout) for one kind of row."""
    tool = f"python3 {REMOTE_DIR}/bench_e906_runtime.py"
    run_s = count // 1000
    if kind == "bench":
        return f"{tool} --json {remote_json}", ECHO_S, False
    if kind == "cyclic":
        return f"{tool} --cyclic {count} --json {remote_json}", run_s, False
    if kind == "native_cyclic":
        return f"{tool} --native-cyclic {count} --json {remote_json}", run_s, False
    if kind.startswith("oneway_"):
        _, reader, sched = kind.split("_")
        args = f"--oneway {count} --reader {'python' if reader == 'py' else 'c'}" + (" --rt" if sched == "rt" else "")
        return f"{tool} {args} --json {remote_json}", run_s, False
    if kind in ("cyclictest_rt", "cyclictest_other"):
        prio = "-p 95 " if kind == "cyclictest_rt" else ""
        return f"cyclictest -m {prio}-i 1000 -l {count} -t1 -a 2 -q -h {HIST_US}", run_s, True
    raise ValueError(kind)


def with_load(command: str, seconds: int) -> str:
    """Article §7: the load runs for the whole row and is gone before the next one."""
    return (f"{STRESS} --timeout {seconds + 60}s -q & S=$!; sleep 5; {command}; rc=$?; "
            f"kill $S 2>/dev/null; wait $S 2>/dev/null; exit $rc")


def plan(count: int, runs: int = 1, only: str = "", firmwares=FIRMWARES) -> list:
    rows = []
    for run in range(1, runs + 1):
        suffix = f"_run{run}" if runs > 1 else ""
        for load in (False, True):
            tag = "load" if load else "idle"
            work = [(fw.name, kind) for fw in firmwares for kind in fw.rows]
            work += [("", "cyclictest_rt"), ("", "cyclictest_other")]
            for fw_name, kind in work:
                name = f"{fw_name or 'linux'}_{kind}_{tag}{suffix}"
                if only and only not in name:
                    continue
                cmd, secs, stdout = row_command(kind, count, f"{REMOTE_RESULTS}/{name}.json")
                rows.append(Row(name, fw_name, load, with_load(cmd, secs) if load else cmd, secs, stdout))
    return rows


def load_firmware_command(fw: Firmware) -> str:
    return (f"R={RPROC}; [ \"$(cat $R/state)\" = running ] && echo stop > $R/state; "
            f"echo {fw.name}.elf > $R/firmware && echo start > $R/state && sleep 2 && "
            f"[ \"$(cat $R/state)\" = running ]")


def build_commands(firmwares=FIRMWARES) -> list:
    out = []
    for fw in firmwares:
        out.append(["cmake", "--preset", "e906", "-B", str(fw.build_dir), *fw.cmake_args])
        out.append(["cmake", "--build", str(fw.build_dir), "--target", fw.app, "-j"])
    return out


def reader_compile_command(buildroot_out: str) -> list:
    return [str(Path(buildroot_out).expanduser() / "host/bin/aarch64-linux-gcc"), "-std=c11", "-O2", "-Wall",
            "-Wextra", "-Werror", "-o", str(READER_BIN), str(ROOT / READER_SRC)]


def parse_cyclictest(text: str, loops: int) -> dict:
    """`cyclictest -q -h N` output; percentiles as upper bucket edges, same as the one-way reader."""
    buckets, summary = {}, {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0].isdigit() and parts[1].isdigit():
            buckets[int(parts[0])] = int(parts[1])
        elif line.startswith("# ") and ":" in line:
            key, value = line[2:].split(":", 1)
            summary[key.strip()] = value.strip()
    overflows = int(summary.get("Histogram Overflows", "0") or 0)
    n = sum(buckets.values()) + overflows

    def pct(per_mille):
        need, acc = (n * per_mille + 999) // 1000, 0
        for us in sorted(buckets):
            acc += buckets[us]
            if acc >= need:
                return us + 1
        return HIST_US
    out = {"n": n, "overflows": overflows}
    try:
        out.update(min_us=int(summary["Min Latencies"]), avg_us=int(summary["Avg Latencies"]),
                   max_us=int(summary["Max Latencies"]), p50_us=pct(500), p99_us=pct(990), p999_us=pct(999))
    except (KeyError, ValueError):
        out["error"] = "no summary in the cyclictest output"
    if n != loops:
        out.setdefault("error", f"{n} samples, expected {loops}")
    return out


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_rows(rows, firmwares, ip: str, out_dir: Path, count: int, run=subprocess.run) -> list:
    """Runs and fetches every row. A failure is recorded and the next row still runs."""
    target = f"root@{ip}"
    by_name = {fw.name: fw for fw in firmwares}
    results, current = [], None
    for i, row in enumerate(rows, 1):
        print(f"[{i}/{len(rows)}] {row.name} (~{row.seconds} s)", flush=True)
        res = {"row": row.name, "firmware": row.firmware, "load": row.load, "command": row.command}
        try:
            if row.firmware and row.firmware != current:
                loaded = run(["ssh", target, load_firmware_command(by_name[row.firmware])], timeout=60)
                current = row.firmware if loaded.returncode == 0 else None
                if loaded.returncode != 0:
                    results.append({**res, "rc": 2, "error": "firmware did not start"})
                    print("   firmware did not start")
                    continue
            proc = run(["ssh", target, row.command], capture_output=True, text=True, timeout=row.seconds + 180)
        except subprocess.TimeoutExpired:  # a hung row must not lose the rows before it
            current = None                 # the firmware may be in any state: load it again
            results.append({**res, "rc": 1, "error": "timed out"})
            print("   FAILED, timed out", flush=True)
            continue
        res["rc"] = proc.returncode
        out = out_dir / f"{row.name}.json"
        if row.stdout:
            parsed = parse_cyclictest(proc.stdout, count)
            out.write_text(json.dumps({"check": "board:a55:cyclictest", "command": row.command, **parsed}, indent=1) + "\n")
            if "error" in parsed:
                res.update(rc=res["rc"] or 1, error=parsed["error"])
        elif run(["scp", "-O", f"{target}:{REMOTE_RESULTS}/{row.name}.json", str(out)], timeout=60).returncode != 0:
            res.update(rc=res["rc"] or 1, error="no JSON from the board")
        print(f"   {'ok' if res['rc'] == 0 else 'FAILED, rc ' + str(res['rc'])}", flush=True)
        results.append(res)
    return results


def make_record(manifest: dict, now: str) -> dict:
    failed = sum(1 for r in manifest["rows"] if r["rc"] != 0)
    return {"check": "e906-bench-part4", "commit": manifest["commit"], "dirty": manifest["dirty"], "family": "board",
            "host": "cubie-a5e", "source": "board", "time": now, "result": "fail" if failed else "pass",
            "metrics": {"rows": len(manifest["rows"]), "failed": failed, "uname": manifest["kernel"]}, "delta": {}}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ip", default=None)
    ap.add_argument("--buildroot-out", default=os.environ.get("ABSTRACTX_BUILDROOT_OUT"))
    ap.add_argument("--build", action="store_true", help="configure and build every firmware first")
    ap.add_argument("--count", type=int, default=60000)
    ap.add_argument("--runs", type=int, default=1)
    ap.add_argument("--only", default="", help="only rows whose name contains this")
    ap.add_argument("--record", action="store_true", help="append a board record to the evidence ledger")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv)
    ip = a.ip or board_target.board_ip(default=None)
    if not ip:
        print("could not run: no board (--ip or ABSTRACTX_BOARD_IP)")
        return 2
    rows = plan(a.count, a.runs, a.only)
    if not rows:
        print(f"could not run: no row matches '{a.only}'")
        return 2
    needed = {r.firmware for r in rows if r.firmware}
    firmwares = [fw for fw in FIRMWARES if fw.name in needed]
    total = sum(r.seconds + (5 if r.load else 0) for r in rows)
    if a.dry_run:
        print(f"# {len(rows)} rows, about {total // 60} min")
        for r in rows:
            print(f"{r.firmware or '-'}\t{r.name}\t{r.command}")
        return 0
    if not a.buildroot_out:
        print("could not run: no toolchain for the reader (--buildroot-out or ABSTRACTX_BUILDROOT_OUT)")
        return 2
    target = f"root@{ip}"
    board_target.require_a5e(ip)                       # [SPEC-AUDIT-13] right board
    if subprocess.run(["ssh", target, f"test -e {RPROC}/state"], timeout=30).returncode != 0:
        print(f"could not run: {RPROC} missing on the board: check `dmesg | grep rproc`, kernel and DTB must match")
        return 2
    if a.build:
        for cmd in build_commands(firmwares):
            subprocess.run(cmd, cwd=ROOT, check=True)
    missing = [str(fw.elf) for fw in firmwares if not fw.elf.is_file()]
    if missing:
        print("could not run: not built (use --build): " + ", ".join(missing))
        return 2
    READER_BIN.parent.mkdir(parents=True, exist_ok=True)
    if subprocess.run(reader_compile_command(a.buildroot_out)).returncode != 0:
        print("could not run: the one-way reader did not compile")
        return 2

    now = dt.datetime.now(dt.timezone.utc)
    stamp = now.strftime("%Y-%m-%dT%H:%M:%SZ")
    subprocess.run(["ssh", target, f"date -u -s '{now:%Y-%m-%d %H:%M:%S}' >/dev/null; "
                    f"mkdir -p {REMOTE_DIR}; rm -rf {REMOTE_RESULTS}; mkdir -p {REMOTE_RESULTS}"], check=True, timeout=30)
    subprocess.run(["scp", "-O", *[str(ROOT / f) for f in TOOLS], str(READER_BIN), f"{target}:{REMOTE_DIR}/"],
                   check=True, timeout=120)
    for fw in firmwares:
        subprocess.run(["scp", "-O", str(fw.elf), f"{target}:/lib/firmware/{fw.name}.elf"], check=True, timeout=120)

    out_dir = ROOT / "docs/verification/evidence" / stamp[:7] / f"part4_bench_{stamp[:19].replace(':', '')}"
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"{len(rows)} rows, about {total // 60} min; evidence: {out_dir.relative_to(ROOT)}", flush=True)
    results = run_rows(rows, firmwares, ip, out_dir, a.count)
    kernel = subprocess.run(["ssh", target, "cat /proc/version"], capture_output=True, text=True, timeout=30).stdout.strip()
    manifest = {"check": "board:e906:part4", "time": stamp, "commit": git("rev-parse", "--short=12", "HEAD"),
                "dirty": bool(git("status", "--porcelain")), "kernel": kernel, "count": a.count, "stress": STRESS,
                "elf_sha256": {fw.name: sha256(fw.elf) for fw in firmwares}, "rows": results}
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    failed = [r["row"] for r in results if r["rc"] != 0]
    print(f"{len(results) - len(failed)}/{len(results)} rows passed" + (f"; failed: {', '.join(failed)}" if failed else ""))
    if a.record:
        rec = make_record(manifest, stamp)
        print(f"recorded in {ledger.append(ROOT, rec).relative_to(ROOT)}: {rec['result']}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
