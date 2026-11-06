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
    run = f"cd {REMOTE_DIR} && timeout {timeout_s} python3 run_board_tests.py --json {REMOTE_JSON}"
    if selector:
        run += f" -k {shlex.quote(selector)}"
    return [["ssh", target, f"mkdir -p {REMOTE_DIR}"],
            ["scp", "-O", *SUITE_FILES, f"{target}:{REMOTE_DIR}/"],
            ["ssh", target, run]]


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
