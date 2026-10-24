---
name: queue-concurrency
runs_on: host
verifies: "Shared multi-producer rings (telemetry, I/O requests, completions) use MpscRingBuffer / MpscTlpRing with InterruptLock, while SpscRingBuffer is restricted strictly to dedicated 1:1 hardware DMA streams ([SPEC-TLP-03])."
description: "Check skill. Verifies queue concurrency invariants ([SPEC-TLP-03]): multi-producer queues must use MpscRingBuffer / MpscTlpRing with InterruptLock, preventing SPSC data races under concurrent coroutine, worker, or ISR producers. Run when a requirement names this skill in its Verified by line or a change touches queue definitions."
---

# Skill: queue-concurrency

**What it verifies**: Shared multi-producer rings (telemetry, I/O requests, completions) use `MpscRingBuffer` / `MpscTlpRing` with `InterruptLock`, while `SpscRingBuffer` is restricted strictly to dedicated 1:1 hardware DMA streams ([SPEC-TLP-03]).

## How to run

```bash
python3 tools/check_queue_concurrency.py
```

## Pass criteria

`[QUEUE CONCURRENCY CHECK PASSED]` with exit code 0. No multi-producer ring uses `SpscRingBuffer` or `SpscTlpRing`.

## Evidence

Output of `tools/check_queue_concurrency.py` and accompanying ThreadSanitizer run (`build_tsan`).
