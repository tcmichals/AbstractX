---
name: sanitizer-gate
verifies: Memory and undefined-behaviour safety of the same tests: use-after-return, overflow, misalignment, leaks.
---

# Skill: sanitizer-gate

**What it verifies**: Memory and undefined-behaviour safety of the same tests: use-after-return, overflow, misalignment, leaks.

## How to run

```bash
cmake -S . -B build_asan -DABSTRACTX_TARGET=host \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build_asan -j
ctest --test-dir build_asan
```

## Pass criteria

Every test passes. `-fno-sanitize-recover=all` is required: without it UBSan prints and the test still passes.

## Evidence

ctest output in the commit message.
