#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the E906 board suite on the Cubie A5E in one command ([SPEC-E906-REPL-15] one command).

  python3 tools/run_e906_board_suite.py [--ip IP] [-k SUBSTR] [--record] [--timeout S] [--dry-run]

Board from --ip or ABSTRACTX_BOARD_IP. Checks the board is the A5E, copies the suite, runs it, fetches
the JSON into docs/verification/evidence/<YYYY-MM>/, and with --record appends a `source: board`
record to the evidence ledger. Exit: the suite's (0 pass, 1 a test failed, 2 could not run).
"""
import argparse
import datetime as dt
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

REMOTE_DIR = "/root/abstractx_board_tests"
REMOTE_JSON = "/tmp/e906_selftest.json"
EXPECTED_DT_VERSION = "1.6-e906"
EXPECTED_FIRMWARE = "e906_repl_app.elf"
# The files the board suite imports (the e906-board-selftest skill's list)
SUITE_FILES = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "tests/board").glob("*.py")) + [
    "tools/abstractx_wire.py", "tools/e906_rpmsg_bridge.py", "tools/visualizer/runtime_stats.py",
    "tools/visualizer/event_streams.py", "apps/e906_repl_app/raw_sensor_streamer.py",
    "apps/e906_repl_app/run_e906_script.py", "apps/e906_repl_app/sensor_contract.py",
    "tools/board_studio_bridge.py",
]


# @impl [SPEC-E906-REPL-15] apps/e906_repl_app/SPECIFICATION.md (one command: copy and run)
def commands(ip: str, selector: str, timeout_s: int) -> list[list[str]]:
    target = f"root@{ip}"
    test_args = ["python3", "run_board_tests.py", "--json", REMOTE_JSON]
    if selector:
        test_args += ["-k", selector]
    python_runner = f"import subprocess,sys; sys.exit(subprocess.run({test_args!r}, timeout={timeout_s}).returncode)"
    run = f"cd {REMOTE_DIR} && python3 -c {shlex.quote(python_runner)}"
    return [["ssh", target, f"mkdir -p {REMOTE_DIR}"],
            ["scp", "-O", *SUITE_FILES, f"{target}:{REMOTE_DIR}/"],
            ["ssh", target, run]]


# @impl [SPEC-E906-REPL-15] apps/e906_repl_app/SPECIFICATION.md (test-target preflight)
def validate_test_setup(output: str) -> list[str]:
    values = {}
    for line in output.splitlines():
        key, separator, value = line.partition("=")
        if separator:
            values[key] = value.strip()
    errors = []
    if values.get("tree") != EXPECTED_DT_VERSION:
        errors.append(f"device tree is {values.get('tree') or 'unreadable'}, expected {EXPECTED_DT_VERSION}")
    if values.get("state") != "running":
        errors.append(f"remoteproc0 state is {values.get('state') or 'unreadable'}, expected running")
    firmware = os.path.basename(values.get("resolved") or values.get("firmware") or "")
    if firmware != EXPECTED_FIRMWARE:
        errors.append(f"remoteproc0 firmware resolves to {firmware or 'unreadable'}, expected {EXPECTED_FIRMWARE}")
    return errors


def require_e906_test_setup(ip: str) -> None:
    probe = (
        "set -eu; "
        "tree=$(tr -d '\\000' </proc/device-tree/cubie-flight-stack-version); "
        "state=$(cat /sys/class/remoteproc/remoteproc0/state); "
        "firmware=$(cat /sys/class/remoteproc/remoteproc0/firmware); "
        "resolved=$(readlink -f \"/lib/firmware/$firmware\" || true); "
        "printf 'tree=%s\\nstate=%s\\nfirmware=%s\\nresolved=%s\\n' "
        "\"$tree\" \"$state\" \"$firmware\" \"$resolved\""
    )
    result = subprocess.run(
        ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", f"root@{ip}", probe],
        capture_output=True, text=True, timeout=15)
    if result.returncode != 0:
        reason = result.stderr.strip() or f"remote preflight exited {result.returncode}"
        print(f"could not run: cannot verify E906 test setup: {reason}", file=sys.stderr)
        raise SystemExit(2)
    errors = validate_test_setup(result.stdout)
    if errors:
        print("could not run: E906 board setup mismatch: " + "; ".join(errors), file=sys.stderr)
        print(f"load dtoverlay=cubie-a5e-flight-stack-e906 and {EXPECTED_FIRMWARE}, then reboot", file=sys.stderr)
        raise SystemExit(2)


# @impl [SPEC-E906-REPL-15] apps/e906_repl_app/SPECIFICATION.md (one command: ledger record)
def make_record(result: dict, commit: str, dirty: bool, rc: int, now: str) -> dict:
    """The ledger record for one run (same shape as the 2026-10 cubie-a5e records)."""
    counts = result.get("counts", {})
    return {"check": "e906-board-selftest", "commit": commit, "dirty": dirty, "family": "board",
            "host": "cubie-a5e", "source": "board", "time": now,
            "result": "pass" if rc == 0 and counts.get("FAIL", 0) == 0 else "fail",
            "metrics": {"passed": counts.get("PASS", 0), "failed": counts.get("FAIL", 0),
                        "skipped": counts.get("SKIP", 0), "firmware": result.get("firmware", ""),
                        "uname": result.get("uname", "")},
            "delta": {}}


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ip", default=None)
    ap.add_argument("-k", dest="selector", default="")
    ap.add_argument("--record", action="store_true", help="append a board record to the evidence ledger")
    ap.add_argument("--timeout", type=int, default=600, help="overall limit for the suite on the board (s)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv)
    ip = a.ip or board_target.board_ip(default=None)
    if not ip:
        print("could not run: no board (--ip or ABSTRACTX_BOARD_IP)")
        return 2
    cmds = commands(ip, a.selector, a.timeout)
    if a.dry_run:
        for c in cmds:
            print(" ".join(shlex.quote(x) for x in c))
        return 0
    board_target.require_a5e(ip)                       # [SPEC-AUDIT-13] right board
    require_e906_test_setup(ip)
    for c in cmds[:2]:
        subprocess.run(c, check=True, timeout=120)
    rc = subprocess.run(cmds[2], timeout=a.timeout + 60).returncode
    now = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    out = ROOT / "docs/verification/evidence" / now[:7] / f"e906_selftest_{now[:19].replace(':', '')}.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    if subprocess.run(["scp", "-O", f"root@{ip}:{REMOTE_JSON}", str(out)], timeout=120).returncode != 0:
        print("no JSON from the board (the suite could not run)")
        return rc or 2
    result = json.loads(out.read_text())
    print(f"evidence: {out.relative_to(ROOT)}")
    if a.record:
        rec = make_record(result, git("rev-parse", "--short=12", "HEAD"), bool(git("status", "--porcelain")), rc, now)
        print(f"recorded in {ledger.append(ROOT, rec).relative_to(ROOT)}: {rec['result']} {rec['metrics']}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
