# AbstractX XuanTie E906 MicroPython REPL Application Specification

This document is the **Single Source of Truth (SSOT)** for all architectural
contracts, wire formats, hardware invariants, and requirements for the
**AbstractX E906 MicroPython Scripting & REPL Reference Application**
(`apps/e906_repl_app/`).

Each requirement carries a unique **Design ID (`[SPEC-E906-REPL-*]`)** directly
traceable in implementation files via `// @impl [SPEC-E906-REPL-*]`.

---

## 1. Application Overview & Scope

The `e906_repl_app` is an interactive hardware scripting and diagnostic runtime
for the Allwinner A5E / A7A XuanTie E906 coprocessor. It embeds the upstream
MicroPython bytecode engine as an event-driven C++20 coroutine without compromising
the hard real-time determinism or zero-heap constraints of the AbstractX flight loop.

### Core Architecture Invariants:
1. **Target Specificity**: Exclusively targets the Allwinner A5E / A7A (XuanTie E906 + dormant HiFi4 Audio DSP).
2. **Zero MCU SRAM Heap**: The real-time C++ flight engine runs 100% zero-heap in MCU SRAM Space 0 (`0x3FFC0000`).
3. **DSP Memory Quarantine**: MicroPython's 256 KB GC heap is quarantined inside the dormant on-chip Cadence HiFi4 Audio DSP memory aperture (`0x00020000` / `0x00400000`).
4. **Cooperative Coroutine Guest**: The MicroPython REPL executes as a single cooperative C++20 coroutine task inside `abstractx::step()`.
5. **Non-Preemptive Interleaving**: Bytecode loop hooks (`MICROPY_VM_HOOK_LOOP`) yield cooperatively to primary-paced sensor rings.
6. **Unified Observability Integration**: Bridges seamlessly to AbstractX Studio Tab 11 ([SPEC-STUDIO-13]) over RPMsg text frames.

---

## 2. Three-Plane Communication Architecture: REPL, TLP & barectf

The AbstractX XuanTie E906 runtime implements a strict **Three-Plane Separation of Concerns**. Each plane has a distinct purpose, wire format, transport mechanism, and latency guarantee:

| Dimension | 1. Interactive Control Plane (REPL) | 2. Real-Time Data Plane (TLP) | 3. Observability Plane (barectf) |
| :--- | :--- | :--- | :--- |
| **Primary Role** | Human interaction, Python scripting, dynamic configuration | High-rate flight sensor telemetry, actuation, fusion | Execution profiling, coroutine timeline, diagnostics |
| **Wire Format** | UTF-8 interactive text stream | **Fixed 64-byte `Tlp64` binary packets** | **CTF 1.8 binary event stream** (`barectf`) |
| **Transport** | VirtIO RPMsg Endpoint 1026 (`/dev/rpmsg0`, `"abstractx-repl"`) | Lock-free SPSC rings (`g_telemetry_ring`) & FPGA X-Fabric | VirtIO RPMsg Endpoint 1025 (`"abstractx-trace"`) |
| **Memory Isolation** | Quarantined in HiFi4 DSP SRAM (`0x00020000`) | MCU SRAM Space 0 (`0x3FFC0000`) & Shared SRAM | MCU SRAM Space 0 & Shared RPMsg Vrings |
| **Latency / Rate** | 10 Hz – 100 Hz (human-scale, character-paced) | **10 Hz – 8 kHz (deterministic hard real-time)** | Nanosecond event timestamps ($< 10$ CPU cycles/event) |
| **Formatting** | Textual (`>>> ` prompts, string rendering) | **Zero-Copy binary integers / floats (No peeking)** | Binary bit-packed payloads decoded by schema |
| **Host Consumer** | AbstractX Studio Tab 11 / Terminal REPL | Flight Display, Attitude Scope, Autopilot | AbstractX Studio Timeline / Chrome Tracing / Babeltrace |

### Architectural Dataflow & Plane Separation

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

### Detailed Breakdown of the Three Planes:

#### Plane 1: The REPL Interactive Control Plane
* **What it is**: The interactive command, control, and debugging interface for developers and ground control.
* **Protocol & Encodings**: UTF-8 characters and newline-delimited lines. Supports standard VT100 terminal escape sequences, raw REPL mode (`\x01` Ctrl-A), soft reset (`\x04` Ctrl-D), and keyboard interrupt (`\x03` Ctrl-C).
* **Transport**: VirtIO RPMsg Endpoint 1026 (`"abstractx-repl"`), mapped to `/dev/rpmsg0` in Linux user space.
* **Safety & Invariants**: Quarantined strictly in dormant HiFi4 DSP SRAM (`0x00020000`). If a user script crashes or runs out of memory, it triggers a non-local jump (`nlr_jump`) and resets the VM without affecting the C++ flight dispatcher or corrupting MCU flight memory.

#### Plane 2: The TLP Real-Time Data Plane (`Tlp64`)
* **What it is**: The high-throughput, deterministic sensor ingestion, state estimation, and actuation bus.
* **Protocol & Encodings**: Fixed 64-byte `Tlp64` binary frames (`sizeof(asp_tlp64_t) == 64`, `alignas(64)`):
  * **24-Byte Header**: `type` (1B), `flags` (1B), `tag` (1B), `channel` (1B), `target_address` (4B), `length_dw` (2B), `sequence` (2B), `timestamp_ns` (8B).
  * **40-Byte Payload**: Pre-scaled binary integer records (GPS latitude/longitude in $10^{-7}$ deg, altitude in mm, 3-axis gyro in $0.01^\circ/\text{s}$, 3-axis accel in $\text{mg}$).
  * **4-Byte Trailer**: Hardware CRC32.
* **Transport**: Lock-free SPSC circular ring buffers (`SpscTlpRing`, `g_telemetry_ring`) and synthesizable FPGA AXI-Stream crossbar switch fabric (`asp_router.sv`).
* **The No-Peeking Mandate**:
  - Direct register peeking or field-by-field polling over the bus is strictly prohibited.
  - Transactions move as atomic, self-describing 64-byte packets.
  - Producers borrow a slot (`acquire`), populate the known binary struct in place, and commit (`commit`). Consumers pop full packets atomically in $< 50 \text{ ns}$.

#### Plane 3: The barectf CTF 1.8 Observability Plane
* **What it is**: High-fidelity, nanosecond-resolution code execution tracing and diagnostic instrumentation.
* **Protocol & Encodings**: Common Trace Format (CTF 1.8) bit-packed binary structures configured in `barectf_config.yaml`.
* **Transport**: VirtIO RPMsg Endpoint 1025 (`"abstractx-trace"`).
* **Key Characteristic**: Zero string formatting in firmware. An event call (e.g. `barectf_default_trace_coro_resume(ctx, coro_id, task_state)`) takes only 4 to 12 CPU instructions to write a compact 8-to-16 byte binary record into the trace buffer. Host tools (AbstractX Studio Timeline, Babeltrace) use the CTF metadata schema (`trace_schema.json`) to expand the fields into human-readable traces, timelines, and flame graphs.

---

## 3. Normative Requirements

### `[SPEC-E906-REPL-01]` Target Platform Invariant (A5E / A7A E906 Only)
* **Requirement**: The `e906_repl_app` is supported **only** on the Allwinner A5E
  and A7A targets (`allwinner_e906`). It MUST NOT compile for host SITL, Pico 2 W,
  or ESP32-P4 targets that lack the secondary DSP memory aperture.
* **Enforcement**: CMake configuration must guard compilation with:
  `if (NOT ABSTRACTX_TARGET STREQUAL "allwinner_e906") return() endif()`.
* **Verified by**: `e906-build-layout`
* **Implementation Target**: `apps/e906_repl_app/CMakeLists.txt`

### `[SPEC-E906-REPL-02]` HiFi4 DSP SRAM Quarantine & Zero MCU SRAM Heap
* **Requirement**:
  1. MicroPython's Garbage Collector (`gc_init`) MUST be initialized with start address
     `0x00020000` (DSP Local RAM Base) and length `128 KB` (up to `0x00040000`).
  2. MicroPython MUST NOT call standard C `malloc()`, `calloc()`, or `free()`.
  3. No MicroPython objects, parse trees, or GC blocks may reside in MCU SRAM Space 0
     (`0x3FFC0000`) or Space 1 (`0x40000000`).
* **Rationale**: Quarantining the GC heap in dormant DSP SRAM prevents Python heap
  fragmentation or GC sweep latency from polluting zero-wait-state MCU flight memory.
* **Verified by**: `e906-build-layout`, `host-tests`
* **Implementation Target**: `apps/e906_repl_app/src/mphalport.cpp`, `apps/e906_repl_app/src/main.cpp`

### `[SPEC-E906-REPL-03]` Event-Driven MicroPython Coroutine Task
* **Requirement**:
  1. MicroPython's REPL MUST execute as a cooperative C++20 coroutine task:
     `coro::Task<void> repl_task(hal::IRpmsg &rpmsg)`.
  2. When no input is available, the coroutine yields execution via `co_await console.read_char_async()`,
     consuming 0 CPU cycles.
  3. Incoming characters and lines are dispatched into `pyexec_event_repl_process_char()`
     without blocking the dispatcher.
  4. On soft reset (`PYEXEC_FORCED_EXIT`, Ctrl-D), the task MUST reinitialize the MicroPython VM
     and REPL context via `mp_deinit()`, `mp_init()`, and `pyexec_event_repl_init()`.
* **Verified by**: `host-tests`, `sanitizer-gate`
* **Implementation Target**: `apps/e906_repl_app/src/main.cpp`

### `[SPEC-E906-REPL-04]` `MICROPY_VM_HOOK_LOOP` Non-Preemptive Interleaving & Ctrl-C Interrupts
* **Requirement**:
  1. The MicroPython bytecode dispatch loop MUST define `MICROPY_VM_HOOK_LOOP` to call
     `abstractx::repl_vm_hook()`.
  2. On every loop invocation (or prescaled VM tick), `abstractx::repl_vm_hook()` MUST
     pump pending ISR dispatcher queues and service high-rate primary-paced sensor rings.
  3. Long-running or infinite Python loops (`while True: pass`) MUST NOT starve
     the 8 kHz IMU fusion pipeline or watchdog timers.
  4. The VM hook MUST inspect the console input ring for interrupt character `CHAR_CTRL_C`
     (`0x03`) and schedule `mp_sched_keyboard_interrupt()` to trigger a non-local return (`nlr_jump`)
     back to the REPL prompt.
  5. Uncaught NLR failures (`nlr_jump_fail`) MUST dump an exception diagnostic autopsy to
     the console rather than silently stalling.
* **Verified by**: `host-tests`
* **Implementation Target**: `apps/e906_repl_app/include/mpconfigport.h`, `apps/e906_repl_app/src/mphalport.cpp`

### `[SPEC-E906-REPL-05]` RPMsg Studio REPL Endpoint Protocol
* **Requirement**:
  1. The REPL communicates over VirtIO RPMsg using dedicated destination endpoint `1026`
     (`"abstractx-repl"`).
  2. Text payloads emitted by MicroPython (`mp_hal_stdout_tx_strn`) are batched into
     RPMsg reply frames and kicked over MSGBOX Channel 1.
  3. Seamlessly interoperates with AbstractX Studio Tab 11 ([SPEC-STUDIO-13]) `>>> ` mode.
* **Verified by**: `host-tests`
* **Implementation Target**: `apps/e906_repl_app/src/rpmsg_console.cpp`

### `[SPEC-E906-REPL-06]` Native `abstractx` Module Hardware Bindings
* **Requirement**:
  MicroPython MUST expose a built-in C module `abstractx`:
  * `abstractx.peek(addr)`: Reads 32-bit MMIO or SRAM word at address `addr`.
  * `abstractx.poke(addr, val)`: Writes 32-bit MMIO or SRAM word at address `addr`.
  * `abstractx.version()`: Returns AbstractX firmware release string.
  * `abstractx.cycles()`: Returns current RISC-V 64-bit `mcycle` timestamp.
  * `abstractx.alive()`: Returns `True` (liveness probe).
  * `abstractx.uart_read()`: Reads pending bytes from UART3 FIFO (u-blox NEO-M8N GPS on Pins 16 & 31, `0x02500C00`).
  * `abstractx.uart_write(data)`: Transmits bytes or string over UART3 to GPS.
  * `abstractx.i2c_scan()`: Probes I2C1 (TWI1, Pins 3 & 5, `0x02502400`) and returns list of responding 7-bit addresses.
  * `abstractx.i2c_read(addr, reg, len)`: Reads `len` bytes from register `reg` on I2C device `addr`.
* **Verified by**: `host-tests`
* **Implementation Target**: `apps/e906_repl_app/src/modabstractx.c`

### `[SPEC-E906-REPL-07]` Dual-Tier Observability: Lean RemoteProc trace0 vs. Binary barectf Code Tracing
* **Requirement**:
  1. **RemoteProc `trace0` Buffer**: The memory-mapped debugfs trace buffer (`g_rproc_trace_buffer`, 1 KB in `SRAM_FAST`) is STRICTLY reserved for:
     - Firmware liveness confirmation upon boot (`[E906] Online\n`).
     - Fatal crash autopsies (`hal_crash_dispatcher` register dump and stack unwinding).
     - **Negative Invariant**: It MUST NOT be used for general diagnostic logging, function call tracing, or arbitrary printf string emissions. Firmware rodata string literals are prohibited from consuming on-chip SRAM for trace formatting.
  2. **barectf CTF 1.8 Binary Code Tracing**: All code tracing, driver event telemetry, and coroutine execution instrumentation MUST use structured binary CTF 1.8 frames emitted via `barectf` over VirtIO RPMsg endpoint 1025 (`"abstractx-trace"`).
  3. Binary trace packets must be emitted zero-copy into host SRAM buffers without heap allocation or CPU-bound string rendering.
* **Verified by**: `host-tests`, `e906-build-layout`
* **Implementation Target**: `apps/e906_repl_app/src/main.cpp`, `targets/allwinner_e906/src/trace.cpp`

### `[SPEC-E906-REPL-08]` Three-Plane Communication Isolation & Endpoint Mapping
* **Requirement**:
  1. The XuanTie E906 runtime MUST maintain strict wire, memory, and channel isolation between the three communications planes:
     - **Interactive REPL Plane**: Transported exclusively over VirtIO RPMsg Endpoint 1026 (`"abstractx-repl"`), carrying UTF-8 text lines.
     - **Real-Time Data Plane**: Transported exclusively as 64-byte `Tlp64` binary packets over lock-free SPSC rings (`SpscTlpRing`, `g_telemetry_ring`) and FPGA crossbar fabrics.
     - **Observability Plane**: Transported exclusively as CTF 1.8 binary event streams over VirtIO RPMsg Endpoint 1025 (`"abstractx-trace"`).
  2. REPL text streams, real-time TLP packets, and barectf diagnostic events MUST NOT be multiplexed into the same channel or share wire encodings.
* **Verified by**: `host-tests`, `e906-build-layout`
* **Implementation Target**: `apps/e906_repl_app/src/main.cpp`, `apps/e906_repl_app/src/rpmsg_console.cpp`

### `[SPEC-E906-REPL-09]` Packet-Driven Invariant & No-Peeking Mandate for Telemetry/Sensors
* **Requirement**:
  1. All real-time telemetry, sensor ingestion records, and navigation solutions MUST be encapsulated into fixed, cache-line aligned 64-byte `Tlp64` packets (`sizeof(asp_tlp64_t) == 64`, `alignas(64)`).
  2. **Negative Invariant**: High-rate flight loops and telemetry bridges MUST NOT use register peeking, address-based polling, or field-by-field memory reads over IPC.
  3. Producers MUST acquire and commit entire 64-byte packet frames into wait-free SPSC rings, and consumers MUST pop full packets atomically in $\mathcal{O}(1)$ time.
* **Verified by**: `host-tests`
* **Implementation Target**: `apps/e906_repl_app/flight_sensors.py`, `apps/e906_repl_app/src/modabstractx.c`

### `[SPEC-E906-REPL-10]` Native MicroPython TLP Buffer Protocol & Zero-Copy Egress
* **Requirement**:
  1. MicroPython's `abstractx` C module MUST provide zero-allocation TLP packet primitives:
     - `abstractx.tlp_push(buf)`: Accepts any Python object implementing the Python buffer protocol (`bytes`, `bytearray`, `memoryview`). Validates exact 64-byte length (`sizeof(asp_tlp64_t)`). Directly copies into `g_telemetry_ring` or emplaced RPMsg slot.
     - `abstractx.tlp_poll_into(bytearray_64)`: Non-blocking pop from the ingress SPSC TLP ring directly into a pre-allocated mutable buffer without heap allocation. Returns `True` if a packet was dequeued, `False` if empty.
     - `abstractx.tlp_send_nav(lat, lon, alt_mm, speed_mms, heading_cdeg, sats, fix)`: Direct C helper that packs pre-scaled navigation integers into an in-place `asp_tlp64_unaligned_t*` slot borrowed via `tlp_channel.emplace()`.
  2. **Zero GC Pressure Invariant**: Continuous telemetry emissions (10 Hz – 100 Hz) MUST allocate **0 bytes** on the MicroPython garbage-collected heap during steady-state flight loops.
  3. **Execution Latency Invariant**: Packing and submitting a 64-byte TLP frame from MicroPython MUST complete in $< 5 \ \mu\text{s}$ per frame on the 800 MHz XuanTie E906 core.
* **Verified by**: `host-tests`, `e906-build-layout`
* **Implementation Target**: `apps/e906_repl_app/src/modabstractx.c`, `apps/e906_repl_app/flight_sensors.py`

### `[SPEC-E906-REPL-11]` Serial TLP Tunneling & Streaming Protocol for MicroPython
* **Requirement**:
  1. Serial data streams (UART GPS, ESC telemetry, MAVLink) routed to MicroPython MUST be encapsulated in 64-byte `Tlp64` packets using `Channel::EscSerial` (`0x05`):
     - **Serial Ingress (`abstractx.serial_tlp_poll(buf)`)**: The C++ driver chunks incoming serial bytes into 40-byte payloads with sequence IDs and nanosecond timestamps. MicroPython pops incoming serial packets directly into a 64-byte mutable buffer without string allocation.
     - **Serial Egress (`abstractx.serial_tlp_send(data)`)**: Outgoing serial byte sequences are chunked by the native C binding into 40-byte streaming TLPs (`Tlp64::make_stream_tx`) and pushed into the egress SPSC ring, where the hardware serializer transmits them via DMA/FIFO.
  2. **Zero Polling Invariant**: MicroPython MUST NOT busy-poll UART status registers (`LSR`). Ingress serial TLPs are signaled cooperatively via `abstractx::Dispatcher` wakeups.
  3. **Packet Framing Invariant**: Each serial TLP carries a 2-byte sequence counter and 2-byte valid payload length, ensuring lossless stream reassembly across multi-core boundaries.
* **Verified by**: `host-tests`, `e906-build-layout`
* **Implementation Target**: `apps/e906_repl_app/src/modabstractx.c`, `apps/e906_repl_app/flight_sensors.py`

### `[SPEC-E906-REPL-12]` Resource Lifecycle & Open/Close Semantics across Host and Target
* **Requirement**:
  1. **Target Zero-Filesystem Invariant**: The XuanTie E906 firmware MUST operate with `MICROPY_VFS = 0` and zero POSIX filesystem dependencies. Calling `open()` in MicroPython MUST raise `NameError` (name 'open' is not defined). Scripts are transferred and compiled in-memory over VirtIO RPMsg (`/dev/rpmsg0`).
  2. **Host POSIX Lifecycle Contract**: Linux host utilities (`run_e906_script.py`, `e906_rpmsg_bridge.py`) manage communication endpoints using POSIX `open()` and `close()` (`ioctl(RPMSG_CREATE_EPT_IOCTL)`, `os.open("/dev/rpmsgN")`, `os.close(fd)`). Closing the device node releases host DMA vrings and notifies kernel remoteproc.
  3. **Target Static RAII & Channel Lifetime**: All hardware peripherals on the E906 are initialized via native `abstractx.*_init()` bindings. SPSC channels (`AsyncQueue`, `SpscTlpRing`) operate with static lifetime in SRAM Space 0 and are never closed with `free()`.
  4. **Target Soft-Reset Teardown**: Sending Ctrl-D triggers `mp_deinit()`, sweeping and reclaiming all quarantined HiFi4 DSP SRAM in $\mathcal{O}(1)$ without leaking memory or disrupting running C++ flight coroutines.
* **Verified by**: `host-tests`, `e906-build-layout`
* **Implementation Target**: `apps/e906_repl_app/run_e906_script.py`, `apps/e906_repl_app/src/main.cpp`


