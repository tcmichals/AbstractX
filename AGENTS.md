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
1. Public HAL interfaces (`ISpi`, `II2c`, `IUart`, `ITimer`) and device drivers MUST NOT expose synchronous blocking transfer methods (`transfer_sync`, `read_sync`, `write_sync`).
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
3. **Allwinner Cubie A5E (ARM Cortex-A55 Linux) & Desktop SITL**: Quad AArch64 @ 1.4 GHz, NEON FPU, PREEMPT_RT or coprocessor.

All platform-specific clock initialization, core allocation, and pinmux are handled autonomously inside `abstractx::init(config)`.

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

