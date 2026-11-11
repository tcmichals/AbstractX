#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/run_e906_board_suite.py without a board: every file it copies exists, the dry run prints the
three steps, and a suite result becomes a ledger record the matrix reads as board evidence. The board
lock ([SPEC-AUDIT-13]) is taken before anything else, a held lock stops the run, and the evidence names
the holder (lock scripts run in a local sh, tests/test_board_lock.py).

@verifies [SPEC-E906-REPL-15] apps/e906_repl_app/SPECIFICATION.md (make_record commands)
@verifies [SPEC-AUDIT-13] docs/SPECTRACE.md (board lock: run_locked, manifest_entry)
"""
import io
import contextlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import run_e906_board_suite as r  # noqa: E402
sys.path.insert(0, str(ROOT / "tests"))
from test_board_lock import local_board  # noqa: E402

BAD_SETUP = "tree=1.5\nstate=running\nfirmware=wrong.elf\nresolved=/lib/firmware/wrong.elf\n"


def check_lock_first_and_released() -> None:
    """Wrong DT stops the run after the lock; the lock is taken before the model check and released."""
    with tempfile.TemporaryDirectory() as d:
        lock = str(Path(d) / "abstractx-board.lock")
        calls, order = [], []
        run = local_board(lock, other=lambda cmd, **kw: subprocess.CompletedProcess(cmd, 0, stdout=BAD_SETUP,
                                                                                      stderr=""), calls=calls)
        with patch.object(r.board_lock, "LOCK_PATH", lock), patch.object(
                r.board_target, "require_a5e", side_effect=lambda ip: order.append(("model", len(calls)))), \
                patch.object(r.subprocess, "run", side_effect=run), \
                contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
            try:
                r.main(["--ip", "10.0.0.9"])
            except SystemExit as exc:
                assert exc.code == 2
            else:
                assert False, "wrong DT / firmware must stop before copying or running the suite"
        assert lock in calls[0][-1] and "mkdir" in calls[0][-1], "the lock is the first thing on the board"
        assert order == [("model", 1)], "the model check runs after the lock"
        assert lock in calls[-1][-1] and "RELEASED" in calls[-1][-1], "released on the way out"
        assert len(calls) == 3, [c[:2] for c in calls]  # lock, setup probe, release: nothing copied
        assert not os.path.exists(lock)


def check_held_lock_stops_before_the_board() -> None:
    with tempfile.TemporaryDirectory() as d:
        lock = str(Path(d) / "abstractx-board.lock")
        os.mkdir(lock)
        (Path(lock) / "owner").write_text('{"tool": "copilot_bench", "user": "x", "host": "pc2", "pid": 7, '
                                          '"start": "2026-11-11T12:12:00Z", "start_epoch": 4102444800}\n')
        calls = []
        err = io.StringIO()
        with patch.object(r.board_lock, "LOCK_PATH", lock), patch.object(r.board_target, "require_a5e") as model, \
                patch.object(r.subprocess, "run", side_effect=local_board(lock, calls=calls)), \
                contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            assert r.main(["--ip", "10.0.0.9", "--force-lock"]) == 2, "a young lock is never broken"
        model.assert_not_called()
        assert len(calls) == 1 and "copilot_bench" in err.getvalue() and "pc2" in err.getvalue(), err.getvalue()
        assert (Path(lock) / "owner").read_text().startswith('{"tool": "copilot_bench"'), "holder untouched"


def check_evidence_names_the_lock_holder() -> None:
    with tempfile.TemporaryDirectory() as d:
        lock = str(Path(d) / "abstractx-board.lock")

        def board(cmd, **kw):
            if cmd[0] == "scp" and cmd[-2].endswith(r.REMOTE_JSON):  # fetch the suite's JSON
                Path(cmd[-1]).write_text(json.dumps({"counts": {"PASS": 1}}))
            return subprocess.CompletedProcess(cmd, 0, stdout="", stderr="")
        with patch.object(r.board_lock, "LOCK_PATH", lock), patch.object(r.board_target, "require_a5e"), \
                patch.object(r, "require_e906_test_setup"), patch.object(r, "ROOT", Path(d)), \
                patch.object(r.subprocess, "run", side_effect=local_board(lock, other=board)), \
                contextlib.redirect_stdout(io.StringIO()):
            assert r.main(["--ip", "10.0.0.9"]) == 0
        [out] = list(Path(d).glob("docs/verification/evidence/*/e906_selftest_*.json"))
        got = json.loads(out.read_text())
        assert got["counts"] == {"PASS": 1}
        assert got["board_lock"]["tool"] == "run_e906_board_suite" and got["board_lock"]["path"] == lock
        assert {"host", "user", "pid", "acquired"} <= set(got["board_lock"]) and "token" not in got["board_lock"]
        assert not os.path.exists(lock)


def main() -> int:
    missing = [f for f in r.SUITE_FILES if not (ROOT / f).is_file()]
    assert not missing, f"files the suite needs are missing: {missing}"
    assert "tests/board/run_board_tests.py" in r.SUITE_FILES
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        assert r.main(["--ip", "10.0.0.9", "--dry-run", "-k", "uart"]) == 0
    lines = out.getvalue().splitlines()
    assert lines[0].startswith("# board lock") and "not in a dry run" in lines[0], lines[0]
    lines = lines[1:]
    assert len(lines) == 3 and lines[0].startswith("ssh root@10.0.0.9") and lines[1].startswith("scp -O")
    assert "python3 -c" in lines[2] and "timeout=600" in lines[2], lines[2]
    assert "run_board_tests.py" in lines[2] and "-k" in lines[2] and "uart" in lines[2], lines[2]
    assert "timeout 600 python3" not in lines[2], lines[2]
    rec = r.make_record({"counts": {"PASS": 23, "FAIL": 0, "SKIP": 2}, "firmware": "e906_repl_app.elf",
                         "uname": "7.1.0"}, "abc123def456", False, 0, "2026-11-06T12:00:00Z")
    assert rec["source"] == "board" and rec["result"] == "pass" and rec["check"] == "e906-board-selftest"
    assert rec["metrics"] == {"passed": 23, "failed": 0, "skipped": 2, "firmware": "e906_repl_app.elf", "uname": "7.1.0"}
    bad = r.make_record({"counts": {"PASS": 20, "FAIL": 1, "SKIP": 0}}, "abc", False, 1, "2026-11-06T12:00:00Z")
    assert bad["result"] == "fail"
    assert r.make_record({"counts": {"PASS": 3}}, "abc", False, 2, "t")["result"] == "fail", "could not run is not a pass"
    setup = "tree=1.6-e906\nstate=running\nfirmware=riscv-firmware.elf\nresolved=/lib/firmware/e906_repl_app.elf\n"
    assert r.validate_test_setup(setup) == []
    errors = r.validate_test_setup(
        "tree=1.5\nstate=running\nfirmware=riscv-firmware.elf\nresolved=/lib/firmware/riscv-firmware.elf\n")
    assert len(errors) == 2 and "device tree" in errors[0] and "firmware" in errors[1], errors
    check_lock_first_and_released()
    check_held_lock_stops_before_the_board()
    check_evidence_names_the_lock_holder()
    with contextlib.redirect_stdout(io.StringIO()):
        os.environ.pop("ABSTRACTX_BOARD_IP", None)
        assert r.main([]) == 2, "no board -> could not run"
    print("[PASS] run_e906_board_suite")
    return 0


if __name__ == "__main__":
    sys.exit(main())
