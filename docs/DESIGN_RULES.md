# AbstractX Design Rules

This document outlines the hard engineering invariants for the AbstractX project. Any pull request or commit that violates these rules must be rejected to ensure the commercial viability and reliability of the IP core.

## 1. Testing and Verification
- **Test-bench Mandate:** Every SystemVerilog (`.sv`) module MUST have a corresponding isolated testbench.
- **Python-first Validation:** We use `cocotb` as our primary testing framework. 
- **Verilator Default:** All `cocotb` testbenches must support compilation and execution via Verilator (`SIM ?= verilator`).
- **No Mock Tie-Offs in Top-Level:** The top-level architectures (e.g., `asp_top.sv`) must not contain hardcoded mock tie-offs for critical datapath signals (such as `tlast`). Top-level wrappers must represent functional, deployable architectures.
- **Self-Documenting Builds:** All `CMakeLists.txt` configurations MUST provide an explicit, human-readable helper mapping (like `make info` or custom module comments) so that generated Makefile targets are entirely self-discoverable by developers.

## 2. Naming Conventions
- **Module Prefix:** All native AbstractX modules should be prefixed with `asp_` (AbstractX Switch Protocol) to avoid namespace collisions in host systems.
- **File Matching:** A module named `asp_foo` MUST reside in a file named `asp_foo.sv`.
- **Testbench Matching:** Testbenches must reside in the `sim/cocotb` directory and must be explicitly linked to their hardware module counterpart (e.g. `test_asp_foo_cocotb.py`).

## 3. Interfaces
- **AXIS Native:** Internal data paths should prefer AXI-Stream (`tdata`, `tvalid`, `tready`, `tlast`) structures for switching and routing.
- **Hardware Isolation:** Physical pin implementations (like SPI protocol decoding) must be isolated into "frontend" or "MAC" modules (e.g., `asp_spi_frontend.sv`). The core routing layers must only ingest byte streams/AXIS.

## 4. AXIS Test Methodology (The AbstractX Standard)
All AXI-Stream modules claiming AbstractX compatibility MUST pass a strict validation methodology in their Cocotb testbenches. Ad-hoc "happy path" testing is strictly rejected. Ensure the following protocol invariants are tested:
- **Random Backpressure Tolerance:** The testbench MUST randomly deassert `tready` (stall) during packet traversal to formally prove that `tdata`, `tvalid`, and `tlast` remain perfectly stable without data dropping or duplication.
- **Framing Integrity:** The testbench MUST computationally validate that `tlast` perfectly matches the boundary of the transaction without slipping cycles or firing early.
- **AXIS Rule Compliance:** Testbenches must verify that modules do not create combinatorial loops (e.g., `tvalid` waiting on `tready`, or `tready` directly driving incoming `tvalid`).

## 5. IP Boundaries
- **Dual-License Rules:** You must not copy/paste aggressively licensed open source code into `asp_` components without verifying compliance with our Dual-License (Open Source + Commercial) model.

## 6. C++20 Freestanding Header Rules
All headers under `include/` MUST remain freestanding-compatible for MCU targets:
- **Prohibited includes:** `<mutex>`, `<thread>`, `<condition_variable>`, `<iostream>`, `<vector>`, `<list>`, `<map>`, `<queue>`.
- **Permitted includes:** `<coroutine>`, `<atomic>`, `<array>`, `<optional>`, `<variant>`, `<tuple>`, `<cstdint>`, `<cstddef>`, `<utility>`.
- **Dynamic allocation:** `operator new` / `malloc` are only permitted inside static pool overrides (`operator new(size_t)` returning from a fixed pool). Direct heap use in core headers is REJECTED.

## 7. Execution Domain and Queue Safety Rules
- **Canonical abstraction:** The public dispatcher abstraction is `DomainDispatcher`. The historical `IsrDispatcher` name remains only as a compatibility alias; it does not redefine the design boundary.
- **Coroutine domain vs ISR domain:** AbstractX distinguishes the cooperative coroutine domain from the interrupt-driven ISR domain. These are separate execution domains with different safety requirements.
- **Topology vs. Safety are different:** SPSC only describes producer/consumer topology; it does not imply ISR safety.
- **ISR-safe queues are required for any queue touched by an ISR or any code that can be interrupted by a higher-priority IRQ.**
- **ETL pattern requirement:** Any queue that may be written from an ISR must provide interrupt save/restore semantics and must protect the critical section against nested higher-priority interrupts. The canonical implementation is `etl::queue_spsc_isr<T, N, abstractx::InterruptLock>`; do not hand-roll a replacement ring.
- **Domain direction is explicit in the call:** the coroutine domain uses the locked entry points (`push()` / `pop()`), and the ISR domain uses the unlocked ones (`push_from_isr()` / `pop_from_isr()`), because interrupts are already masked there.
- **Interrupt API contract:** The access policy MUST expose the ETL `lock()` / `unlock()` pair, saving the prior interrupt state on the outermost lock and restoring the exact previous state on the matching unlock, so nesting never re-enables interrupts early. `abstractx::disable_interrupts_save_flags()` / `restore_interrupts_flags(uint32_t flags)` provide the underlying primitives.
- **Priority-aware design:** If multiple IRQ sources with different priorities can enqueue into the same queue, the implementation MUST either:
  - use a separate queue per priority domain, or
  - mask interrupts / save flags while modifying the ring, restoring the prior interrupt state after the enqueue/dequeue.
- **Driver request queues:** Top-level coroutines and main-loop code may enqueue requests to a driver, but the driver-facing queue must be ISR-safe because the driver can be called by hardware ISR activity and must support multiple queued requests in flight.
- **Core-to-core and thread-to-thread follow the same bridge pattern:** the same queue/bridge design can be reused across core-to-core and thread-to-thread boundaries, but the implementation details are platform-specific (shared-memory queues, processor-local queues, or RTOS message queues).
- **Ring sizing:** Minimum ring capacity: `max_in_flight_transactions * 2` slots (never less than 8).
- **Power-of-two capacity:** Capacity MUST be a power of 2 (enforced by `static_assert` in `SpscRingBuffer`).
- **Channel arrays:** Channel arrays MUST have one dedicated ring per producer (one per ISR / worker thread / processor core).

## 8. Endianness Convention
- **ASP wire protocol:** Big-Endian for all multi-byte header fields.
- **C/C++ structs in `include/`:** Native endianness (host byte order).
- **Requirement:** Any code that serializes a struct to the wire or deserializes from wire bytes MUST include explicit byte-swap operations or a compile-time `static_assert` confirming the target is Big-Endian.
- **Forbidden:** Relying on `memcpy` of native structs into SPI TX buffers without a byte-swap layer.
