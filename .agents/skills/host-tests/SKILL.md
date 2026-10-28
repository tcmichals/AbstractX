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
   nearby. `python3 tools/spectrace_matrix.py --check-verifies` checks this for every test.
4. **Peers with different addresses.** A fake peer must not reuse our own address or endpoint number.
   That hid swapped src/dst.
5. **Steady state ≥1000 cycles** with resources recycled (AGENTS §10.5).
6. **Portable test scaffolding.** No `MAP_32BIT` or other x86-only flags without a guard; the SITL host
   can be AArch64.
7. **Independent cases.** Each case builds its own fixture; a long dependent `main()` hides every
   failure after the first.
8. **Assertions are live.** `sim/`, `tests/` and `examples/` compile with `-UNDEBUG`, so `assert()`
   runs in Release too; `sim_test_asserts_live` fails to build otherwise (2026-10-28: eleven sim tests,
   ~276 asserts, checked nothing in the default host build).
9. **Concurrency tests run more than once, and fail instead of hanging.** Loop a threaded test
   (`ctest --repeat until-fail:20 -R <name>`) before calling it green, and give a handshake test a
   watchdog that reports the lost wake (2026-10-28: `sim_test_uart_rx_ring` hung in ~1% of runs).

## Pass criteria

Every test passes.

## Evidence

ctest output; summary line recorded in the commit message and by `quality-trends`.
