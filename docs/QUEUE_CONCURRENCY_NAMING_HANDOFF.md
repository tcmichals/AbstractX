# AbstractX Handoff: Queue Concurrency Invariant & Semantic Naming Alignment

> **Originating Session**: `tcmichals/inav` (INAV-AbstractX Flight Engine)  
> **Target Session**: `tcmichals/AbstractX`  
> **Date**: 2026-10-25  
> **Reference Rule**: `[SPEC-TLP-03]` in `docs/DESIGN_SPECIFICATION.md` & `tools/check_queue_concurrency.py`

---

## 1. Problem Statement & User Directive

In `inav-abstractx`, during review of the RP2350 (Pico 2 W) dual-core and Allwinner A5E (E906) co-processor architectures, a critical naming and concurrency hazard was identified:

> **User Observation**:
> *"are you sure it can be a spsc? because on the pico 2 w you have to loops, the local processor and a remote processor putting stuff in to the queue? I think calling it a spsc queue is not the best name and would be better name so it is not so misleading"*

### The Hazard:
1. **Misleading Naming**: Calling a shared multi-producer telemetry sink (e.g. `g_telemetry_ring`) an "SPSC queue" misleads developers and AI coding agents into believing it can safely take concurrent writes from multiple cores (Core 0 I/O + Core 1 PIO DMA) or ISRs.
2. **Atomic Race Condition**: In an SPSC queue (`SpscTlpRing`), multiple concurrent callers of `push()` race on `head_.load()` and `head_.store()`, resulting in torn writes, dropped 64-byte TLP frames, and corrupted queue indices.

---

## 2. Architectural Standardization

To eliminate this confusion across all AbstractX targets, we are standardizing on three explicit, unambiguous queue abstractions:

### A. Point-to-Point Dedicated Channel: `DedicatedSpscChannel`
- **Topology**: Strictly **1 Producer : 1 Consumer**.
- **Implementation**: `SpscRingBuffer<T, Cap>` / `DedicatedSpscChannel<Cap>`.
- **Use Case**: 1:1 dedicated hardware DMA pipelines (e.g. PIO DMA $\rightarrow$ Core 1, or unidirectional inter-core mailboxes like E906 $\rightarrow$ Linux SRAM C).
- **Rule**: NEVER shared among multiple writers.

### B. Array of Dedicated Channels: `TlpChannelMultiplexer` (`SpscChannelArray`)
- **Topology**: **N Private Channels $\rightarrow$ 1 Consumer**.
- **Implementation**: `SpscChannelArray<T, NumChannels, Cap>` (defined in `include/tlp_ring.hpp`) or `TlpChannelMultiplexer<N, Cap>`.
- **Use Case**: Multiple distinct producers (Core 0 I/O, PIO DMA, I2C coroutines) each have a private channel index `[0..N-1]`.
- **Consumer**: Single consumer drains channels round-robin or via priority with **zero atomic write contention**.

### C. Multi-Producer Shared Queue: `MpscEventQueue` (`MpscTlpRing`)
- **Topology**: **N Producers $\rightarrow$ 1 Consumer**.
- **Implementation**: `MpscRingBuffer<T, Cap>` / `MpscTlpRing<Cap>`.
- **Use Case**: Shared event sinks where multiple threads, coroutines, or hardware ISRs must push to a single queue.
- **Synchronization**: Uses nesting-aware `InterruptLock` on bare-metal, RP2350 hardware spinlocks, or atomic ticket reservations.

---

## 3. Tasks for the AbstractX Ag/Agy Session

1. **Verify and Run `tools/check_queue_concurrency.py`**:
   - Confirm that no shared multi-producer queues are typed as raw `SpscTlpRing`.
2. **Semantic Aliases & Clean Naming in `include/tlp_ring.hpp`**:
   - Provide explicit alias: `template <size_t Cap = 64> using DedicatedSpscChannel = SpscTlpRing<Cap>;`
   - Ensure `TlpChannelMultiplexer` is exported as a first-class abstraction alongside `SpscChannelArray`.
   - Update comments and docstrings in `include/tlp_ring.hpp` and `include/asp_coro.hpp` warning against multi-producer SPSC usage.
3. **Pico 2 W (RP2350) Asymmetric Dual-Core Alignment**:
   - Ensure the RP2350 target definitions support Core 0 (I/O Processor) and Core 1 (RT Flight Core) partitioning using dedicated SPSC channel pairs or `MpscTlpRing` with RP2350 hardware spinlock 31.
4. **Log Chronicle in `AbstractX/engineering_log.md`**:
   - Record the naming refinement and concurrency contract update in accordance with `[SPEC-TLP-03]`.
