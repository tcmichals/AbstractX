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
# Run full cross-language scan (C++, Python, SystemVerilog)
python3 tools/check_queue_concurrency.py

# Display architectural SPSC vs MPSC vs MPMC concurrency guide
python3 tools/check_queue_concurrency.py --usage

# Run language-specific audit
python3 tools/check_queue_concurrency.py --lang cpp
python3 tools/check_queue_concurrency.py --lang py
python3 tools/check_queue_concurrency.py --lang sv
```

## Pass criteria

`[QUEUE CONCURRENCY CHECK PASSED]` with exit code 0.
- C++: Shared queues must use `MpscRingBuffer` / `MpscTlpRing` with `InterruptLock`; `SpscRingBuffer` restricted to 1:1 hardware streams; MPMC is strictly prohibited.
- Python: Multi-threaded queues must use thread-safe `queue.Queue` or `threading.Lock`.
- SystemVerilog: Shared egress sinks must route through `asp_router.sv` / `asp_rr_arbiter.sv` without un-arbitrated multi-writer collisions.

## Evidence

Output of `tools/check_queue_concurrency.py` and accompanying ThreadSanitizer run (`build_tsan`).
