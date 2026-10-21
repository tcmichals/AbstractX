---
name: memory-footprint
runs_on: host
verifies: "ELF section sizes per target stay inside their memory budget and do not grow unexpectedly."
description: "Check skill. ELF section sizes per target stay inside their memory budget and do not grow unexpectedly. Run when a requirement names this skill in its Verified by line or a change touches what it checks."
---

# Skill: memory-footprint

**What it verifies**: ELF section sizes per target stay inside their memory budget and do not grow unexpectedly.

## How to run

```bash
python3 tools/track_memory_footprint.py   # targets in tools/membrowse-targets.json
```

## Pass criteria

Every target is under `ram_budget_bytes` and grows less than `max_allowed_growth_bytes` per commit. Budgets must match the real memory map (E906 firmware window is 192 KB of SRAM Space 0, [SPEC-E906-MEM-01]).

## Evidence

`tools/visualizer/memory_metrics.json`.
