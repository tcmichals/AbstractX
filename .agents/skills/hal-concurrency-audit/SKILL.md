---
name: hal-concurrency-audit
runs_on: host
verifies: "Low-level hardware abstraction layer and bare-metal concurrency invariants ([SPEC-E906-HAL-01], [SPEC-E906-IRQ-02], [SPEC-E906-IRQ-03]): safe WFI interrupt masking, calibrated hardware timer timeouts, race-free cross-domain register writes, and address-validated REPL access."
description: "Check skill. Verifies low-level HAL concurrency contracts: prevents bare WFI check-then-sleep races, eliminates arbitrary NOP spinloops in favor of Timer::get_ticks() deadlines, prohibits 32-bit RMW on shared cross-core registers, and enforces address validation on REPL peek/poke. Run when a requirement names this skill in its Verified by line or when modifying target HAL drivers."
---

# Skill: hal-concurrency-audit

**What it verifies**: Low-level hardware abstraction layer and bare-metal concurrency invariants ([SPEC-E906-HAL-01], [SPEC-E906-IRQ-02], [SPEC-E906-IRQ-03]):
- WFI Sleep-Check Race: `wfi` loops must mask interrupts (`disable_interrupts_save_flags()` or `InterruptGuard`) prior to testing the loop condition, preventing lost wakeups if an interrupt fires between condition check and `wfi`.
- Calibrated Timeouts: Peripheral drivers must not busy-spin on arbitrary NOP loops (`for (int timeout = 10000; ...)`), but must use calibrated hardware timer deadlines (`Timer::get_ticks()`).
- Cross-Domain Register Writes: Registers shared across SMP/AMP cores (e.g., `DMA_IRQ_CPU_EN_REG`) must not use 32-bit read-modify-write which clobbers concurrent OS state (e.g., Linux DMA channels).
- Safe REPL Access: `mod_abstractx` peek/poke memory inspection must enforce 4-byte alignment and validate SRAM/peripheral physical address bounds.

## How to run

```bash
python3 tools/audit_hal_invariants.py
```

## Pass criteria

`[PASS] 100% HAL Concurrency Invariant Parity: Safe WFI, calibrated timeouts, race-free DMA, safe peek/poke.` with exit code 0.

## Evidence

Output of `tools/audit_hal_invariants.py`.
