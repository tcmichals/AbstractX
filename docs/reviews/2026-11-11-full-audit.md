# AbstractX project and FreeRTOS integration review (2026-11-11)

**Reviewed revision:** `2af893b`
**Scope:** project audit plus the FreeRTOS host, E906 FreeRTOS application, its
specification, and the current board benchmark evidence. This is a review and
does not change firmware or the existing untracked evidence.

## Summary

The core has a strong requirements-and-gates structure: the full host gate,
trace/contract checks, and E906 cross-build all passed in the initial audit.
The static-stage false green is fixed and clang-tidy was subsequently run
successfully. Coverage, mutation and fuzz checks did not run; 65 existing
`@verifies` claims are on the known baseline; and the latest board benchmark
suite recorded 17 failures among 42 rows.

FreeRTOS is integrated as a host for one AbstractX task, not as a second
preemptive scheduler for AbstractX coroutines. This preserves the single
dispatcher/resume context, but means FreeRTOS priorities, queues and task
preemption apply between tasks, not among coroutines. The E906 board data shows
the cost of the 1 kHz shared tick: AbstractX timer wakes are phase-late by
hundreds of microseconds, while the native FreeRTOS task baseline is on the
tick. Treat the observed mode differences as one run, not as a general ranking.

## Initial automatic audit evidence (before the static-status fix)

Command: `python3 tools/full_audit.py` on `2af893b`.

| Stage | Result | Evidence / qualification |
|---|---|---|
| Host gate (plain, ASan/UBSan, TSan) | PASS | `tools/gate.sh` exited 0. The full-audit report did not retain individual test counts. |
| Trace and contract audits | PASS | 9/9 checks; `audit_specs`, skill/verifies checks, coroutine, boundary, interface, HAL, IPC and wake-fence checks passed. `check-verifies` still reports 65 baseline unbacked claims (63 known, zero new). |
| Static analysis | **Not actually run** | Neither clang-tidy nor cppcheck is installed. The stage is labelled PASS despite both being skipped; see finding R1. Owner: tooling maintainer, currently unassigned. |
| Host coverage | SKIPPED | `gcovr` is not installed. Owner: tooling maintainer, currently unassigned. |
| Mutation testing | SKIPPED | Mull is not installed. Owner: tooling maintainer, currently unassigned. |
| Fuzzing | SKIPPED | No targets exist under `fuzz/`. Owner: parser/runtime maintainer, currently unassigned. |
| E906 build/layout/memory | PASS | Six images fit. `e906_freertos_app` uses 133.0 KB / 192 KB SRAM_FAST (69.2%) in the audit build. |
| CI | PASS, stale for this revision | Latest listed success is `e8aef02` on 2026-11-07, not `2af893b`. |
| Host implementations | REPORT | 3/5: FreeRTOS, Linux/Asio, bare metal; Zephyr and ChibiOS are absent. |
| Blocking-call scan | REPORT | 31 calls remain across E906, Pico, Linux, Zynq and bring-up apps; each needs an explicit boot-only/runtime classification. |

The machine-generated stage report is `build_audit/audit-2026-11-11.md`.

### Follow-up after the fix

- `clang-tidy` was available; the host compile database was not. Configured it
  with `cmake -S . -B build_host -DABSTRACTX_TARGET=host
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.
- `python3 tools/run_static_analysis.py` reported
  `[PASS] static analysis: 0 known finding(s), no new ones`.
- `python3 tools/full_audit.py --stages static` reported PASS with clang-tidy
  actually run. `cppcheck` remains uninstalled, but is advisory.
- `python3 -m pytest -q tests/test_full_audit.py
  tests/test_run_e906_board_suite.py tests/test_run_e906_bench_suite.py`:
  **23 passed**.
- `tools/gate.sh`: **132/132 plain, 121/121 ASan/UBSan, 121/121 TSan**.
- `python3 tools/spec_to_doc.py --target rtl --check`: passed.
- A later `python3 tools/full_audit.py` rerun reported the static stage PASS
  with clang-tidy executed, but the overall gate failed because
  `py_test_studio_gui_clicks` timed out at 60 seconds. An isolated rerun of
  that GUI test hung beyond four minutes and was terminated; this unrelated
  GUI test failure is not counted as a pass. The full audit therefore exited 1.

## FreeRTOS + AbstractX

### What is working well

- One FreeRTOS task owns `step()` and the dispatcher; ISR and other-task work
  reaches it through task notifications. This is a clean mapping of
  `[SPEC-ARCH-15]` and avoids a second coroutine scheduler.
- `WAIT` is the simple default and leaves FreeRTOS in charge of the tick
  comparator. The alternative `XTIMER` and `TICK_HOOK` modes are explicit build
  choices rather than hidden runtime behavior.
- The E906 cross-build and memory/layout check passed. The current size record
  also reports zero heap symbols for the FreeRTOS timing and debug images.
- Existing E906 evidence exercises RPMsg, coroutine yield and IRQ wake paths,
  rather than relying solely on source-level assertions.

### Current board measurements

An earlier run in
`docs/verification/evidence/2026-11/part4_bench_2026-11-11T080247/` reported
19 failed rows out of 42. After the user authorized board tests, the complete
default 60,000-sample sweep was re-run:
`docs/verification/evidence/2026-11/part4_bench_2026-11-11T092420/manifest.json`.
It completed all **42 rows: 25 passed, 17 failed** (9 timed out, 8 returned no
board JSON). The manifest is marked dirty because the worktree had pending
changes; `--record` was not used. The hottest recorded zone reached 51.546 C,
below the runner's 85 C stop limit.

For the 60,000-sample idle cyclic runs in the latest sweep:

| Mode | Average lateness | Maximum lateness | Missed |
|---|---:|---:|---:|
| Bare-metal AbstractX | 12.333 µs | 14.333 µs | 0 |
| FreeRTOS WAIT | 448.541 µs | 449.583 µs | 0 |
| FreeRTOS TICK_HOOK | 553.916 µs | 554.833 µs | 0 |
| FreeRTOS XTIMER | 61.375 µs | 62.125 µs | 0 |
| Native FreeRTOS task | 0 µs reported | 0 µs reported | 0 |

These are individual runs, not enough to conclude that XTIMER is generally
faster. The configured E906 kernel tick is 1 kHz and owns `mtimecmp`; the
AbstractX host therefore cannot promise sub-millisecond sleep resolution in
this configuration. The native task is a different scheduling path, so the
comparison is useful for latency budgeting, not proof that one runtime is
universally superior.

The latest TICK_HOOK row reports `cyc_late_us_p99 = 500` and its
`cyc_over_500us` counter shows that the histogram is saturated; treat this p99
as censored, not as the actual 99th percentile. Load/one-way rows failed with
`timed out` or `no JSON from the board`, so this run does not establish
stressed performance or E906-to-Linux delivery latency.

### Improvements, ranked

1. **R1 — RESOLVED.** The audit's static stage now reports
   `SKIPPED` when required clang-tidy is missing, even if optional cppcheck is
   available. Regression coverage is in `tests/test_full_audit.py`.
   The test suite verifies both missing-analyzer and cppcheck-only cases; the
   initial full-audit table above records the pre-fix false-green. Clang-tidy
   was later run successfully as detailed above.
2. **R2 — Add a FreeRTOS-host runtime/fault test.** The host has no
   FreeRTOS-specific runtime test in the host suite. In `XTIMER` mode, a failed
   `xTimerChangePeriod` increments a counter and comments that it will retry,
   but `run()` subsequently waits on a notification with `portMAX_DELAY`.
   Unless some unrelated event wakes the task, the retry path is not reached.
   Treat this as a high-confidence code-review candidate until a queue-full
   fault-injection test reproduces it. On failure, fall back to a bounded wait
   or surface a terminal host error; then test timer-command failure and
   recovery. Evidence: the failure branch and following indefinite notification
   wait in `hosts/freertos/src/freertos_host.cpp`. Existing regression test:
   none.
3. **R3 — Resolve the driver-wrapper contract.** `[SPEC-E906-RTOS-08]` describes
   blocking wrappers that submit `ISpi`/`IUart`/`II2c` work and await ISR
   completion, but `freertos_driver_wrapper.hpp` currently supplies only a
   generic `TaskNotifier`; it does not implement those adapters. Either make
   that requirement explicitly future-facing, or implement and test the
   adapter behavior, including timeout/cancellation and notification ownership.
   Evidence: `[SPEC-E906-RTOS-08]` versus
   `apps/e906_freertos_app/include/freertos_driver_wrapper.hpp`. No
   adapter-specific regression test was found.
4. **R4 — Make benchmark failures and quantiles unambiguous.** The latest
   sweep completed with 17/42 failed rows (9 timeouts, 8 missing JSON); split
   transport/tool failures from firmware failures,
   preserve stderr/board state for timed-out rows, and allow partial evidence
   without treating a missing JSON result as a runtime measurement. Expand the
   lateness histogram or mark overflowed percentiles as censored. Repeat timer
   modes under idle and load before selecting a preferred mode. Evidence:
   timestamped board manifest and row JSON cited above; this remains an open
   finding until failure causes are understood and/or covered.
5. **R5 — Measure FreeRTOS stack headroom.** The task stacks are statically
   allocated (including an 8 KiB AbstractX task stack and a 2 KiB cyclic-task
   stack). Report each task's high-water mark under full benchmark/trace load
   and add a guard threshold before reducing any stack. This is a targeted
   opportunity to protect the E906's limited SRAM, not a recommendation to
   shrink stacks without measurement. Existing guard: static ELF size/layout
   only; task-level high-water reporting/test is absent.
6. **R6 — State the integration boundary plainly.** FreeRTOS provides
   preemption and priorities among its tasks; AbstractX's dispatcher remains
   cooperative and FIFO within its one task. Document which work belongs in
   AbstractX coroutines versus separate FreeRTOS tasks, the permitted
   notification/queue handoff, and priority/stack ownership. Do not describe
   the combination as inheriting FreeRTOS task scheduling for each coroutine.
   Existing contract: `[SPEC-ARCH-15]` and `[SPEC-E906-RTOS-05]`; no dedicated
   conformance test for this boundary was run.
7. **R7 — Close project-wide verification debt.** Install/run clang-tidy and
   cppcheck, add coverage, mutation and parser-fuzz stages, and reduce the 65
   known unbacked `@verifies` claims. Add the Zephyr/ChibiOS hosts only if those
   platforms remain part of the supported portability promise. Existing
   evidence: `spectrace_matrix.py --check-verifies` reports 63 baseline claims
   (65 total); no coverage/mutation/fuzz evidence was produced in this run.

## Manual checklist answers

### Independent review

Reviewed the FreeRTOS host and app against `[SPEC-ARCH-15]` and
`[SPEC-E906-RTOS-01..08]`, plus the prior platform and full-audit reviews.
Findings R1–R7 above are evidence-based recommendations; R2 remains a candidate
until fault injection. Prior open board checks for E906 SPI/UART/I2C and the
cross-target driver gaps are not closed by this audit.

### Concurrency

`boundary_review.py --check` passes with no reported store/load wake-fence
issues. The general TSan gate passes. This does not substitute for randomized
FreeRTOS wake/timer stress or model checking; neither was run. In particular,
the FreeRTOS host itself is not covered by a host-side scheduler test here.

### Buses and drivers

The automated scan reports 31 blocking/vendor calls, including E906 I2C
delays, the Linux host's 100 µs idle sleep, and Pico SDK blocking I2C/UART
calls. This review did not classify every call site. The previous
2026-11-06 bus review remains the detailed checklist; its E906 SPI FIFO polling
and UART RFL spin observations still need a board-backed disposition. Pico
CYW43 remains on Core 0 per its specification, but the cost of its blocking DMA
wait was not measured in this run.

### GUI

No GUI changes were in the reviewed scope. The 2026-11-06 review records
click-driven tests for the Studio windows; those were not rerun here.

### Board

The Nov. 11 full benchmark sweep provides real E906 data but is incomplete
(25/42 passed, 17 failed). Afterward the board remained running its original
`riscv-firmware.elf`; no RPMsg devices were announced. The E906 REPL board
self-test was therefore not run: its precondition requires the REPL firmware
and endpoints. Do not treat failed rows as passing measurements or use this
partial sweep to make loaded-latency claims.

### Platform comparison

- **FreeRTOS:** a single task calls `step()`, waits on a notification until a
  kernel-tick deadline, and is awakened by task/ISR notifications. FreeRTOS
  owns the E906 comparator/tick; configured sleep resolution is one tick
  (1 ms). This is an appropriate minimal host, but not an AbstractX
  preemptive-task scheduler.
- **Bare metal:** one loop owns `step()` and waits in the target idle path;
  AbstractX directly owns its timer. This is the lower-overhead option where
  preemption and OS services are not needed.
- **Linux POSIX / Asio:** host integrations exist; Asio is a pump/handler host,
  not a second coroutine runtime. The two Linux hosts share the platform but
  differ in event/timer integration.
- **Zephyr / ChibiOS:** design mappings only; no host implementations or
  conformance runs. Portability to them is not demonstrated.

FreeRTOS adds preemption, task isolation, kernel services and a familiar
integration point, while AbstractX adds statically pooled coroutine frames,
typed async composition, a single resume owner and CTF tracing. The tradeoff is
the extra kernel memory and tick-quantized sleeps on this E906 configuration.
The timing build uses about 132 KB SRAM_FAST versus about 113 KB for the
bare-metal benchmark image; stack high-water measurements are the next step
before deciding whether that difference can be reduced safely.

## Not checked

- No full set of 42 board benchmark rows passed; 17 failed or produced no
  result. The REPL self-test was not run because no RPMsg endpoints were
  announced by the board's original firmware.
- No FreeRTOS queue-full fault injection, host-side FreeRTOS runtime test, or
  randomized wake stress/model check.
- Clang-tidy ran and passed after enabling the host compile database. Cppcheck,
  coverage, mutation, and fuzz were not run (tool/targets missing).
- No ESP32-P4, Zephyr, ChibiOS, Zynq, RTL, FPGA-fit or GUI click run in this
  audit.
- CI status is from 2026-11-07 and does not cover `2af893b`.
