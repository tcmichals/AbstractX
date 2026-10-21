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
```

## Pass criteria

No untraced code hunks (or each one has a new requirement); every requirement touched by the change has its `Verified by` skills re-run.

## Evidence

Report output in the review or PR.
