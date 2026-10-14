---
name: spec-change-review
verifies: Every changed line of code is governed by a requirement, and changed requirements are re-verified (the SpecTrace retrieval step).
---

# Skill: spec-change-review

**What it verifies**: Every changed line of code is governed by a requirement, and changed requirements are re-verified (the SpecTrace retrieval step).

## How to run

```bash
python3 tools/spectrace_matrix.py --changes --base origin/main          # report
python3 tools/spectrace_matrix.py --changes --base origin/main --prompt   # review bundle for an AI
```

## Pass criteria

No untraced code hunks (or each one has a new requirement); every requirement touched by the change has its `Verified by` skills re-run.

## Evidence

Report output in the review or PR.
