#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the portable unit tests on the E906 ([SPEC-AUDIT-18] one command on the board).

  python3 tools/run_e906_unit_tests.py [--ip IP] [--elf PATH] [--record] [--timeout S] [--dry-run]

Copies the e906_unit_tests firmware to /lib/firmware, restarts remoteproc0 with it, reads trace0 until
"UNIT: <n> passed, <m> failed", then restores the firmware that was running. Exit 0 all passed,
1 a test failed, 2 could not run.
"""

import argparse
import datetime as dt
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import board_target  # noqa: E402
from trac_lib import ledger  # noqa: E402

RPROC = "/sys/class/remoteproc/remoteproc0"
TRACE0 = "/sys/kernel/debug/remoteproc/remoteproc0/trace0"
FW_NAME = "e906_unit_tests.elf"
DEFAULT_ELF = ROOT / "build_e906/apps/e906_unit_tests/e906_unit_tests"
UNIT_RE = re.compile(r"UNIT: (\d+) passed, (\d+) failed")


# @impl [SPEC-AUDIT-18] docs/SPECTRACE.md (board run: swap firmware, read trace0, restore)
def remote_script(timeout_s: int) -> str:
    """Shell run on the board; prints trace0 and always restores the previous firmware."""
    return f"""set -u
R={RPROC}; OLD=$(cat $R/firmware); WAS=$(cat $R/state)
restore() {{ echo stop > $R/state 2>/dev/null; echo "$OLD" > $R/firmware; [ "$WAS" = running ] && echo start > $R/state; }}
trap restore EXIT
[ "$WAS" = running ] && echo stop > $R/state
echo {FW_NAME} > $R/firmware && echo start > $R/state || {{ echo "could not start {FW_NAME}"; exit 2; }}
for i in $(seq 1 {timeout_s * 10}); do grep -q 'UNIT:' {TRACE0} 2>/dev/null && break; sleep 0.1; done
cat {TRACE0}
"""


def commands(ip: str, elf: Path, timeout_s: int) -> list[list[str]]:
    target = f"root@{ip}"
    return [["scp", "-O", str(elf), f"{target}:/lib/firmware/{FW_NAME}"],
            ["ssh", target, remote_script(timeout_s)]]


def parse(trace: str) -> dict:
    """Results from trace0 text: per-test lines and the UNIT summary (None if it never came)."""
    tests = [(m.group(1), m.group(2)) for m in re.finditer(r"^(PASS|FAIL) (\S+)$", trace, re.M)]
    m = UNIT_RE.search(trace)
    return {"tests": tests, "passed": int(m.group(1)) if m else None, "failed": int(m.group(2)) if m else None}


# @impl [SPEC-AUDIT-18] docs/SPECTRACE.md (ledger record of a board unit-test run)
def make_record(res: dict, commit: str, dirty: bool, now: str) -> dict:
    ok = res["passed"] is not None and res["failed"] == 0 and res["passed"] > 0
    return {"check": "e906-unit-tests", "commit": commit, "dirty": dirty, "family": "board", "host": "cubie-a5e",
            "source": "board", "time": now, "result": "pass" if ok else "fail",
            "metrics": {"passed": res["passed"] or 0, "failed": res["failed"] if res["failed"] is not None else -1},
            "delta": {}}


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ip", default=None)
    ap.add_argument("--elf", type=Path, default=DEFAULT_ELF)
    ap.add_argument("--record", action="store_true")
    ap.add_argument("--timeout", type=int, default=30, help="seconds to wait for the UNIT line")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv)
    ip = a.ip or board_target.board_ip(default=None)
    if not ip:
        print("could not run: no board (--ip or ABSTRACTX_BOARD_IP)")
        return 2
    cmds = commands(ip, a.elf, a.timeout)
    if a.dry_run:
        print(" ".join(shlex.quote(x) for x in cmds[0]))
        print(f"ssh root@{ip} <<'EOF'\n{cmds[1][2]}EOF")
        return 0
    if not a.elf.is_file():
        print(f"could not run: {a.elf} not built (cmake --build build_e906 --target e906_unit_tests)")
        return 2
    board_target.require_a5e(ip)
    subprocess.run(cmds[0], check=True, timeout=120)
    out = subprocess.run(cmds[1], capture_output=True, text=True, timeout=a.timeout + 60)
    print(out.stdout, end="")
    res = parse(out.stdout)
    if res["passed"] is None:
        print("could not run: no UNIT line in trace0")
        return 2
    if a.record:
        now = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        rec = make_record(res, git("rev-parse", "--short=12", "HEAD"), bool(git("status", "--porcelain")), now)
        print(f"recorded in {ledger.append(ROOT, rec).relative_to(ROOT)}: {rec['result']}")
    return 0 if res["failed"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
