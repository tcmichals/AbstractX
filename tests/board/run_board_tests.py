#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
run_board_tests.py - Run tests/board/test_*.py on the target with plain python3 (no pytest).

  python3 run_board_tests.py [-k substring] [--json out.json]

Exit 0 all passed, 1 a test failed, 2 could not run (not an E906 board).
The test files are pytest-compatible; under pytest, skips use pytest.skip.
"""
# @impl [SPEC-E906-REPL-15] apps/e906_repl_app/SPECIFICATION.md

import glob
import importlib
import json
import os
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import board_common  # noqa: E402
from e906_link import on_e906_board, read_text, RPROC, REPL, RPMSG_CTRL, create_endpoint  # noqa: E402


def discover(selector: str = ""):
    tests = []
    for path in sorted(glob.glob(os.path.join(HERE, "test_*.py"))):
        mod = importlib.import_module(os.path.splitext(os.path.basename(path))[0])
        for name, fn in vars(mod).items():  # definition order
            if name.startswith("test_") and callable(fn) and selector in name:
                tests.append((f"{mod.__name__}::{name}", fn))
    return tests


def repl_busy() -> str:
    """'' if the REPL endpoint can be opened, else who holds it (e.g. Studio's bridge)."""
    try:
        node = create_endpoint(RPMSG_CTRL, REPL["name"], REPL["addr"])
        os.close(os.open(node, os.O_RDWR | os.O_NONBLOCK))
        return ""
    except OSError as e:
        holders = []
        for pid in os.listdir("/proc"):
            try:
                with open(f"/proc/{pid}/cmdline", "rb") as f:
                    cmd = f.read().replace(b"\0", b" ").decode(errors="replace").strip()
            except OSError:
                continue
            if "board_studio_bridge" in cmd or "run_e906_script" in cmd:
                holders.append(f"pid {pid}: {cmd[:60]}")
        return f"{e.strerror}; " + ("; ".join(holders) or "holder unknown")


def run(tests):
    results = []
    for tid, fn in tests:
        t0 = time.monotonic()
        try:
            fn()
            status, detail = "PASS", ""
        except board_common.Skipped as e:
            status, detail = "SKIP", str(e)
        except AssertionError as e:
            status, detail = "FAIL", str(e) or traceback.format_exc(limit=2).strip().splitlines()[-1]
        except Exception as e:  # a crash in a test is a failure, never a skip
            status, detail = "FAIL", f"{type(e).__name__}: {e}"
        dt = time.monotonic() - t0
        results.append({"test": tid, "status": status, "detail": detail, "seconds": round(dt, 2)})
        print(f"{status:4s} {tid} ({dt:.1f}s){'  ' + detail if detail else ''}", flush=True)
    return results


def main(argv) -> int:
    selector, json_out = "", None
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == "-k" and args:
            selector = args.pop(0)
        elif a == "--json" and args:
            json_out = args.pop(0)
        else:
            print(__doc__.strip())
            return 2
    tests = discover(selector)
    if not on_e906_board():
        print(f"could not run: {RPROC} not found (not an E906 board); {len(tests)} tests discovered")
        return 2
    busy = repl_busy()
    if busy:
        print(f"could not run: the abstractx-repl endpoint is in use ({busy}); stop Studio's "
              f"live link or the bridge first")
        return 2
    print(f"E906 board self-test: {len(tests)} tests, firmware "
          f"{read_text(RPROC + '/firmware')!r}, state {read_text(RPROC + '/state')!r}")
    results = run(tests)
    counts = {s: sum(r["status"] == s for r in results) for s in ("PASS", "FAIL", "SKIP")}
    print(f"\n{counts['PASS']} passed, {counts['FAIL']} failed, {counts['SKIP']} skipped")
    if json_out:
        record = {"check": "board:e906:selftest", "time": time.strftime("%Y-%m-%dT%H:%M:%S"),
                  "uname": os.uname().release, "counts": counts, "results": results}
        with open(json_out, "w") as f:
            json.dump(record, f, indent=1)
        print(f"evidence: {json_out}")
    return 1 if counts["FAIL"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
