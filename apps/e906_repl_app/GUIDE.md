# AbstractX XuanTie E906 MicroPython REPL Application Specification — Developer Guide

> **SSOT Notice**: This documentation is automatically generated from the authoritative specification [`SPECIFICATION.md`](SPECIFICATION.md). Do not edit manually.

## 1. Architectural Overview

This guide provides operational and implementation details for `AbstractX XuanTie E906 MicroPython REPL Application Specification`. Total governing requirements: **12**.

## 2. System Architecture Diagrams

### Diagram 1: Dataflow & Architecture
```mermaid
flowchart TD
    subgraph SILICON_A5E["Allwinner Radxa Cubie A5E Silicon"]
        subgraph E906_MCU["XuanTie E906 RISC-V Core @ 800 MHz (SRAM Space 0: 0x3FFC0000)"]
            DISPATCHER["<b>AbstractX Work Queue (Dispatcher)</b><br/>Lock-Free etl::queue_spsc_isr<br/>Cooperative Coroutine Resumption"]
            
            subgraph COROUTINES["Cooperative C++20 Tasks"]
                CORO_REPL["<b>REPL Task</b><br/>co_await line_async()"]
                CORO_SENS["<b>Sensor Ingress Task</b><br/>UART3 & TWI1 Driver"]
                CORO_EGRESS["<b>TLP Egress Task</b><br/>trace_dispatcher_task"]
            end

            CH_SENS["<b>SPSC Channels (AsyncQueue)</b><br/>AsyncQueue&lt;GpsFix&gt;<br/>AsyncQueue&lt;MagVector&gt;"]
            TLP_RING["<b>SPSC TLP Ring (g_telemetry_ring)</b><br/>64-Byte Tlp64 Packets"]
        end

        subgraph DSP_SRAM_APERTURE["Dormant HiFi4 DSP SRAM (0x00020000, 128 KB)"]
            PY_VM["<b>MicroPython Runtime</b><br/>Bytecode Engine & GC Heap<br/>Quarantined from MCU Cache"]
        end

        subgraph SHARED_SRAM["Shared SRAM Space 1 (0x40000000)"]
            RPMSG_REPL["<b>RPMsg Vring EP 1026</b><br/>abstractx-repl (Text)"]
            RPMSG_TRACE["<b>RPMsg Vring EP 1025</b><br/>abstractx-trace (CTF 1.8)"]
            RPMSG_TLP["<b>Shared TLP Ring</b><br/>64-Byte Bus Packets"]
        end
    end

    subgraph HOST_LINUX["Host Linux (Cortex-A53 @ 1.4 GHz) / Desktop Studio"]
        UI_REPL["<b>Studio Tab 11 (REPL)</b><br/>Interactive Python Shell (>>> )"]
        UI_TELEM["<b>Studio PFD & Instruments</b><br/>64B TLP Decoder & Visualizer"]
        UI_TRACE["<b>Studio Timeline & Tracing</b><br/>CTF 1.8 / barectf Dynamic Parser"]
    end

    %% Plane 1: REPL (Text)
    UI_REPL <==>|Plane 1: UTF-8 Text Lines| RPMSG_REPL
    RPMSG_REPL <==>|Zero-Copy Line Streaming| CORO_REPL
    CORO_REPL <-->|Execute Script / Bytecode| PY_VM

    %% Plane 2: TLP (64-Byte Binary Packets)
    CORO_SENS -->|Ingest Raw HW| CH_SENS
    CH_SENS -->|Native Channel Pop| PY_VM
    PY_VM -->|Emit Fixed 64B Tlp64| TLP_RING
    TLP_RING -->|co_await pop()| CORO_EGRESS
    CORO_EGRESS ==>|Plane 2: 64B Tlp64 Binary Stream| RPMSG_TLP
    RPMSG_TLP ==>|Zero-Copy DMA / Crossbar| UI_TELEM

    %% Plane 3: barectf (CTF 1.8 Event Stream)
    CORO_SENS -.->|barectf_trace_event()| RPMSG_TRACE
    DISPATCHER -.->|barectf_trace_coro_resume()| RPMSG_TRACE
    RPMSG_TRACE ==>|Plane 3: CTF 1.8 Binary Events| UI_TRACE

    %% Dispatcher Coordination
    DISPATCHER <--->|Pumps & Resumes| COROUTINES
    PY_VM -.->|MICROPY_VM_HOOK_LOOP| DISPATCHER
```

## 3. Normative Requirements Matrix

| Requirement ID | Title | Verification Skill | Implementation Target |
| :--- | :--- | :--- | :--- |
| `SPEC-E906-REPL-01` | **Target Platform Invariant (A5E / A7A E906 Only)** | ``e906-build-layout`` | ``apps/e906_repl_app/CMakeLists.txt`` |
| `SPEC-E906-REPL-02` | **HiFi4 DSP SRAM Quarantine & Zero MCU SRAM Heap** | ``e906-build-layout`, `host-tests`` | ``apps/e906_repl_app/src/mphalport.cpp`, `apps/e906_repl_app/src/main.cpp`` |
| `SPEC-E906-REPL-03` | **Event-Driven MicroPython Coroutine Task** | ``host-tests`, `sanitizer-gate`` | ``apps/e906_repl_app/src/main.cpp`` |
| `SPEC-E906-REPL-04` | **`MICROPY_VM_HOOK_LOOP` Non-Preemptive Interleaving & Ctrl-C Interrupts** | ``host-tests`` | ``apps/e906_repl_app/include/mpconfigport.h`, `apps/e906_repl_app/src/mphalport.cpp`` |
| `SPEC-E906-REPL-05` | **RPMsg Studio REPL Endpoint Protocol** | ``host-tests`` | ``apps/e906_repl_app/src/rpmsg_console.cpp`` |
| `SPEC-E906-REPL-06` | **Native `abstractx` Module Hardware Bindings** | ``host-tests`` | ``apps/e906_repl_app/src/modabstractx.c`` |
| `SPEC-E906-REPL-07` | **Dual-Tier Observability: Lean RemoteProc trace0 vs. Binary barectf Code Tracing** | ``host-tests`, `e906-build-layout`` | ``apps/e906_repl_app/src/main.cpp`, `targets/allwinner_e906/src/trace.cpp`` |
| `SPEC-E906-REPL-08` | **Three-Plane Communication Isolation & Endpoint Mapping** | ``host-tests`, `e906-build-layout`` | ``apps/e906_repl_app/src/main.cpp`, `apps/e906_repl_app/src/rpmsg_console.cpp`` |
| `SPEC-E906-REPL-09` | **Packet-Driven Invariant & No-Peeking Mandate for Telemetry/Sensors** | ``host-tests`` | ``apps/e906_repl_app/flight_sensors.py`, `apps/e906_repl_app/src/modabstractx.c`` |
| `SPEC-E906-REPL-10` | **Native MicroPython TLP Buffer Protocol & Zero-Copy Egress** | ``host-tests`, `e906-build-layout`` | ``apps/e906_repl_app/src/modabstractx.c`, `apps/e906_repl_app/flight_sensors.py`` |
| `SPEC-E906-REPL-11` | **Serial TLP Tunneling & Streaming Protocol for MicroPython** | ``host-tests`, `e906-build-layout`` | ``apps/e906_repl_app/src/modabstractx.c`, `apps/e906_repl_app/flight_sensors.py`` |
| `SPEC-E906-REPL-12` | **Resource Lifecycle & Open/Close Semantics across Host and Target** | ``host-tests`, `e906-build-layout`` | ``apps/e906_repl_app/run_e906_script.py`, `apps/e906_repl_app/src/main.cpp`` |

## 4. Requirement Specifications Detail

### `SPEC-E906-REPL-01`: Target Platform Invariant (A5E / A7A E906 Only)
- **Verification Skill**: ``e906-build-layout``
- **Implementation Target**: ``apps/e906_repl_app/CMakeLists.txt``
- **Key Contract Invariants**:
  * **Requirement**: The `e906_repl_app` is supported **only** on the Allwinner A5E
  * **Enforcement**: CMake configuration must guard compilation with:

### `SPEC-E906-REPL-02`: HiFi4 DSP SRAM Quarantine & Zero MCU SRAM Heap
- **Verification Skill**: ``e906-build-layout`, `host-tests``
- **Implementation Target**: ``apps/e906_repl_app/src/mphalport.cpp`, `apps/e906_repl_app/src/main.cpp``
- **Key Contract Invariants**:
  * **Requirement**:
  * **Rationale**: Quarantining the GC heap in dormant DSP SRAM prevents Python heap

### `SPEC-E906-REPL-03`: Event-Driven MicroPython Coroutine Task
- **Verification Skill**: ``host-tests`, `sanitizer-gate``
- **Implementation Target**: ``apps/e906_repl_app/src/main.cpp``
- **Key Contract Invariants**:
  * **Requirement**:

### `SPEC-E906-REPL-04`: `MICROPY_VM_HOOK_LOOP` Non-Preemptive Interleaving & Ctrl-C Interrupts
- **Verification Skill**: ``host-tests``
- **Implementation Target**: ``apps/e906_repl_app/include/mpconfigport.h`, `apps/e906_repl_app/src/mphalport.cpp``
- **Key Contract Invariants**:
  * **Requirement**:

### `SPEC-E906-REPL-05`: RPMsg Studio REPL Endpoint Protocol
- **Verification Skill**: ``host-tests``
- **Implementation Target**: ``apps/e906_repl_app/src/rpmsg_console.cpp``
- **Key Contract Invariants**:
  * **Requirement**:

### `SPEC-E906-REPL-06`: Native `abstractx` Module Hardware Bindings
- **Verification Skill**: ``host-tests``
- **Implementation Target**: ``apps/e906_repl_app/src/modabstractx.c``
- **Key Contract Invariants**:
  * **Requirement**:
  * `abstractx.peek(addr)`: Reads 32-bit MMIO or SRAM word at address `addr`.
  * `abstractx.poke(addr, val)`: Writes 32-bit MMIO or SRAM word at address `addr`.
  * `abstractx.version()`: Returns AbstractX firmware release string.
  * `abstractx.cycles()`: Returns current RISC-V 64-bit `mcycle` timestamp.

### `SPEC-E906-REPL-07`: Dual-Tier Observability: Lean RemoteProc trace0 vs. Binary barectf Code Tracing
- **Verification Skill**: ``host-tests`, `e906-build-layout``
- **Implementation Target**: ``apps/e906_repl_app/src/main.cpp`, `targets/allwinner_e906/src/trace.cpp``
- **Key Contract Invariants**:
  * **Requirement**:

### `SPEC-E906-REPL-08`: Three-Plane Communication Isolation & Endpoint Mapping
- **Verification Skill**: ``host-tests`, `e906-build-layout``
- **Implementation Target**: ``apps/e906_repl_app/src/main.cpp`, `apps/e906_repl_app/src/rpmsg_console.cpp``
- **Key Contract Invariants**:
  * **Requirement**:

### `SPEC-E906-REPL-09`: Packet-Driven Invariant & No-Peeking Mandate for Telemetry/Sensors
- **Verification Skill**: ``host-tests``
- **Implementation Target**: ``apps/e906_repl_app/flight_sensors.py`, `apps/e906_repl_app/src/modabstractx.c``
- **Key Contract Invariants**:
  * **Requirement**:

### `SPEC-E906-REPL-10`: Native MicroPython TLP Buffer Protocol & Zero-Copy Egress
- **Verification Skill**: ``host-tests`, `e906-build-layout``
- **Implementation Target**: ``apps/e906_repl_app/src/modabstractx.c`, `apps/e906_repl_app/flight_sensors.py``
- **Key Contract Invariants**:
  * **Requirement**:

### `SPEC-E906-REPL-11`: Serial TLP Tunneling & Streaming Protocol for MicroPython
- **Verification Skill**: ``host-tests`, `e906-build-layout``
- **Implementation Target**: ``apps/e906_repl_app/src/modabstractx.c`, `apps/e906_repl_app/flight_sensors.py``
- **Key Contract Invariants**:
  * **Requirement**:

### `SPEC-E906-REPL-12`: Resource Lifecycle & Open/Close Semantics across Host and Target
- **Verification Skill**: ``host-tests`, `e906-build-layout``
- **Implementation Target**: ``apps/e906_repl_app/run_e906_script.py`, `apps/e906_repl_app/src/main.cpp``
- **Key Contract Invariants**:
  * **Requirement**:
