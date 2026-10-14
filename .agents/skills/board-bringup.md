---
name: board-bringup
verifies: Behaviour on real hardware that no simulation can show (link timing, interrupts, silicon errata).
---

# Skill: board-bringup

**What it verifies**: Behaviour on real hardware that no simulation can show (link timing, interrupts, silicon errata).

## How to run

```bash
Follow the bring-up steps in the requirement (e.g. [SPEC-TN20K-04]).
```

## Pass criteria

Every step in the requirement passes on the board.

## Evidence

Log under `docs/verification/evidence/` with board, firmware/bitstream hash and date.
