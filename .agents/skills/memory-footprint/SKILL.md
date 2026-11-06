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
python3 tools/track_memory_footprint.py                  # targets in tools/membrowse-targets.json
python3 tools/track_memory_footprint.py --allow-missing  # unbuilt targets SKIPPED instead of failed
python3 tools/track_memory_footprint.py --elf build_e906/apps/*/e906_repl_app   # zero-heap check on any ELF
```

## Pass criteria

The script exits 0. It exits 1 when a configured ELF is missing (unless `--allow-missing`) or contains a heap symbol: `malloc`, `free`, `calloc`, `realloc`, their `_r` forms, `_sbrk`, `_sbrk_r`, or global `operator new` / `delete`, defined or undefined ([SPEC-AUDIT-19]). Budgets are printed, not enforced by the exit code yet: every target should be under `ram_budget_bytes` and grow less than `max_allowed_growth_bytes` per commit. Budgets must match the real memory map (E906 firmware window is 192 KB of SRAM Space 0, [SPEC-E906-MEM-01]).

## Evidence

`tools/visualizer/memory_metrics.json`.
