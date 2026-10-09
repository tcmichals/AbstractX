---
name: spectrace-review
description: Run the canonical SpecTrace scoped adversarial review and static invariant quality gate.
---

Run the canonical SpecTrace review across the current changes or a specified module.

### Command Execution:
Run the repository command:
```bash
python3 tools/spectrace.py --review $ARGUMENTS
```

### SpecTrace Modes:
- **Code Invariant Review (Stages 1-5)**:
  `python3 tools/spectrace.py --review [--module <path>] [--base-ref origin/main]`
- **Shift-Left Spec Review (Audit the SPECIFICATION.md)**:
  `python3 tools/spectrace.py --spec-review [--module <path>]`
- **Anti-Drift & Parity Check (Spec <-> Code)**:
  `python3 tools/spectrace.py --drift [--module <path>]`

Adhere to the bounded AI review context emitted by the command. Do not modify C++ or RTL implementation until any flagged defects or missing contracts are documented in the appropriate `SPECIFICATION.md` first (`// @impl [SPEC-*]`).
