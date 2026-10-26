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

## Test quality rules (a test that cannot fail is not evidence)

Learned from the 2026-10-26 RPMsg review, where every test passed while 11 transport bugs were present:

1. **Fakes must also fail for good.** For every bounded wait, add one case where the fake never recovers.
   The test must finish (no hang) and assert the bounded outcome (`false` / `Busy` / owed kick).
2. **Test the step after an error.** After asserting an error counter, send the next message and assert
   that it is delivered.
3. **`@verifies [SPEC-X]` means that the test calls the code that implements SPEC-X**, not something
   nearby. `tools/audit_ipc_invariants.py` (`T-VERIFIES`) checks this for the E906 transport tests.
4. **Peers with different addresses.** A fake peer must not reuse our own address or endpoint number.
   That hid swapped src/dst.
5. **Steady state ≥1000 cycles** with resources recycled (AGENTS §10.5).
6. **Portable test scaffolding.** No `MAP_32BIT` or other x86-only flags without a guard; the SITL host
   can be AArch64.
7. **Independent cases.** Each case builds its own fixture; a long dependent `main()` hides every
   failure after the first.

## Pass criteria

Every test passes.

## Evidence

ctest output; summary line recorded in the commit message and by `quality-trends`.
