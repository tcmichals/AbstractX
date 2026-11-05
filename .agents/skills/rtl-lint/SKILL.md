---
name: rtl-lint
runs_on: rtl-sim
verifies: "RTL is free of multi-driven signals, width errors and incomplete cases that simulation hides."
description: "Check skill. RTL is free of multi-driven signals, width errors and incomplete cases that simulation hides. Run when a requirement names this skill in its Verified by line or a change touches what it checks."
---

# Skill: rtl-lint

**What it verifies**: RTL is free of multi-driven signals, width errors and incomplete cases that simulation hides.

## How to run

```bash
tools/rtl_lint.sh                    # every rtl/*.sv, rtl/*/*.sv and hw/**/rtl/*.sv as its own top
tools/rtl_lint.sh rtl/asp_top.sv     # chosen files
```

One include list and the waiver files (`rtl/pio/src/rtl/third_party.vlt`,
`rtl/pio_tlp/asp_pio_wrap.vlt`, each waiver with its reason) for every file. Exit 1 when any file
fails, 2 when Verilator is missing (not a pass). The pio2 submodule's own RTL is linted in pio2; here
it is read through the tops that use it.

2026-11-05: the inline loop this replaced had a stale include path (`asp_top` and `asp_pio_wrap`
could not find `wbm2axilite`), used `hw/**` without `globstar` (so it only matched one level) and
set no exit status.

## Pass criteria

No `MULTIDRIVEN`, `SYNCASYNCNET` or `CASEINCOMPLETE`; width warnings reviewed.

## Evidence

Lint output in the commit message.
