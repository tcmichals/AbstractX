---
name: thread-sanitizer
runs_on: host
verifies: "Data races between threads on host builds (Linux target workers, SITL)."
description: "Check skill. Data races between threads on host builds (Linux target workers, SITL). Run when a requirement names this skill in its Verified by line or a change touches what it checks."
---

# Skill: thread-sanitizer

**What it verifies**: Data races between threads on host builds (Linux target workers, SITL).

## How to run

```bash
cmake -S . -B build_tsan -DABSTRACTX_TARGET=host -DCMAKE_CXX_FLAGS="-fsanitize=thread -O1 -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build_tsan -j
ctest --test-dir build_tsan
```

## Pass criteria

Every test passes with zero ThreadSanitizer reports (a report fails the test). Clean since 2026-10-15; required before every commit touching `include/`, `src/`, `targets/linux/`, `sim/`.

## Evidence

ctest output.
