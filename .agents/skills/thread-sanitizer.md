---
name: thread-sanitizer
verifies: Data races between threads on host builds (Linux target workers, SITL).
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

Every test passes with zero ThreadSanitizer reports. Known failing as of 2026-10-14: 3 tests (TODO.md).

## Evidence

ctest output.
