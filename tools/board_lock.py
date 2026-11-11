# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""One session drives the board at a time: a lock directory on the board ([SPEC-AUDIT-13] board lock).

The runners that change board state (tools/run_e906_bench_suite.py, tools/run_e906_board_suite.py)
take the lock before they touch the board and release it on every exit:

    with board_lock.hold(ip, tool="run_e906_bench_suite", force=a.force_lock) as lock:
        ...                       # lock.info: holder and acquire time, for the evidence manifest

`mkdir` is atomic (also on busybox), so of two sessions only one creates the directory. The directory
holds `owner` (one JSON line: host, user, pid, tool, start time, token). A held lock is never taken:
the runner prints the holder and exits 2. `--force-lock` breaks a lock older than the stale limit
(the holder's start time against this PC's clock), never a younger one. Release removes the lock only
if its token is ours, so a run never frees a lock someone else took after a reboot or a forced break.
"""
from __future__ import annotations

import contextlib
import datetime as dt
import getpass
import json
import os
import shlex
import signal
import socket
import subprocess
import sys
import time
import uuid

LOCK_PATH = "/run/abstractx-board.lock"  # tmpfs: a board reboot clears it
STALE_MIN = 180                          # --force-lock breaks only locks older than this
SSH_OPTS = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=5"]


class LockHeld(RuntimeError):
    """The board lock is held by another session (or could not be taken)."""


def owner_record(tool: str, now: float | None = None) -> dict:
    now = time.time() if now is None else now
    return {"host": socket.gethostname(), "user": getpass.getuser(), "pid": os.getpid(), "tool": tool,
            "start": dt.datetime.fromtimestamp(now, dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "start_epoch": int(now), "token": uuid.uuid4().hex}


def acquire_script(owner: dict, path: str = LOCK_PATH) -> str:
    """Prints ACQUIRED, or HELD and the holder's owner line. The owner file is written after mkdir."""
    line = shlex.quote(json.dumps(owner, sort_keys=True))
    p = shlex.quote(path)
    return (f"if mkdir {p} 2>/dev/null; then printf '%s\\n' {line} > {p}/owner && echo ACQUIRED; "
            f"else echo HELD; cat {p}/owner 2>/dev/null; true; fi")


def break_script(old_owner_line: str, path: str = LOCK_PATH) -> str:
    """Removes the lock only if its owner line is still the one we judged stale (not a newer lock)."""
    p = shlex.quote(path)
    return (f"if [ \"$(cat {p}/owner 2>/dev/null)\" = {shlex.quote(old_owner_line)} ]; then "
            f"mv {p} {p}.stale.$$ && rm -rf {p}.stale.$$ && echo BROKEN; else echo CHANGED; fi")


def release_script(token: str, path: str = LOCK_PATH) -> str:
    p = shlex.quote(path)
    return (f"if grep -q {shlex.quote(token)} {p}/owner 2>/dev/null; then rm -rf {p} && echo RELEASED; "
            f"else echo NOT_OURS; fi")


def parse_owner(text: str) -> dict | None:
    try:
        value = json.loads(text.strip())
    except ValueError:
        return None
    return value if isinstance(value, dict) else None


def describe(owner: dict | None, raw: str = "", now: float | None = None) -> str:
    if not owner:
        return f"unknown holder (owner file: {raw.strip() or 'empty'})"
    now = time.time() if now is None else now
    age = ""
    if isinstance(owner.get("start_epoch"), int):
        age = f", {max(0, int(now) - owner['start_epoch']) // 60} min ago"
    return (f"{owner.get('tool', '?')} by {owner.get('user', '?')}@{owner.get('host', '?')} "
            f"pid {owner.get('pid', '?')}, since {owner.get('start', '?')}{age}")


def age_min(owner: dict | None, now: float | None = None) -> float | None:
    if not owner or not isinstance(owner.get("start_epoch"), int):
        return None
    now = time.time() if now is None else now
    return (now - owner["start_epoch"]) / 60.0


def _ssh(target: str, script: str, run, timeout: int = 30) -> subprocess.CompletedProcess:
    return run(["ssh", *SSH_OPTS, target, script], capture_output=True, text=True, timeout=timeout)


# @impl [SPEC-AUDIT-13] docs/SPECTRACE.md (board lock)
def acquire(ip: str, tool: str, force: bool = False, stale_min: float = STALE_MIN, run=None,
            path: str | None = None, now: float | None = None) -> dict:
    """Takes the lock or raises LockHeld (with the holder in the message). Returns our owner record."""
    run, path = run or subprocess.run, path or LOCK_PATH
    target = f"root@{ip}"
    owner = owner_record(tool, now)
    for _ in range(2):  # a second try only after a forced break
        try:
            res = _ssh(target, acquire_script(owner, path), run)
        except subprocess.TimeoutExpired:
            raise LockHeld(f"could not take the board lock {path}: ssh timed out") from None
        lines = (res.stdout or "").splitlines()
        if res.returncode != 0 or not lines or lines[0] not in ("ACQUIRED", "HELD"):
            raise LockHeld(f"could not take the board lock {path}: "
                           f"{(res.stderr or '').strip() or 'ssh exited ' + str(res.returncode)}")
        if lines[0] == "ACQUIRED":
            return owner
        raw = "\n".join(lines[1:])
        holder = parse_owner(raw)
        age = age_min(holder, now)
        who = describe(holder, raw, now)
        if not force:
            raise LockHeld(f"the board is in use: {path} held by {who}. Wait for it, or if that session is "
                           f"gone and the lock is older than {stale_min:g} min, rerun with --force-lock")
        if age is not None and age < stale_min:
            raise LockHeld(f"the board is in use: {path} held by {who}; --force-lock breaks only locks "
                           f"older than {stale_min:g} min")
        print(f"breaking a stale board lock: {who}", file=sys.stderr)
        brk = _ssh(target, break_script(raw.strip(), path), run)
        if (brk.stdout or "").strip() != "BROKEN":
            raise LockHeld(f"the board lock {path} changed while breaking it; rerun to see the new holder")
    raise LockHeld(f"could not take the board lock {path} after breaking it")


# @impl [SPEC-AUDIT-13] docs/SPECTRACE.md (board lock)
def release(ip: str, owner: dict, run=None, path: str | None = None) -> bool:
    """Removes the lock if it is still ours. Never raises: the run's result matters more."""
    run, path = run or subprocess.run, path or LOCK_PATH
    try:
        res = _ssh(f"root@{ip}", release_script(owner["token"], path), run)
    except (OSError, subprocess.TimeoutExpired):
        res = None
    ok = res is not None and (res.stdout or "").strip() == "RELEASED"
    if not ok:
        print(f"warning: board lock {path} not released (board unreachable, rebooted or lock taken by "
              f"another session); check it with: ssh root@{ip} cat {path}/owner", file=sys.stderr)
    return ok


def _raise_exit(signum, _frame):
    raise SystemExit(128 + signum)  # unwinds through hold()'s finally, which releases the lock


# @impl [SPEC-AUDIT-13] docs/SPECTRACE.md (board lock)
@contextlib.contextmanager
def hold(ip: str, tool: str, force: bool = False, stale_min: float = STALE_MIN, run=None,
         path: str | None = None):
    """Context manager: takes the lock, yields our owner record plus `path` and `acquired`, releases on
    any exit (normal return, exception, Ctrl-C, SIGTERM / SIGHUP). Raises LockHeld if it is held."""
    path = path or LOCK_PATH
    owner = acquire(ip, tool, force, stale_min, run, path)
    info = {**owner, "path": path, "acquired": owner["start"]}
    previous = {}
    for sig in (signal.SIGTERM, signal.SIGHUP):
        try:
            previous[sig] = signal.signal(sig, _raise_exit)
        except (ValueError, OSError):  # not the main thread: Ctrl-C and normal exit still release
            pass
    try:
        yield info
    finally:
        for sig, handler in previous.items():
            signal.signal(sig, handler)
        release(ip, owner, run, path)


# @impl [SPEC-AUDIT-13] docs/SPECTRACE.md (board lock)
def manifest_entry(info: dict) -> dict:
    """What the evidence records about the lock (the token stays out of the evidence)."""
    return {k: info[k] for k in ("path", "host", "user", "pid", "tool", "acquired") if k in info}


def add_arguments(ap) -> None:
    """--force-lock and --lock-stale-min for a runner's argparse parser."""
    ap.add_argument("--force-lock", action="store_true",
                    help="break a board lock older than --lock-stale-min (a session that died without releasing it)")
    ap.add_argument("--lock-stale-min", type=float, default=STALE_MIN,
                    help=f"age in minutes after which --force-lock may break the board lock (default {STALE_MIN})")
