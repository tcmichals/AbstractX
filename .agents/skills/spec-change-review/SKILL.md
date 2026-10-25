---
name: spec-change-review
runs_on: host
verifies: "Every changed line of code is governed by a requirement, and changed requirements are re-verified (the SpecTrace retrieval step)."
description: "Check skill. Every changed line of code is governed by a requirement, and changed requirements are re-verified (the SpecTrace retrieval step). Run when a requirement names this skill in its Verified by line or a change touches what it checks."
---

# Skill: spec-change-review

**What it verifies**: Every changed line of code is governed by a requirement, and changed requirements are re-verified (the SpecTrace retrieval step).

## How to run

```bash
python3 tools/spectrace_matrix.py --changes --base origin/main          # report
python3 tools/spectrace_matrix.py --changes --base origin/main --prompt   # review bundle for an AI
python3 tools/spectrace_matrix.py --changes --staged --strict             # what the pre-commit hook runs
git config core.hooksPath tools/git-hooks                                 # enable the hook once per clone

# Audit and verify human-readable documentation parity (Zero Drift)
python3 tools/spec_to_doc.py --target pio --check
python3 tools/spec_to_doc.py --target rtl --check
```

## Pass criteria

No untraced code hunks (or each one has a new requirement); every requirement touched by the change has its `Verified by` skills re-run; all human-readable developer documentation is 100% synchronized with specifications and RTL/C++ source code via `python3 tools/spec_to_doc.py --check`.

## Evidence

Report output in the review or PR.
