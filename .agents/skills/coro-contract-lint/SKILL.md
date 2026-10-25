---
name: coro-contract-lint
runs_on: host
verifies: "C++20 coroutine contracts and concurrency invariants ([SPEC-ARCH-10], [SPEC-ARCH-11]): zero direct resumes outside Dispatcher/step, atomic state in race combinators, and RAII destructors for intrusive awaiters."
description: "Check skill. Verifies C++20 coroutine contracts: no direct .resume() calls inside awaiters or combinators, multi-task combinators use std::atomic state counters, and intrusive awaiters unlink cleanly upon coroutine cancellation. Run when a requirement names this skill in its Verified by line or when modifying coroutine primitives."
---

# Skill: coro-contract-lint

**What it verifies**: C++20 coroutine contracts and concurrency invariants ([SPEC-ARCH-10], [SPEC-ARCH-11]):
- All coroutine resumptions must be enqueued via `abstractx::Dispatcher::push()` or `abstractx::IsrDispatcher::post()`. Only `abstractx::step()` (the event loop) or unit test runners may call `.resume()`.
- Race combinators (`WhenAllAwaiter`, `WhenAnyAwaiter`) must use `std::atomic` counters (`remaining_`, `ready_index_`) to guard against multi-core or ISR completion races.
- Intrusive awaiters (inheriting from `etl::bidirectional_link` or holding waiter nodes) must implement RAII destructors to unlink their nodes upon early coroutine destruction/cancellation.

## How to run

```bash
python3 tools/audit_coro_contracts.py
```

## Pass criteria

`[PASS] 100% Coroutine Contract Parity: Zero direct resumes, atomics in combinators, safe awaiter destructors.` with exit code 0.

## Evidence

Output of `tools/audit_coro_contracts.py` and accompanying ThreadSanitizer run (`build_tsan`).
