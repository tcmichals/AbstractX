---
name: ipc-transport-audit
runs_on: host
verifies: "Inter-processor transport invariants ([SPEC-E906-IPC-02], [SPEC-E906-IPC-03], [SPEC-E906-IPC-04], [SPEC-E906-CORO-01], [SPEC-E906-REPL-05]): bounded hardware waits, host-writable fields read once, no address passthrough, ring errors never stall, ISR-shared state atomic, no 0-byte give-back, saved errno in awaiters, correct reply addressing, non-blocking Python I/O that survives EAGAIN, 32-bit UIO MMIO, and tests that exercise what they claim."
description: "Check skill. Static audit of RPMsg / message box / UIO / rpmsg_char transports (E906 C++, Linux awaiters, Linux Python bridges and UIO tools) for the bug classes the 2026-10-26 review found and every other check passed: unbounded spins, TOCTOU on shared memory, unchecked host addresses, permanent ring stalls, non-atomic ISR handoff, swapped reply addresses, stale errno, EAGAIN that kills a loop, memcpy-width MMIO, and @verifies claims with no test behind them. Run when touching rpmsg*, msgbox*, rpmsg_console*, reactor/async_*, e906_rpmsg_bridge.py, e906_link.py or the Zynq UIO tools."
---

# Skill: ipc-transport-audit

**What it verifies**: the transport between two processors (E906 ↔ Linux over RPMsg, Linux ↔ FPGA over
UIO) treats the other side as **untrusted and possibly dead**. A transport bug almost never shows up in a
happy-path test, because the fakes always recover. This skill exists because the 2026-10-26 review found
11 such bugs in code that passed `audit_specs.py`, `audit_hal_invariants.py`, the host tests and the
sanitizers.

## How to run

```bash
python3 tools/audit_ipc_invariants.py                 # working tree
python3 tools/audit_ipc_invariants.py --git-rev <rev> # e.g. show a check fails on the old code
```

## What each check catches (static)

| Check | Bug it would have caught (2026-10-26) |
| :--- | :--- |
| `IPC-WAIT` | `notify_host()` / `reply()` spun forever when Linux stopped draining |
| `IPC-REREAD` | `hdr->len` / `desc->len` read twice: the length used was never the one checked |
| `IPC-PASSTHRU` | `translate_da()` passed unknown host addresses through (host could write firmware SRAM) |
| `IPC-STALL` | one bad descriptor index stopped RX for good (return without consuming) |
| `IPC-ISRSHARE` | plain `std::coroutine_handle<>` shared with the msgbox ISR |
| `IPC-SLOT` | a second awaiter silently overwrote the first waiter's handle |
| `IPC-ZEROLEN` | 0-byte commits to "return" a buffer (malformed NS message on Linux) |
| `IPC-ERRNO` | `await_resume` tested `errno` long after the syscall |
| `IPC-REPLYADDR` | REPL console put its own endpoint in `.src`; `reply()` swapped it (hidden by src==dst on Linux) |
| `PY-NBBREAK` | lease-renewal loop quit for good on one `BlockingIOError` (trace stopped after 3 s) |
| `PY-NBSPIN` | `_write_all` retried forever when the E906 was dead |
| `PY-MMIO` | UIO registers read and written through mmap slices / `struct.*_from` (access width not guaranteed) |
| `PY-UIO0` | hardcoded `/dev/uio0` |
| `T-VERIFIES` | a test said `@verifies [SPEC-E906-CORO-01]` but never ran the awaiter or the ISR |
| `T-MAP32` | `MAP_32BIT` without a guard: the sim tests did not build on AArch64 hosts |

A deliberate exception is marked on the line, or the line before, with
`ipc-audit: allow <CHECK> (<reason>)`.

## Review checklist (what a regex cannot see; answer each in the review)

1. **Adversarial fakes.** For every wait on the peer, is there a test whose fake *never* recovers
   (no buffer returned, FIFO always full, index always torn)? It must finish and assert the bounded
   result. A fake that recovers after N tries only proves the happy path.
2. **Recovery after error.** For every error counter, does a test go on to the *next* message and assert
   that it is delivered? A test that stops at "error count == 1" hides a permanent stall.
3. **Untrusted fields.** List every field the peer can write (`desc.addr/len`, `hdr.len/src/dst`, ring
   indices, UIO registers). Is each read once, bounded, and checked against a window?
4. **Address direction.** For every reply or send, write down which side's address goes in src and
   which in dst, and test it with a peer address that is **not** equal to ours.
5. **Shared ownership.** For every flag or handle touched by an ISR and the main loop (or by two
   consumers), name its single owner, and check that it is atomic.
6. **Steady state.** Is there a ≥1000-cycle test with the peer recycling buffers, asserting no
   growth or leak (AGENTS §10.5)?
7. **Register map parity.** Every UIO/MMIO offset used from Python or C++ appears in the RTL decode of the
   block that is actually instantiated in the top level, not just in a spec table.

## Pass criteria

`[PASS] IPC transport invariants hold in working tree.` with exit code 0, plus the checklist answered in
the review or the commit message.

## Evidence

Output of `tools/audit_ipc_invariants.py`, and of the same command with `--git-rev` on the pre-fix
revision when a check is added or changed (it must fail there).
