---
name: e906-build-layout
verifies: "The E906 firmware cross-builds and every load segment fits a window remoteproc accepts ([SPEC-E906-MEM-01])."
description: "Check skill. The E906 firmware cross-builds and every load segment fits a window remoteproc accepts ([SPEC-E906-MEM-01]). Run when a requirement names this skill in its Verified by line or a change touches what it checks."
---

# Skill: e906-build-layout

**What it verifies**: The E906 firmware cross-builds and every load segment fits a window remoteproc accepts ([SPEC-E906-MEM-01]).

## How to run

```bash
cmake --preset e906
cmake --build build_e906 -j      # runs tools/check_e906_layout.py after each link
```

## Pass criteria

Build succeeds; every segment prints `[ok]`; no `[FAIL]`.

## Evidence

Build log; `riscv-none-elf-size` output in the commit message.
