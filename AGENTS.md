# AbstractX AI Agent Guidelines & Architecture Invariants

This file defines the mandatory engineering rules and architectural invariants for all AI coding agents working on the AbstractX repository. **These rules are non-negotiable and strictly enforced.**

---

## 1. Core Rule: Markdown Drives the Code (SSOT)
1. **Single Source of Truth**: All architectures, hardware memory maps, peripheral contracts, and multi-rate dataflows are specified in Markdown first (`SPECIFICATION.md`).
2. **Read Before Writing**: Before modifying or creating code in `apps/`, `targets/`, or `include/`, you MUST read the corresponding `SPECIFICATION.md` document.
3. **Create Spec First**: When creating a new application or driver, you MUST generate its `SPECIFICATION.md` first (using `python3 tools/create_app_spec.py`) before writing any C++ implementation.
4. **Traceability Mandate**: Every requirement in a specification must have a unique tag (`[SPEC-*]`), and the implementing C++ code must carry a matching `// @impl [SPEC-*] <file_path>` tag. Validate with `python3 tools/audit_specs.py`.

---

## 2. Freestanding C++20 Invariants (Zero Heap & No STL Containers)
All code under `include/`, `apps/`, and `targets/` must remain freestanding-compatible for bare-metal dual-core MCUs:
* **Strictly Prohibited Headers & Calls**:
  - `<iostream>`, `<mutex>`, `<thread>`, `<condition_variable>`, `<vector>`, `<list>`, `<map>`, `<queue>`, `<string>`.
  - `operator new`, `operator delete`, `malloc()`, `free()`.
* **Permitted Primitives**:
  - `<coroutine>`, `<atomic>`, `<array>`, `<optional>`, `<variant>`, `<tuple>`, `<cstdint>`, `<cstddef>`, `<utility>`, `<cmath>`.
  - `etl::*` embedded template library containers (e.g. `etl::queue_spsc_isr`).
  - Statically pooled allocations (`SpscRingBuffer`, `AsyncQueue`, `SpscTlpRing`).

### Zero-Copy Data Plane `[SPEC-ARCH-08]`
* **Top-level rule: no memory copies** between a data producer and its transport. The producer borrows the transport's buffer (`acquire`), fills it in place, and hands the same buffer back (`commit`).
* **How is architecture-specific** and lives in each target's `SPECIFICATION.md` (E906: RPMsg buffers in SRAM; Pico 2 W / ESP32-P4: lwIP `PBUF_POOL` pbufs; Linux: static pool, kernel copy allowed). Write that per-target mapping *before* implementing, and do not add a `memcpy` or copy loop to a data path to make something work.

---

## 3. Execution Model: Simple Event Loop, Event-Driven Drivers & Hardware X-Fabric
The AbstractX execution model is grounded in two software basics and a symmetrical hardware X-fabric:
1. **Simple Poll Event Loop (`abstractx::step()`)**:
   - The cooperative execution context that continuously processes events and pumps the dispatcher.
   - **Sole context that calls `.resume()`.**
   - **NEVER** executes blocking I/O calls (`delay_ms`, `ioctl`, polling spinloops). Yields cooperatively via `co_await`.
2. **Completely Event-Driven Driver API (Zero Blocking, ISR / DMA)**:
   - Drivers dispatch transactions directly to hardware (DMA, ISR, PIO, DIO pin triggers) and yield execution.
   - **NEVER calls `.resume()` directly from ISRs or DMA workers.** Communicates exclusively by pushing completions/packets into lock-free SPSC rings and signaling doorbells.
3. **Symmetrical Hardware FPGA X-Fabric**:
   - Synthesizable full-crossbar switch fabric (`asp_router.sv`) routing 64-byte TLPs to and from the bus, with hardware DMA and ISRs on physical DIO lines.

---

## 4. Zero Synchronous Bus I/O Mandate
1. Public HAL interfaces (`ISpi`, `II2c`, `IUart`, `ITimer`) and device drivers MUST NOT add new synchronous blocking transfer methods. The existing `transfer_sync` / `transfer_byte` / driver `init()` fallbacks are tolerated **only for pre-scheduler bring-up** and MUST NOT be called from a coroutine or from `abstractx::step()`. Do not extend them; prefer removing them.
2. Driver lifecycles and register transactions MUST be asynchronous C++20 coroutines returning `coro::Task<bool>`:
   - `co_await dev.init_async()`
   - `co_await dev.read_reg_async(reg)`
   - `co_await dev.write_reg_async(reg, val)`
   - `co_await dev.next_sample_async()`

---

## 5. Multi-Rate Sensor Ingestion: The Primary-Paced Channel Pattern
When building applications that ingest multiple sensors running at different rates (e.g. 8 kHz IMU, 50 Hz Magnetometer, 10 Hz GPS):
* **BANNED**: Do NOT write fragmented state machines with tick counters or modulus prescalers (`if (++counter % 80 == 0)`).
* **MANDATED**: Use the **Primary-Paced Coroutine Channel Pattern**:
  1. Each sensor driver has a dedicated coroutine producer streaming into an `AsyncQueue<SampleType, N>`.
  2. The primary high-rate sensor (e.g. IMU DRDY) acts as the physical clock: the fusion loop awaits it with `co_await g_imu_channel.pop()`.
  3. Auxiliary lower-rate sensors (Mag, GPS, Baro) are drained non-blockingly on each tick using `while (g_mag_channel.try_pop(mag))`.
  4. The control flow is **100% linear**, readable, and free of race conditions.

---

## 6. Multi-Target Silicon Portability (Zero Application `#ifdef`s)
Applications written against `abstractx::` MUST compile and run identically across all supported platforms without target preprocessor conditionals:
1. **Raspberry Pi Pico 2 W**: Dual ARM Cortex-M33 @ 150 MHz, hardware single-precision FPU, Core 1 coroutine engine, Core 0 PIO/DMA + Wi-Fi.
2. **Espressif ESP32-P4**: Dual RISC-V RV32IMAFDC @ 400 MHz, hardware FPU, Core 1 coroutines, Core 0 GDMA + Wi-Fi 6.
3. **Allwinner Cubie A5E (Pure Silicon / No FPGA)**: Quad AArch64 @ 1.4 GHz Linux + XuanTie E906 RISC-V coprocessor, shared SRAM rings + `sun6i-msgbox`, driving on-chip SPI0, TWI0, UART2.
4. **Allwinner Cubie A5E + FPGA (Hardware X-Fabric)**: Quad AArch64 @ 1.4 GHz + XuanTie E906 + Gowin/Zynq FPGA AXI-Stream crossbar switch fabric (`asp_router.sv`).
5. **Desktop SITL**: Native Linux x86_64 / AArch64 under GDB/ASan with software loopback.

All platform-specific clock initialization, core allocation, and pinmux are handled autonomously inside `abstractx::init(config)`.

### Toolchain Standardization: `~/.tools`, xPack GNU GCC & newlib-nano
All bare-metal cross-compilation environments in AbstractX MUST adhere to the standardized toolchain layout:
1. **Toolchain Location Standard (`~/.tools/`)**:
   - ARM Cortex-M: `~/.tools/xpack-arm-none-eabi-gcc-*/bin/arm-none-eabi-`
   - RISC-V Bare-metal: `~/.tools/xpack-riscv-none-elf-gcc-*/bin/riscv-none-elf-`
   - Xtensa Bare-metal / DSP: `~/.tools/xtensa-esp-elf/bin/xtensa-esp32-elf-` / `xtensa-esp-elf-` / `~/.tools/xtensa-hifi4-gcc/`
   - Raspberry Pi Pico SDK: `~/.tools/pico/pico-sdk` (or `~/.tools/pico-sdk`)
   - FPGA Tools: `~/.tools/oss-cad-suite/bin`
2. **Nano Libc Mandate (`--specs=nano.specs`, `--specs=nosys.specs`)**:
   - All bare-metal ARM and RISC-V targets MUST link with `newlib-nano` (`--specs=nano.specs`) to eliminate bloated standard library I/O, heavy string formatting, and dynamic heap stubs.
   - Combined with `-fno-exceptions`, `-fno-rtti`, `-nostartfiles`, and freestanding C++20, this guarantees sub-16 KB binary footprints and 0 dynamic heap allocations.

---

## 7. Telemetry & CTF 1.8 Dynamic Schema Compliance
* Real-time flight loops MUST NOT perform string formatting or synchronous socket writes.
* Fused state and sensor packets are emitted as 64-byte `Tlp64` binary frames into `g_telemetry_ring`.
* Every telemetry packet must map to the dynamic barectf/CTF 1.8 schema (`trace_schema.json` and `trace/barectf_config.yaml`).
* Visualizers and GUI tools (`flight_display.py`, `abstractx_studio.py`) load the schema dynamically to decode fields, units, and widgets without hardcoded offsets.

---

## 8. GUI Architecture Invariants: Studio Workbench vs. USER Floating Canvas
The AbstractX Visualizer architecture (`abstractx_studio.py` and `flight_display.py`) enforces a strict, permanent separation of concerns between Application Domain and Silicon/Firmware Observability:

### Invariant 1: The USER Window is Strictly a Single Floating Canvas
1. **Dedicated User Domain**: The USER window contains exclusively application-domain instruments (PFD artificial horizon, 3D attitude wireframe, motor demands, altitude, ground speed, GPS tracking, and IMU scope).
2. **Zero Firmware Diagnostic Bleed**: The USER window MUST NOT embed low-level firmware plumbing, TLP packet inspectors, system event logs, or coroutine watchdogs.
3. **Decoupled Floating Canvas**: The USER window is an independent floating canvas window that can be freely dragged, resized, layered on top of the Studio workbench, or moved to a second monitor.

### Invariant 2: AbstractX Studio is the Unified Engineering Workbench
1. **Unified Observability Foundation**: AbstractX Studio occupies the main desktop workbench, housing all silicon, C++20 coroutine, SPU FPGA switch fabric, and transport diagnostics.
2. **Tab-First by Default**: All diagnostic surfaces exist as standard tabs in `CoreStudioTabBar` (Coroutine Inspector, CPU Gauges & Topology, TLP Bus Debugger, System Event Log, Flow Integrity, Simple Trace, Timeline, Source Code & RTL, FPGA Peripherals, MemBrowse CI).
3. **Pop-to-Canvas Movable Windows**: Any diagnostic tab can be popped out onto the desktop canvas via `🗖 Pop to Canvas` to allow simultaneous multi-window comparison (e.g., inspecting coroutine suspensions alongside TLP packet hex streams and CPU load).
4. **Anti-Clutter Single-Source State**: When a tool is popped out to the canvas, its Studio tab displays a placeholder pointing to the active floating window with a 1-click `🗗 Pop In (Dock to Studio)` button—preventing duplicate rendering, desync, or screen confusion.

---

## 9. SpecTrace: Anti-Drift Adversarial Review & Autonomous Learning Gate
AbstractX implements **SpecTrace** ([`docs/SPECTRACE.md`](docs/SPECTRACE.md)), an autonomous closed-loop architecture connecting aerospace requirement traceability with AI reflexive learning.

Whenever fixing a bug, addressing an algorithmic refinement, resolving a test failure, or implementing an architectural refactor:
1. **Engineering Log Chronicle (`engineering_log.md`)**:
   - Record the mistake / bug, root cause, and lesson learned:
     `python3 tools/log_mistake.py --title "<Title>" --target "<Subsystem>" --spec "<SpecFile>" --tag "<Tag>"`
2. **SpecTrace Anti-Drift Adversarial Audit**:
   - Evaluate proposed changes against the 6 adversarial stages across both Markdown Specifications and Code:
     - Stage 0: Specification Markdown & SSOT Anti-Drift (Requirement tags `[SPEC-*]`, Mermaid diagrams, zero-heap & async mandates, spec-to-code parity)
     - Stage 1: Freestanding C++20 & Hardirq Concurrency (No dynamic allocations, forbidden STL headers, or ISR resumes)
     - Stage 2: Non-Blocking HAL & Coroutine Lifecycle (No synchronous delays/spins, symmetric cancellation)
     - Stage 3: Subsystem Contracts (Primary-Paced Channel Pattern, zero modulus prescalers)
     - Stage 4: Hardware Interconnect, 64B TLP Framing & Endianness (Static assert 64B, posted writes flush)
     - Stage 5: Adversarial Gatekeeper & Anti-Drift Directives (Deduplication, severity ranking, and generating prompt directives to fix `SPECIFICATION.md`)
3. **Spec Markdown Drives Code (SSOT First)**:
   - Use the adversarial review findings to **update the Markdown specification (`SPECIFICATION.md`) FIRST** with explicit `[SPEC-*]` requirements and diagrams so the mistake can never repeat.
   - NEVER modify C++ or RTL code until the spec reflects the design/bugfix decision.
4. **Traceable Code Implementation (Grand Traceability)**:
   - Implement the freestanding C++20 or RTL changes, tagging every modified block with `// @impl [SPEC-*] <file_path>` so algorithms can be mathematically traced and validated against the spec.
5. **Verification Gate**:
   - Run `python3 tools/audit_specs.py` to confirm 100% spec-to-code traceability.
   - Run the canonical review entry point `python3 tools/spectrace.py --review` (`--module <module>` for changed work; `--base-ref origin/main` for branch diffs; `--all-modules` only for an intentional full maintained-module sweep). Linux host code is excluded unless explicitly selected, and generated/vendor output is pruned.
   - Run `ctest --test-dir build` / Cocotb regression suites (100% pass rate).

---

## 10. Behavioural Verification Gates (Traceability Is Not Correctness)
`audit_specs.py` only proves that tags exist. It found nothing wrong while four runtime bugs were present (see `engineering_log.md`, 2026-10-13). The following gates are mandatory in addition to Section 9:

1. **No unverified claims.** Never write "100% passing", "N/N tests", "verified", "formally verified", cycle counts, byte counts or footprint numbers into any Markdown unless the same change ran the command that produced them. Quote the command and its output in the commit message. If you did not run it, say "not verified".
2. **Run the real suite, both ways.** Before any commit touching `include/`, `src/`, `targets/linux/`, `tests/` or `sim/`:
   ```bash
   cmake -S . -B build_host -DABSTRACTX_TARGET=host && cmake --build build_host -j && ctest --test-dir build_host
   cmake -S . -B build_asan -DABSTRACTX_TARGET=host -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
         -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" && cmake --build build_asan -j && ctest --test-dir build_asan
   ```
   A sanitizer report is a failing test, even if the binary exits 0.
3. **Lifetime review for every span/pointer handed across a boundary.** Any `std::span`, raw pointer or reference stored in a request that is completed later (DMA, ISR, worker thread, other core) MUST point at storage that outlives the completion: a member buffer or the awaiting coroutine frame, never a loop-local or a temporary. State the owner in a comment.
4. **Awaiter contract.** Every awaiter must (a) receive *its own* completion via `request.result_slot` (never pop "the next" result from a shared queue), (b) return `false` from `await_suspend` on submit failure instead of resuming inline, and (c) bind spans to its own members inside `await_suspend`, not the constructor.
5. **Steady-state tests.** Any change to coroutine, allocator, queue or driver code needs a test that runs the path at least 1000 times and asserts that `coro::coro_pool_high_water()`, queue depth and `Dispatcher::dropped_resumes()` do not grow. One-shot tests do not catch leaks.
6. **Tests drive coroutines like production does.** Unit tests must start a task once and then pump `Dispatcher::process()` (as `abstractx::step()` does). Calling `.resume()` on an outer task that is awaiting a child is undefined behaviour and is forbidden.
7. **Rules must match the code.** If a rule in this file contradicts existing code, fix one of them in the same change and record which in `engineering_log.md`. Never leave a known contradiction in place.
8. **Report what you did not check.** End every agent summary with a short list of what was *not* built or tested (e.g. cross-compiled targets, RTL, hardware).

