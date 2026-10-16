# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Evidence ledger: one JSON record per check run, appended to
docs/verification/evidence/<YYYY-MM>/<host>.jsonl (one file per month and machine,
so several PCs never conflict on merge). Never edited; a correction is a new record.
"""
# @impl [SPEC-AUDIT-09] docs/SPECTRACE.md

import datetime as _dt
import json
import socket
import subprocess
from pathlib import Path

LEDGER_DIR = Path("docs/verification/evidence")


def git_state(root: Path) -> tuple[str, bool]:
    def run(*args):
        return subprocess.run(["git", *args], cwd=root, capture_output=True, text=True).stdout.strip()
    commit = run("rev-parse", "--short=12", "HEAD") or "unknown"
    dirty = bool(run("status", "--porcelain", "--untracked-files=no"))
    return commit, dirty


def make_record(root: Path, family: str, check: str, result: str, metrics: dict,
                requirements=(), duration_s=0.0, tool="", notes="") -> dict:
    commit, dirty = git_state(root)
    return {
        "time": _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "commit": commit, "dirty": dirty, "host": socket.gethostname(),
        "family": family, "check": check, "requirements": list(requirements),
        "result": result, "metrics": metrics, "delta": {}, "duration_s": round(duration_s, 3),
        "tool": tool, "notes": notes,
    }


def read_all(root: Path) -> list[dict]:
    records = []
    for path in sorted((root / LEDGER_DIR).glob("*/*.jsonl")):
        for line in path.read_text().splitlines():
            if line.strip():
                records.append(json.loads(line))
    records.sort(key=lambda r: r["time"])
    return records


def previous(records: list[dict], check: str) -> dict | None:
    for rec in reversed(records):
        if rec["check"] == check:
            return rec
    return None


def with_delta(record: dict, prev: dict | None) -> dict:
    if prev:
        for k, v in record["metrics"].items():
            pv = prev["metrics"].get(k)
            if isinstance(v, (int, float)) and isinstance(pv, (int, float)):
                record["delta"][k] = round(v - pv, 6)
    return record


def append(root: Path, record: dict) -> Path:
    month = record["time"][:7]
    path = root / LEDGER_DIR / month / f"{record['host']}.jsonl"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a") as f:
        f.write(json.dumps(record, sort_keys=True) + "\n")
    return path
