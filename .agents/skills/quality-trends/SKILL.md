---
name: quality-trends
verifies: "Commit-over-commit history of tests, coverage and memory so regressions are visible."
description: "Check skill. Commit-over-commit history of tests, coverage and memory so regressions are visible. Run when a requirement names this skill in its Verified by line or a change touches what it checks."
---

# Skill: quality-trends

**What it verifies**: Commit-over-commit history of tests, coverage and memory so regressions are visible.

## How to run

```bash
python3 tools/track_quality_trends.py --record
```

## Pass criteria

A new entry is recorded for the commit; deltas reviewed.

## Evidence

`tools/visualizer/quality_trends.json`, `docs/verification/RUNNING_PROFILE.md`.
