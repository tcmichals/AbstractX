---
name: e906-board-selftest
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

```bash
ssh root@192.168.3.3 mkdir -p /root/abstractx_board_tests
scp -O tests/board/*.py tools/abstractx_wire.py tools/e906_rpmsg_bridge.py \
       apps/e906_repl_app/raw_sensor_streamer.py root@192.168.3.3:/root/abstractx_board_tests/
ssh root@192.168.3.3 "cd /root/abstractx_board_tests && python3 run_board_tests.py --json /tmp/e906_selftest.json"
scp -O root@192.168.3.3:/tmp/e906_selftest.json docs/verification/evidence/
```

Run one test with `-k <substring>`. Set `E906_FIRMWARE` if a different firmware name is expected.

## Pass criteria

Exit 0 and every line `PASS`. A `SKIP` is not a pass for the requirement it covers.
Exit 2 means the suite could not run (not on an E906 board).

## Evidence

Runner output and the JSON record, quoted in the commit message; `engineering_log.md`
entry for every failure found.
