---
name: host-tests
runs_on: host
verifies: "Host unit and simulation tests (CppUTest + sim + examples) for the C++ core, HAL logic and host-runnable target code."
description: "Check skill. Host unit and simulation tests (CppUTest + sim + examples) for the C++ core, HAL logic and host-runnable target code. Run when a requirement names this skill in its Verified by line or a change touches what it checks."
---

# Skill: host-tests

**What it verifies**: Host unit and simulation tests (CppUTest + sim + examples) for the C++ core, HAL logic and host-runnable target code.

## How to run

```bash
cmake -S . -B build_host -DABSTRACTX_TARGET=host
cmake --build build_host -j
ctest --test-dir build_host
```

## Pass criteria

Every test passes.

## Evidence

ctest output; summary line recorded in the commit message and by `quality-trends`.
