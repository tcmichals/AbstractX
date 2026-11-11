#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run every Part 4 row on the Cubie A5E in one command ([SPEC-E906-BENCH-08]).

  python3 tools/run_e906_bench_suite.py --buildroot-out ~/projects/cubie/bld.a5e [--ip IP] [--build]
          [--count 60000] [--runs 1] [--only SUBSTR] [--record] [--dry-run] [--force-lock]

Board from --ip or ABSTRACTX_BOARD_IP; toolchain from --buildroot-out or ABSTRACTX_BUILDROOT_OUT.
Takes the board lock first ([SPEC-AUDIT-13], tools/board_lock.py) and refuses to run if it is held.
Exit: 0 every row passed, 1 a row failed, 2 could not run (also: board in use).
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
import board_lock  # noqa: E402
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
MAX_TEMP_MC = 85000  # thermal stop: the A5E has no cpufreq and idles at 87 C without its fan
TEMPS = "cat /sys/class/thermal/thermal_zone*/temp"
SETUP = ("echo online=$(cat /sys/devices/system/cpu/online); echo isolated=$(cat /sys/devices/system/cpu/isolated); "
         "echo irq_default_affinity=$(cat /proc/irq/default_smp_affinity); "
         "echo governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo no-cpufreq); "
         "echo cmdline=$(cat /proc/cmdline)")


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


def hottest_mc(text: str):
    """Hottest thermal zone in millidegrees from `cat .../thermal_zone*/temp`, None if unreadable."""
    vals = [int(x) for x in text.split() if x.lstrip("-").isdigit()]
    return max(vals) if vals else None


def read_temp(target: str, run=subprocess.run):
    try:
        return hottest_mc(run(["ssh", target, TEMPS], capture_output=True, text=True, timeout=30).stdout or "")
    except subprocess.TimeoutExpired:
        return None


def parse_setup(text: str) -> dict:
    return dict(line.split("=", 1) for line in text.splitlines() if "=" in line)


def push(target: str, src: Path, dst: str, run=subprocess.run) -> bool:
    """[SPEC-E906-BENCH-08] busybox has no scp/sftp: copy over ssh and compare MD5 on the board."""
    with open(src, "rb") as f:
        if run(["ssh", target, f"cat > {dst}"], stdin=f, timeout=120).returncode != 0:
            return False
    got = run(["ssh", target, f"md5sum {dst}"], capture_output=True, text=True, timeout=30).stdout or ""
    return got.split()[:1] == [hashlib.md5(src.read_bytes()).hexdigest()]


def fetch(target: str, src: str, dst: Path, run=subprocess.run) -> bool:
    with open(dst, "wb") as f:
        ok = run(["ssh", target, f"cat {src}"], stdout=f, timeout=60).returncode == 0
    if not ok:
        dst.unlink(missing_ok=True)
    return ok


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_rows(rows, firmwares, ip: str, out_dir: Path, count: int, run=subprocess.run,
             max_temp_mc: int = MAX_TEMP_MC) -> list:
    """Runs and fetches every row. A failure is recorded and the next row still runs; too hot stops the run."""
    target = f"root@{ip}"
    by_name = {fw.name: fw for fw in firmwares}
    results, current, hot = [], None, None
    for i, row in enumerate(rows, 1):
        res = {"row": row.name, "firmware": row.firmware, "load": row.load, "command": row.command}
        if hot is None:
            temp = read_temp(target, run)
            res["temp_mC_before"] = temp
            if temp is None or temp > max_temp_mc:
                hot = f"thermal: hottest zone {temp} mC before {row.name} (limit {max_temp_mc})"
        if hot is not None:  # [SPEC-E906-BENCH-08] never run hot; the rows not run stay in the record
            results.append({**res, "rc": 2, "error": hot})
            print(f"[{i}/{len(rows)}] {row.name} NOT RUN, {hot}", flush=True)
            continue
        print(f"[{i}/{len(rows)}] {row.name} (~{row.seconds} s, {temp / 1000:.1f} C)", flush=True)
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
        elif not fetch(target, f"{REMOTE_RESULTS}/{row.name}.json", out, run):
            res.update(rc=res["rc"] or 1, error="no JSON from the board")
        after = read_temp(target, run)
        res["temp_mC_after"] = after
        if after is None or after > max_temp_mc:
            hot = f"thermal: hottest zone {after} mC after {row.name} (limit {max_temp_mc})"
            res["thermal_stop"] = hot
        print(f"   {'ok' if res['rc'] == 0 else 'FAILED, rc ' + str(res['rc'])}"
              f"{'' if after is None else f', {after / 1000:.1f} C'}", flush=True)
        results.append(res)
    return results


def make_manifest(stamp: str, kernel: str, setup: dict, count: int, elf_sha256: dict, results: list,
                  lock: dict) -> dict:
    return {"check": "board:e906:part4", "time": stamp, "commit": git("rev-parse", "--short=12", "HEAD"),
            "dirty": bool(git("status", "--porcelain")), "kernel": kernel, "linux_setup": setup, "count": count,
            "stress": STRESS, "elf_sha256": elf_sha256, "rows": results,
            "board_lock": board_lock.manifest_entry(lock)}  # [SPEC-AUDIT-13] who held the board


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
    ap.add_argument("--max-temp-mc", type=int, default=MAX_TEMP_MC, help="stop the run above this (millidegrees C)")
    board_lock.add_arguments(ap)
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
    # [SPEC-AUDIT-13] board lock: before anything touches the board, released on every exit
    try:
        with board_lock.hold(ip, "run_e906_bench_suite", a.force_lock, a.lock_stale_min) as lock:
            print(f"board lock taken: {lock['path']} on {ip}", flush=True)
            return run_locked(a, ip, rows, firmwares, total, lock)
    except board_lock.LockHeld as exc:
        print(f"could not run: {exc}")
        return 2


# @impl [SPEC-AUDIT-13] docs/SPECTRACE.md (board lock: the run happens only while the lock is held)
def run_locked(a, ip: str, rows: list, firmwares: list, total: int, lock: dict) -> int:
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
    sends = [(ROOT / f, f"{REMOTE_DIR}/{Path(f).name}") for f in TOOLS] + [(READER_BIN, f"{REMOTE_DIR}/{READER_BIN.name}")]
    sends += [(fw.elf, f"/lib/firmware/{fw.name}.elf") for fw in firmwares]
    for src, dst in sends:
        if not push(target, src, dst):
            print(f"could not run: {src} did not reach the board intact ({dst})")
            return 2
    subprocess.run(["ssh", target, f"chmod +x {REMOTE_DIR}/{READER_BIN.name}"], check=True, timeout=30)
    setup = parse_setup(subprocess.run(["ssh", target, SETUP], capture_output=True, text=True, timeout=30).stdout)

    out_dir = ROOT / "docs/verification/evidence" / stamp[:7] / f"part4_bench_{stamp[:19].replace(':', '')}"
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"{len(rows)} rows, about {total // 60} min; evidence: {out_dir.relative_to(ROOT)}", flush=True)
    results = run_rows(rows, firmwares, ip, out_dir, a.count, max_temp_mc=a.max_temp_mc)
    kernel = subprocess.run(["ssh", target, "cat /proc/version"], capture_output=True, text=True, timeout=30).stdout.strip()
    manifest = make_manifest(stamp, kernel, setup, a.count, {fw.name: sha256(fw.elf) for fw in firmwares}, results, lock)
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    failed = [r["row"] for r in results if r["rc"] != 0]
    print(f"{len(results) - len(failed)}/{len(results)} rows passed" + (f"; failed: {', '.join(failed)}" if failed else ""))
    if a.record:
        rec = make_record(manifest, stamp)
        print(f"recorded in {ledger.append(ROOT, rec).relative_to(ROOT)}: {rec['result']}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
