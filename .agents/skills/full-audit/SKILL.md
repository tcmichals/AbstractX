---
name: full-audit
runs_on: host
verifies: "The whole project, not one change ([SPEC-AUDIT-17]): the gate (plain / ASan+UBSan / TSan, build error stops it), traceability and contract audits, static analysis, coverage, mutation, fuzzing, E906 cross-build and layout, CI status, which OS hosts exist, plus reviewer checklists for concurrency, GUI behaviour, board claims and hosting on / competing with FreeRTOS, Zephyr, ChibiOS and Linux."
description: "Check skill. Full audit of AbstractX: runs tools/full_audit.py (every automatic stage, one report with PASS / FAIL / REPORT / SKIPPED / MANUAL per stage) and walks the manual checklists, including how AbstractX is hosted on and compares with FreeRTOS, Zephyr, ChibiOS and Linux (Boost.Asio). Use for a periodic or pre-release review, after a miss that no check caught, or when the user asks for an audit or a comparison with another RTOS. For one change, use spec-change-review and tools/gate.sh instead."
---

# Skill: full-audit

**What it verifies**: that every check the project has actually ran and passed, how strong the
tests are, and what no tool checks yet. It turns misses into checks: every confirmed finding ends
as a failing test, a lint rule or a recorded hardware fact ([SPEC-AUDIT-17]).

## How to run

```bash
python3 tools/full_audit.py --list          # stage ids
python3 tools/full_audit.py                 # everything (hours: three sanitizer builds + coverage)
python3 tools/full_audit.py --quick         # gate --quick + trace + static
python3 tools/full_audit.py --stages gate,trace,hosts --out docs/reviews/<date>-audit-stages.md
tools/gate.sh                               # the gate alone (AGENTS §10.2); --quick = plain only
```

Every stage ends as `PASS`, `FAIL`, `REPORT` (measured, no threshold yet), `SKIPPED` (did not run,
reason given) or `MANUAL`. A stage that did not run is never `PASS`. Exit 1 on any `FAIL`.

| Stage | What it runs | Notes |
| --- | --- | --- |
| `gate` | `tools/gate.sh` | build error stops before ctest (no stale binaries); TSan under `setarch -R` |
| `trace` | `audit_specs`, `spectrace_matrix --check-skills / --check-verifies`, `audit_coro_contracts` (incl. no Asio/Cobalt coroutines), `audit_boundaries`, `audit_interfaces`, `audit_hal_invariants`, `audit_ipc_invariants`, `boundary_review --check` | |
| `static` | `run_static_analysis.py` (shrink-only baseline); `cppcheck` advisory | |
| `coverage` | `--coverage` host build, ctest, `gcovr` over `include/ src/ hosts/ targets/linux/` | REPORT until a threshold is agreed |
| `mutation` | Mull | SKIPPED until Mull is installed |
| `fuzz` | targets in `fuzz/` (libFuzzer) | SKIPPED until targets exist (TLP, RPMsg, CTF parsers first) |
| `e906` | cross-build, `check_e906_layout.py`, `analyze_elf_size.sh` per image | SKIPPED without `~/.tools` RISC-V toolchain |
| `ci` | `gh run list` on `main` | SKIPPED when `gh` is not logged in; a red CI nobody reads is a finding |
| `hosts` | which OS hosts exist (`hosts/<os>`) | REPORT |

## Manual stages (answer each in the review document)

1. **review**: give reviewers (human or LLM) the code or diff and the requirements, *not* your
   suspicions, so they do not just confirm them. A finding stays a candidate until it is a failing
   test, a lint rule or a recorded hardware fact. Re-check the open items of the last review in
   `docs/reviews/`.
2. **concurrency**: `boundary-race-review` checklist for every ISR / cross-core / cross-thread
   boundary; a TSan stress test with random scheduling for each wake handshake; a model check
   (TLA+ or similar) for wake protocols is planned, not set up.
3. **gui**: tests that click (Dear ImGui Test Engine in `imgui_bundle`): tabs, Pop to Canvas / Pop
   In (one copy only, AGENTS §8), View menu, connect / disconnect. Rendering frames is not a test of
   behaviour.
4. **board**: every requirement verified on hardware has a board run on the commit it claims.
5. **platforms** (FreeRTOS, Zephyr, ChibiOS, Linux with Boost.Asio, bare metal). For each OS:

   | Question | Why |
   | --- | --- |
   | Native event loop and async model (FreeRTOS notifications / queues / software timers; Zephyr `k_poll`, `k_work`, `k_timer`; ChibiOS events, virtual timers, OSAL; Linux epoll / io_uring, Boost.Asio) | the host must sit on it, not beside it |
   | How `step()`'s deadline and the wake hooks map onto it (`[SPEC-ARCH-15]`), with no second loop | one owner of time |
   | Which of its coroutine or async runtimes must never mix with AbstractX coroutines | Boost.Asio awaitables were missed until 2026-11-04 |
   | Where driver completions enter (ISR, callback, completion handler) and who owns timers, the tick hook and interrupt priorities | the tick-hook and `mtimecmp` ownership issues |
   | Services: timeouts on every wait, multi-object waits, priorities / preemption, work queues, init order, power management, logging / tracing, latency measurement: AbstractX better / equivalent / gap / not needed (why) | where we compete, where we lag |
   | What an application changes to move to this OS (target: nothing) | the portability promise |

## Pass criteria

No `FAIL` stage; every `SKIPPED` stage has a reason and an owner in the review; every `MANUAL`
checklist is answered in `docs/reviews/<date>-*.md` with each finding ranked and linked to the test
or rule that now covers it.

## Evidence

The report written by `tools/full_audit.py` (quote its stage table in the commit message) and the
review document in `docs/reviews/`.
