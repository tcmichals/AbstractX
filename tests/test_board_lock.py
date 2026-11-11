#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/board_lock.py without a board: the lock scripts run in a local `sh` in place of ssh, against a
temporary directory in place of /run/abstractx-board.lock, so the real mkdir / owner / token logic runs.

@verifies [SPEC-AUDIT-13] docs/SPECTRACE.md (board lock: acquire, release, hold, LockHeld)
"""
import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import board_lock  # noqa: E402

_REAL_RUN = subprocess.run  # runner tests patch subprocess.run; the local "board" still needs a shell


def local_board(lock_path: str, other=None, calls=None):
    """Stands in for subprocess.run: an ssh command that names lock_path runs in a local sh (the board's
    busybox sh is POSIX too); any other command goes to `other` (default: rc 0, no output)."""
    def run(cmd, **kw):
        if calls is not None:
            calls.append(cmd)
        if cmd and cmd[0] == "ssh" and lock_path in cmd[-1]:
            return _REAL_RUN(["sh", "-c", cmd[-1]], capture_output=True, text=True, timeout=30)
        if other is not None:
            return other(cmd, **kw)
        return subprocess.CompletedProcess(cmd, 0, stdout="", stderr="")
    return run


@contextlib.contextmanager
def lock_dir():
    with tempfile.TemporaryDirectory() as d:
        yield str(Path(d) / "abstractx-board.lock")


def owner_of(path: str) -> dict:
    return json.loads((Path(path) / "owner").read_text())


def test_acquire_writes_owner_and_release_removes_it():
    with lock_dir() as p:
        run = local_board(p)
        me = board_lock.acquire("10.0.0.9", "tool_a", run=run, path=p)
        got = owner_of(p)
        assert got == me
        assert set(got) >= {"host", "user", "pid", "tool", "start", "start_epoch", "token"}
        assert got["tool"] == "tool_a" and got["pid"] == os.getpid()
        assert board_lock.release("10.0.0.9", me, run=run, path=p) is True
        assert not os.path.exists(p)


def test_held_lock_is_refused_and_names_the_holder():
    with lock_dir() as p:
        run = local_board(p)
        first = board_lock.acquire("10.0.0.9", "copilot_suite", run=run, path=p)
        try:
            board_lock.acquire("10.0.0.9", "claude_bench", run=run, path=p)
        except board_lock.LockHeld as exc:
            msg = str(exc)
        else:
            raise AssertionError("a held lock must never be taken")
        assert "in use" in msg and "copilot_suite" in msg and first["user"] in msg and str(first["pid"]) in msg
        assert owner_of(p)["token"] == first["token"], "the refused session must not touch the lock"


def test_force_breaks_only_a_stale_lock():
    with lock_dir() as p:
        run = local_board(p)
        old = board_lock.acquire("10.0.0.9", "died", run=run, path=p, now=time.time() - 4 * 3600)
        young = None
        with contextlib.redirect_stderr(io.StringIO()):
            me = board_lock.acquire("10.0.0.9", "new", force=True, stale_min=180, run=run, path=p)
        assert owner_of(p)["token"] == me["token"] != old["token"]
        try:  # a 1-minute-old lock is never broken, even with --force-lock
            young = board_lock.acquire("10.0.0.9", "other", force=True, stale_min=180, run=run, path=p)
        except board_lock.LockHeld as exc:
            assert "older than 180" in str(exc)
        assert young is None and owner_of(p)["token"] == me["token"]


def test_force_breaks_an_unreadable_lock():
    with lock_dir() as p:
        os.mkdir(p)  # a session died between mkdir and writing owner
        with contextlib.redirect_stderr(io.StringIO()):
            me = board_lock.acquire("10.0.0.9", "new", force=True, run=local_board(p), path=p)
        assert owner_of(p)["token"] == me["token"]


def test_break_does_not_remove_a_lock_taken_in_between():
    with lock_dir() as p:
        run = local_board(p)
        board_lock.acquire("10.0.0.9", "a", run=run, path=p)
        judged = (Path(p) / "owner").read_text().strip()
        (Path(p) / "owner").write_text('{"tool": "b"}\n')  # someone else broke it and took it meanwhile
        res = run(["ssh", "root@10.0.0.9", board_lock.break_script(judged, p)])
        assert res.stdout.strip() == "CHANGED" and owner_of(p) == {"tool": "b"}


def test_release_never_frees_a_lock_that_is_not_ours():
    with lock_dir() as p:
        run = local_board(p)
        me = board_lock.acquire("10.0.0.9", "a", run=run, path=p)
        (Path(p) / "owner").write_text('{"tool": "b", "token": "someone-else"}\n')  # reboot + new holder
        with contextlib.redirect_stderr(io.StringIO()) as err:
            assert board_lock.release("10.0.0.9", me, run=run, path=p) is False
        assert "not released" in err.getvalue() and os.path.isdir(p)


def test_hold_releases_on_error_and_ctrl_c():
    with lock_dir() as p:
        run = local_board(p)
        for exc in (RuntimeError("row failed"), KeyboardInterrupt(), SystemExit(2)):
            try:
                with board_lock.hold("10.0.0.9", "t", run=run, path=p) as info:
                    assert os.path.isdir(p) and info["path"] == p and info["acquired"] == info["start"]
                    raise exc
            except (RuntimeError, KeyboardInterrupt, SystemExit):
                pass
            assert not os.path.exists(p), f"lock left behind after {type(exc).__name__}"
        with board_lock.hold("10.0.0.9", "t", run=run, path=p):
            pass
        assert not os.path.exists(p)


def test_sigterm_releases():
    """A runner killed with SIGTERM (or a closed terminal, SIGHUP) still releases the lock."""
    child = (
        "import sys, time, subprocess; sys.path.insert(0, sys.argv[1]); import board_lock\n"
        "run = lambda c, **k: subprocess.run(['sh', '-c', c[-1]], capture_output=True, text=True)\n"
        "with board_lock.hold('10.0.0.9', 't', run=run, path=sys.argv[2]):\n"
        "    print('held', flush=True); time.sleep(30)\n")
    with lock_dir() as p:
        proc = subprocess.Popen([sys.executable, "-c", child, str(ROOT / "tools"), p], stdout=subprocess.PIPE,
                                text=True)
        assert proc.stdout.readline().strip() == "held" and os.path.isdir(p)
        proc.terminate()
        assert proc.wait(timeout=10) == 128 + 15
        assert not os.path.exists(p), "SIGTERM left the lock behind"


def test_unreachable_board_is_lock_held_not_a_crash():
    def down(cmd, **kw):
        return subprocess.CompletedProcess(cmd, 255, stdout="", stderr="ssh: connect to host: No route")
    try:
        board_lock.acquire("10.0.0.9", "t", run=down, path="/run/x.lock")
    except board_lock.LockHeld as exc:
        assert "No route" in str(exc)
    else:
        raise AssertionError("no lock without the board")


def test_manifest_entry_has_no_token():
    info = {"path": "/run/abstractx-board.lock", "host": "pc", "user": "u", "pid": 1, "tool": "t",
            "acquired": "2026-11-11T12:00:00Z", "start": "2026-11-11T12:00:00Z", "start_epoch": 1, "token": "x"}
    assert board_lock.manifest_entry(info) == {"path": "/run/abstractx-board.lock", "host": "pc", "user": "u",
                                               "pid": 1, "tool": "t", "acquired": "2026-11-11T12:00:00Z"}


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
    print("[PASS] board_lock")
