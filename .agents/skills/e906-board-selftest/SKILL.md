---
name: e906-board-selftest
runs_on: board
verifies: "E906 behaviour on the Cubie A5E itself: REPL, TLP and CTF trace planes decoded from the real endpoints, abstractx API, compass, GPS, watchdog, quiet kernel log ([SPEC-E906-REPL-15])."
description: "Check skill. Runs tests/board/run_board_tests.py on the Cubie A5E with plain python3 (no pytest needed). Use after changing E906 firmware, modabstractx, the RPMsg tools or wire formats, and before claiming any E906 plane works."
---

# Skill: e906-board-selftest

**What it verifies**: the E906 planes and sensors on real silicon, read from the real
RPMsg endpoints, never from host-synthesised data ([SPEC-E906-REPL-15]).

## Preconditions

- The board is free (coordinate with other sessions before using it).
- `/boot/config.txt` loads `cubie-a5e-flight-stack-e906`; remoteproc0 runs `e906_repl_app.elf`.

## How to run

From the repository root on the PC:

Set the board first: `export ABSTRACTX_BOARD_IP=192.168.1.13` (the A5E on Wi-Fi since 2026-10-31; `192.168.3.3` is now the Cubie A7A). Host tools that change board state check `/proc/device-tree/model` for `Cubie A5E` ([SPEC-AUDIT-13], `tools/board_target.py`).

```bash
python3 tools/run_e906_board_suite.py --record          # check board, copy, run, fetch JSON, ledger record
python3 tools/run_e906_board_suite.py -k uart           # one test
python3 tools/run_e906_board_suite.py --dry-run         # print the ssh / scp commands only
```

The script ([SPEC-E906-REPL-15] one command) replaces the hand-copied ssh / scp steps; the JSON lands
in `docs/verification/evidence/<YYYY-MM>/` and `--record` adds a `source: board` ledger record (commit
the ledger file). Record from a clean tree: a dirty tree never counts as evidence.

Run one test with `-k <substring>`. Set `E906_FIRMWARE` if a different firmware name is expected.

## Pass criteria

Exit 0 and every line `PASS`. A `SKIP` is not a pass for the requirement it covers.
Exit 2 means the suite could not run (not on an E906 board).

## Evidence

Runner output and the JSON record, quoted in the commit message; `engineering_log.md`
entry for every failure found.
