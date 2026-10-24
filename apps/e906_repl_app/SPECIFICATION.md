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
3. **DSP Memory Quarantine**: MicroPython's 128 KB GC heap is quarantined inside the dormant on-chip Cadence HiFi4 Audio DSP memory aperture (`0x00020000`–`0x00040000`).
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
        subgraph E906_MCU["XuanTie E906 RISC-V Core @ 200 MHz (SRAM Space 0: 0x3FFC0000)"]
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
  * **20-Byte Header**: `type` (1B), `flags` (1B), `tag` (1B), `channel` (1B), `target_address` (4B), `length_dw` (2B), `sequence` (2B), `timestamp_ns` (8B).
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
  4. `gc_collect()` MUST spill RISC-V callee-saved registers (`s0`–`s11`) onto the stack before
     invoking `gc_collect_root()` so live object references held in registers across C frames are
     never prematurely swept.
* **Rationale**: Quarantining the GC heap in dormant DSP SRAM prevents Python heap
  fragmentation or GC sweep latency from polluting zero-wait-state MCU flight memory.
* **Verified by**: `e906-build-layout`, `host-tests`
* **Implementation Target**: `apps/e906_repl_app/src/mphalport.cpp`, `apps/e906_repl_app/src/main.cpp`

### `[SPEC-E906-REPL-03]` Event-Driven MicroPython Coroutine Task
* **Requirement**:
  1. MicroPython's REPL MUST execute as cooperative C++20 coroutine tasks (`rpmsg_rx_coroutine`,
     `repl_rx_coroutine`, `repl_tx_coroutine`, spawned as `"rpmsg_rx"`, `"repl_rx"`, `"repl_tx"`,
     `[SPEC-E906-REPL-18]`).
  2. When no input is available, `repl_rx_coroutine` yields execution via `co_await console.read_char_async()`,
     consuming 0 CPU cycles.
  3. Incoming characters and lines are dispatched into `pyexec_event_repl_process_char()`
     without blocking the dispatcher, and `console.flush()` is called when the input queue drains
     so non-newline prompts (`\x04\x04>`) are sent immediately (`[SPEC-E906-REPL-16]`).
  4. On soft reset (`PYEXEC_FORCED_EXIT`, Ctrl-D), the task MUST reinitialize the MicroPython VM
     and REPL context via `mp_deinit()`, `mp_init()`, and `pyexec_event_repl_init()`.
* **Verified by**: `host-tests`, `sanitizer-gate`
* **Implementation Target**: `apps/e906_repl_app/src/main.cpp`

### `[SPEC-E906-REPL-04]` `MICROPY_VM_HOOK_LOOP` Non-Preemptive Interleaving & Ctrl-C Interrupts
* **Requirement**:
  1. The MicroPython bytecode dispatch loop MUST define `MICROPY_VM_HOOK_LOOP` to call
     `abstractx_repl_vm_hook()`.
  2. When an RPMsg doorbell or dispatcher resume is pending (or every 64th backward jump),
     `abstractx_repl_vm_hook()` MUST pump `abstractx::Dispatcher::process()`, `platform_poll_network()`,
     `abstractx::status_service()`, and `flush_if_older(20 ms)`.
  3. Long-running or infinite Python loops (`while True: pass`) MUST NOT starve
     cooperative coroutines, telemetry flushing, or runtime status reporting.
  4. While a script runs (`mp_interrupt_char != -1`), `RpmsgConsole::on_rx_data()` MUST intercept
     `CHAR_CTRL_C` (`0x03`) on arrival and schedule `mp_sched_keyboard_interrupt()` without queueing it,
     and `abstractx_repl_vm_hook()` MUST call `mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS)`
     without draining the console input queue (so `PING` heartbeats remain intact, `[SPEC-E906-REPL-14]`).
  5. Uncaught NLR failures (`nlr_jump_fail`) MUST dump an exception diagnostic autopsy to
     the console rather than silently stalling.
* **Verified by**: `host-tests`
* **Implementation Target**: `apps/e906_repl_app/include/mpconfigport.h`, `apps/e906_repl_app/src/mphalport.cpp`, `apps/e906_repl_app/src/rpmsg_console.cpp`

### `[SPEC-E906-REPL-05]` RPMsg Studio REPL Endpoint Protocol
* **Requirement**:
  1. The REPL communicates over VirtIO RPMsg using dedicated destination endpoint `1026`
     (`"abstractx-repl"`).
  2. Text payloads emitted by MicroPython (`mp_hal_stdout_tx_strn`) are batched into
     RPMsg reply frames and kicked over MSGBOX Channel 1.
  3. Seamlessly interoperates with AbstractX Studio Tab 11 ([SPEC-STUDIO-13]) `>>> ` mode.
* **Verified by**: `host-tests`, `e906-board-selftest`
* **Implementation Target**: `apps/e906_repl_app/src/rpmsg_console.cpp`

### `[SPEC-E906-REPL-06]` Native `abstractx` Module Hardware Bindings
* **Requirement**:
  MicroPython MUST expose a built-in C module `abstractx`:
  * `abstractx.peek(addr)`: Reads full 32-bit unsigned MMIO or SRAM word at address `addr` (`mp_obj_new_int_from_uint`).
  * `abstractx.poke(addr, val)`: Writes 32-bit MMIO or SRAM word at address `addr`.
  * `abstractx.version()`: Returns AbstractX firmware release string.
  * `abstractx.cycles()`: Returns current RISC-V 64-bit `mcycle` timestamp.
  * `abstractx.alive()`: Returns `True` (liveness probe).
  * `abstractx.uart_read()`: Reads pending bytes from UART3 RX ring (u-blox NEO-M8N GPS on Pins 16 & 31, `0x02500C00`).
  * `abstractx.uart_read_into(buf)`: Zero-allocation read of pending UART3 RX bytes into a writable buffer (`bytearray` / `memoryview`), returning bytes copied.
  * `abstractx.uart_dma([enable])`: Queries or switches UART3 RX between circular 16x64 B DMA mode (`True`) and interrupt FIFO mode (`False`, `[SPEC-E906-HAL-02]`).
  * `abstractx.uart_write(data)`: Transmits bytes or string over UART3 to GPS.
  * `abstractx.i2c_scan()`: Probes I2C1 (TWI1, Pins 3 & 5, `0x02502400`) and returns list of responding 7-bit addresses.
  * `abstractx.i2c_read(addr, reg, len)`: Reads `len` bytes from register `reg` on I2C device `addr`.
  * `abstractx.i2c_read_into(addr, reg, buf)`: Zero-allocation read from register `reg` on I2C device `addr` directly into a writable buffer, returning bytes read (`0` on NACK/timeout).
  * Also present and part of the contract: `peek_hex`, `uart_init([baud])`, `uart_status()`,
    `i2c_init([hz])`, `i2c_write(addr, reg, data)`, `sleep_ms(ms)`, `ticks_ms()`,
    `console_read([n])`, `console_read_into(buf)` ([SPEC-E906-REPL-14]),
    `tlp_send(buf)`, `tlp_push(buf)`, `tlp_poll_into(buf)`, `tlp_dropped()`
    ([SPEC-E906-REPL-10]), `trace(task_id[, state[, msg]])` ([SPEC-E906-REPL-07]),
    `irq_stats()` ([SPEC-E906-REPL-16]) and `dma_bench(n[, port])` ([SPEC-E906-HAL-01]).
* **Verified by**: `host-tests`, `e906-board-selftest`
* **Implementation Target**: `apps/e906_repl_app/src/modabstractx.c`

### `[SPEC-E906-REPL-07]` Dual-Tier Observability: Lean RemoteProc trace0 vs. Binary barectf Code Tracing
* **Requirement**:
  1. **RemoteProc `trace0` Buffer**: The memory-mapped debugfs trace buffer (`g_rproc_trace_buffer`, 1 KB in `SRAM_FAST`) is STRICTLY reserved for:
     - Firmware liveness confirmation upon boot (`[E906] Online\n`).
     - Fatal crash autopsies (`hal_crash_dispatcher` register dump and stack unwinding).
     - **Negative Invariant**: It MUST NOT be used for general diagnostic logging, function call tracing, REPL stdout mirroring (`mp_hal_stdout_tx_strn`), or arbitrary printf string emissions. Firmware rodata string literals are prohibited from consuming on-chip SRAM for trace formatting.
  2. **barectf CTF 1.8 Binary Code Tracing**: All code tracing, driver event telemetry, and coroutine execution instrumentation MUST use structured binary CTF 1.8 frames emitted via `barectf` over VirtIO RPMsg endpoint 1025 (`"abstractx-trace"`).
  3. Binary trace packets must be emitted zero-copy into host SRAM buffers without heap allocation or CPU-bound string rendering.
* **Verified by**: `host-tests`, `e906-build-layout`, `e906-board-selftest`
* **Implementation Target**: `apps/e906_repl_app/src/main.cpp`, `apps/e906_repl_app/src/rpmsg_console.cpp`, `targets/allwinner_e906/src/trace.cpp`

### `[SPEC-E906-REPL-08]` Three-Plane Communication Isolation & Endpoint Mapping
* **Requirement**:
  1. The XuanTie E906 runtime MUST maintain strict wire, memory, and channel isolation between the three communications planes:
     - **Interactive REPL Plane**: Transported exclusively over VirtIO RPMsg Endpoint 1026 (`"abstractx-repl"`), carrying UTF-8 text lines.
     - **Real-Time Data Plane**: Transported exclusively as 64-byte `Tlp64` binary packets over lock-free SPSC rings (`SpscTlpRing`, `g_telemetry_ring`) and FPGA crossbar fabrics.
     - **Observability Plane**: Transported exclusively as CTF 1.8 binary event streams over VirtIO RPMsg Endpoint 1025 (`"abstractx-trace"`).
  2. REPL text streams, real-time TLP packets, and barectf diagnostic events MUST NOT be multiplexed into the same channel or share wire encodings.
* **Verified by**: `host-tests`, `e906-build-layout`, `e906-board-selftest`
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
  3. **Execution Latency Invariant**: Packing and submitting a 64-byte TLP frame from MicroPython MUST complete in $< 50 \ \mu\text{s}$ per frame on the 200 MHz XuanTie E906 core.
* **Not implemented yet**: `tlp_send_nav`
* **Verified by**: `host-tests`, `e906-build-layout`, `e906-board-selftest`
* **Implementation Target**: `apps/e906_repl_app/src/modabstractx.c`, `apps/e906_repl_app/flight_sensors.py`

### `[SPEC-E906-REPL-11]` Serial TLP Tunneling & Streaming Protocol for MicroPython
* **Requirement**:
  1. Serial data streams (UART GPS, ESC telemetry, MAVLink) routed to MicroPython MUST be encapsulated in 64-byte `Tlp64` packets using `Channel::EscSerial` (`0x05`):
     - **Serial Ingress (`abstractx.serial_tlp_poll(buf)`)**: The C++ driver chunks incoming serial bytes into 40-byte payloads with sequence IDs and nanosecond timestamps. MicroPython pops incoming serial packets directly into a 64-byte mutable buffer without string allocation.
     - **Serial Egress (`abstractx.serial_tlp_send(data)`)**: Outgoing serial byte sequences are chunked by the native C binding into 40-byte streaming TLPs (`Tlp64::make_stream_tx`) and pushed into the egress SPSC ring, where the hardware serializer transmits them via DMA/FIFO.
  2. **Zero Polling Invariant**: MicroPython MUST NOT busy-poll UART status registers (`LSR`). Ingress serial TLPs are signaled cooperatively via `abstractx::Dispatcher` wakeups.
  3. **Packet Framing Invariant**: Each serial TLP carries a 2-byte sequence counter and 2-byte valid payload length, ensuring lossless stream reassembly across multi-core boundaries.
* **Requirement**: sensor formats belong to the application, not to AbstractX. The E906 script
  sends sensor registers exactly as read (no reordering on the target) and names the chip;
  the PC side decodes them with `apps/e906_repl_app/sensor_contract.py`, which both the board
  bridge and the Studio flight plugin import, so the two cannot disagree. The compass on the
  A5E board is an HMC5883L at `0x1E` (ID `H43`, data registers `0x03..0x08` = X, Z, Y
  big-endian), not a QMC5883L (`0x0D`); reading it from `0x00` returns its config registers.
* **Not implemented yet**: `serial_tlp_poll`, `serial_tlp_send`
* **Verified by**: `host-tests` (`tests/test_board_suite_local.py`: decode and bridge/Studio payload round trip), `e906-board-selftest` (`test_compass_answers_and_changes`), `e906-build-layout`
* **Implementation Target**: `apps/e906_repl_app/src/modabstractx.c`, `apps/e906_repl_app/flight_sensors.py`, `apps/e906_repl_app/raw_sensor_streamer.py`, `apps/e906_repl_app/sensor_contract.py`, `tools/board_studio_bridge.py`, `tools/visualizer/flight_plugin.py`

### `[SPEC-E906-REPL-12]` Resource Lifecycle & Open/Close Semantics across Host and Target
* **Requirement**:
  1. **Target Zero-Filesystem Invariant**: The XuanTie E906 firmware MUST operate with `MICROPY_VFS = 0` and zero POSIX filesystem dependencies. Calling `open()` in MicroPython MUST raise `NameError` (name 'open' is not defined). Scripts are transferred and compiled in-memory over VirtIO RPMsg (`/dev/rpmsg0`).
  2. **Host POSIX Lifecycle Contract**: Linux host utilities (`run_e906_script.py`, `e906_rpmsg_bridge.py`) manage communication endpoints using POSIX `open()` and `close()` (`ioctl(RPMSG_CREATE_EPT_IOCTL)`, `os.open("/dev/rpmsgN")`, `os.close(fd)`). Closing the device node releases host DMA vrings and notifies kernel remoteproc.
  3. **Target Static RAII & Channel Lifetime**: All hardware peripherals on the E906 are initialized via native `abstractx.*_init()` bindings. SPSC channels (`AsyncQueue`, `SpscTlpRing`) operate with static lifetime in SRAM Space 0 and are never closed with `free()`.
  4. **Target Soft-Reset Teardown**: Sending Ctrl-D triggers `mp_deinit()`, sweeping and reclaiming all quarantined HiFi4 DSP SRAM in $\mathcal{O}(1)$ without leaking memory or disrupting running C++ flight coroutines.
* **Verified by**: `host-tests`, `e906-build-layout`
* **Implementation Target**: `apps/e906_repl_app/run_e906_script.py`, `apps/e906_repl_app/src/main.cpp`

### `[SPEC-E906-REPL-13]` Fixed-Point Sensor Fusion & Navigation Vector Synthesis
* **Requirement**:
  1. **Integer Fixed-Point Trigonometry**: In Minimal ROM MicroPython environments lacking `<math.h>` floating-point modules, directional projections MUST use precomputed integer trigonometric tables (scaling factor $10000$) with quadrant symmetry, completing in $\mathcal{O}(1)$ time with zero heap allocation.
  2. **Navigation Vector Synthesis (`NavVectorEngine`)**:
     - **2D Unit Heading Vector (`calc_heading_vector`)**: Decomposed from magnetometer azimuth $\psi$ as $(u_N, u_E) = (\cos \psi, \sin \psi)$ scaled to $\pm 10000$.
     - **Ground Velocity Vector (`calc_ground_velocity_vector`)**: Decomposed from GPS ground speed $v_g$ (cm/s) and Course Over Ground $\chi$ as $(v_N, v_E) = (v_g \cos \chi, v_g \sin \chi)$ in cm/s in the Earth NE frame.
     - **Aerodynamic Drift & Wind Crab Angle (`calc_drift_and_wind`)**: Calculated as $\beta = (\chi - \psi) \pmod{360^\circ}$ normalized to $[-180^\circ, +180^\circ]$, yielding along-track velocity $v_{\text{along}} = v_g \cos \beta$ and cross-track velocity $v_{\text{cross}} = v_g \sin \beta$ in cm/s.
     - **Local Displacement Vector (`calc_local_ned_displacement`)**: Integer flat-earth equirectangular projection yielding $(\Delta N, \Delta E)$ in meters relative to the initial Home fix (`1e-7` deg coordinates).
  3. **Zero Dynamic Allocation**: Vector calculation and formatting MUST NOT allocate intermediate heap objects or lists during steady-state sensor fusion loops.
* **Verified by**: `host-tests`, `e906-build-layout`
* **Implementation Target**: `apps/e906_repl_app/compass_gps_vector.py`

### `[SPEC-E906-REPL-14]` Host-Target Heartbeat Watchdog & Clean Teardown Protocol
* **Requirement**:
  1. **Bidirectional Liveness Heartbeat**: The host bridge (`board_studio_bridge.py`, `run_e906_script.py`) MUST transmit periodic `PING\n` heartbeats at $\le 1.0 \text{ s}$ intervals to the target flight controller over RPMsg stdin.
  2. **Autonomous Target Watchdog Shutdown**: The target streaming loops (`raw_sensor_streamer.py`, `flight_sensors.py`) MUST poll incoming console characters via `abstractx.console_read(64)`. If no valid heartbeat (`PING`, `HEARTBEAT`) is received within $3000 \text{ ms}$, the flight loop MUST autonomously break, output `[E906_WATCHDOG_TIMEOUT_SHUTDOWN]`, and return cleanly to the REPL prompt `>>> `.
  3. **Zero Dangling Remote Loop & Recipient Flooding**: Halting the loop on timeout ensures that if the Linux host crashes, disconnects, or closes `/dev/rpmsg0`, the coprocessor stops transmitting RPMsg text frames, preventing kernel `virtio_rpmsg_bus: msg received with no recipient` syslog floods.
  4. **Orderly Graceful Teardown**: On host shutdown, the bridge MUST issue `STOP\n\x03\x03\x02` and allow $\ge 100 \text{ ms}$ for the target to process teardown before terminating the process or closing the device node.
* **Requirement**: a live stream runs until it is stopped. `STREAM_DURATION=0` reaches
  `run_e906_script.py` as timeout `0` (no limit); the script's watchdog sees every `PING`
  because the firmware takes Ctrl-C out of the input on arrival (only while a script runs)
  and nothing else drains the console queue. Studio copies the repo's board scripts to
  `/root` before it starts the bridge and reads the bridge's output so its pipe never fills.
* **Rationale**: on 2026-10-21 Studio connected, showed data and dropped: the VM hook ate
  the PINGs (3 s watchdog), the runner defaulted to 15 s, and a missing `sensor_contract.py`
  in `/root` stopped the bridge at start.
* **Verified by**: `host-tests`, `e906-build-layout`, `e906-board-selftest` (`test_streamer_keeps_running_with_heartbeat`: 25 s, past the old 15 s limit; `test_streamer_stops_without_heartbeat`)
* **Implementation Target**: `apps/e906_repl_app/src/modabstractx.c`, `apps/e906_repl_app/flight_sensors.py`, `apps/e906_repl_app/raw_sensor_streamer.py`, `tools/board_studio_bridge.py`, `apps/e906_repl_app/run_e906_script.py`, `apps/e906_repl_app/src/rpmsg_console.cpp`, `tools/visualizer/flight_plugin.py`

### `[SPEC-E906-REPL-15]` On-Board Self-Test Suite (Runs on the Cubie A5E Itself)
* **Requirement**: every behaviour above that only silicon can show is checked by
  `tests/board/run_board_tests.py`, run **on the board** with plain `python3` and the
  standard library only (no pytest, no pip). Test files use `test_*` functions and bare
  `assert`, so the same files also run under pytest once the image ships it.
* **Requirement**: the suite checks, against the real endpoints (not host-synthesised data):
  remoteproc running the expected firmware; the three channels announced; REPL text round
  trip; the `abstractx` API names listed in [SPEC-E906-REPL-06] and [SPEC-E906-REPL-10];
  N x 64-byte TLPs read from the `abstractx-tlp` endpoint with intact payload sequence and
  `tlp_dropped() == 0`; CTF packets read from `abstractx-trace` that decode (magic
  `0xC1FC1FC1`, `content_size_bits/8 == datagram length`, 38-byte coroutine events with the
  names and task ids sent); no TLP or CTF bytes in REPL text; compass on I2C1 answering
  and its readings changing; GPS bytes (NMEA `$G` or UBX `B5 62`) on UART3; the streamer
  watchdog stopping without `PING`; the kernel log silent when idle.
* **Requirement**: the runner prints one line per test (`PASS` / `FAIL` / `SKIP` + reason),
  writes a JSON evidence record with `--json <file>`, and exits `0` all passed, `1` any
  failure, `2` could not run (not an E906 board, or the `abstractx-repl` endpoint is held by
  another program such as Studio's bridge, which it names). Decoders live in `tools/abstractx_wire.py`
  so Studio uses the same wire definitions.
* **Rationale**: on 2026-10-20 trace never reached Studio, compass and GPS stopped updating,
  and an earlier checkout passed on host-synthesised TLPs (engineering log entry 15). None of
  that is visible to `host-tests`, and pytest is not on the board image.
* **Verified by**: `e906-board-selftest`, `host-tests` (`tests/test_board_suite_local.py`: decoders and runner, off-board)
* **Implementation Target**: `tests/board/`, `tools/abstractx_wire.py`


### `[SPEC-E906-REPL-16]` Interrupt-Driven Bindings and No Polling in the Event Loop
* **Requirement**: the main loop is only `abstractx::step()`. RPMsg is read after the message box
  interrupt ([SPEC-E906-IRQ-04]); the `abstractx-repl` announcement is retried on each doorbell
  wake, not from a polling loop.
* **Requirement**: UART3 (GPS) uses circular 16x64 B DMA receive (`UartRxRing<64, 16>` in `.sram_c`,
  CLIC line 88 package-end + CLIC line 90 RX-timeout / line-status, `[SPEC-E906-HAL-02]`) by
  default, and can be switched at run time to interrupt-driven FIFO receive on CLIC line 90 via
  `abstractx.uart_dma(False)` ([SPEC-E906-IRQ-02]): in ISR mode the handler drains the RX FIFO into
  a ring on RX-data / RX-timeout, and refills the TX FIFO from a ring on THR-empty. `uart_read` /
  `uart_read_into` takes bytes from the ring without touching the UART; `uart_write` queues bytes
  (or uses TX DMA for transfers larger than the 128 B FIFO) and sleeps (`wfi`) only while busy/full.
* **Requirement**: UART3's FIFOs are 128 B each (T527 User Manual V0.94 §8.19.1: 128 B for UART1-7,
  64 B only for UART0 / S_UART). On RX in ISR mode the handler reads the RX FIFO level (`UART_RFL`, +0x84) once
  and then that many bytes from `RBR`, instead of reading `LSR` before every byte; on THR-empty it
  refills up to 128 B. Baseline before this change (2026-10-22, 115200 baud GPS, MicroPython
  reading every 100 ms): 29 interrupts/s, mean 39 us, median and max 50 us per interrupt.
* **Requirement**: TWI1 (compass) is interrupt-driven on CLIC line 91: one `INT_FLG` interrupt per
  bus step runs a state machine (START, address, register, repeated START, data, STOP).
  `i2c_read` / `i2c_read_into` / `i2c_write` / `i2c_scan` start a transaction and sleep (`wfi`) until the ISR
  completes it or a 20 ms `mtimecmp` timeout fires, which aborts with STOP and returns empty / 0.
  Peripheral data buffers (`s_twi` buffers, UART RX/TX rings and DMA descriptors) reside in `.sram_c`
  (SRAM Space 1) to preserve `SRAM_FAST` (`.bss`). `dma_bench` releases its allocated DMA channel
  upon completion so channels 8..15 are not leaked.
* **Requirement**: blocking MicroPython calls (`sleep_ms`, stdin read, UART/I2C waits) sleep with
  `wfi` under the masked-check pattern ([SPEC-ARCH-06]) and run the VM hook on each wake; they never
  spin on a hardware register.
* **Requirement**: REPL output is flushed when the input queue is empty after a character was
  processed, so a reply that does not end in a newline (the raw-REPL `\x04\x04>` tail) is sent
  without waiting for more input.
* **Requirement**: `abstractx.irq_stats()` returns `{line: count}` for every line that fired plus
  `mtvec`, so the board suite can show each interrupt path ran.
* **Rationale**: on 2026-10-21 every CLIC line had IE = 0, `mtvec` was in direct mode, the UART
  and I2C bindings spun on `LSR`/`INT_FLG`, the main loop polled RPMsg, and the raw-REPL tail
  stayed queued until the host sent more bytes.
* **Verified by**: `e906-board-selftest` (`tests/board/test_e906_irq.py`), `host-tests` (`tests/test_adversarial_invariants.py`)
* **Implementation Target**: `apps/e906_repl_app/src/main.cpp`, `apps/e906_repl_app/src/e906_periph.cpp`, `apps/e906_repl_app/src/modabstractx.c`, `apps/e906_repl_app/src/mphalport.cpp`, `apps/e906_repl_app/src/rpmsg_console.cpp`

### `[SPEC-E906-REPL-19]` Live CLI: Python Runs Beside the Sensor Stream
* **Requirement**: while `raw_sensor_streamer.py` runs, a console line `EXEC <id> <hex>` (the
  command's UTF-8 bytes hex-encoded, so no control byte such as Ctrl-C can appear in it) runs
  that code in one namespace kept across commands (`abstractx` pre-imported): first as an
  expression (its `repr` is printed), on `SyntaxError` as statements. Output is framed
  `!CLI:<id>:BEGIN`, the printed lines, then `!CLI:<id>:END:OK` or `!CLI:<id>:END:ERR:<repr>`.
  An exception never stops the stream; the heartbeat restarts after each command.
* **Requirement**: console input is split into lines; a partial line waits for the rest, and a
  line longer than 1 KB is dropped. `PING` / `STOP` / Ctrl-C keep their meaning.
* **Requirement**: `board_studio_bridge.py` reads its stdin as lines: `CLI <id> <hex>` is forwarded
  as `EXEC <id> <hex>` (writes to the runner are serialised with the heartbeat), and the framed
  output is printed to its stdout unchanged, where Studio reads it ([SPEC-STUDIO-13]). EOF still
  stops the bridge when `EXIT_ON_STDIN_EOF=1`.
* **Constraint**: the board's MicroPython is built at the minimum ROM level: no `bytes.decode` and
  no `bytes.fromhex` (probed 2026-10-22); the streamer decodes hex with `int(.., 16)` and makes
  text with `str(b, "ascii")`.
* **Verified by**: `host-tests` (`tests/test_board_suite_local.py`), `e906-board-selftest` (`tests/board/test_e906_trace.py`)
* **Implementation Target**: `apps/e906_repl_app/raw_sensor_streamer.py`, `tools/board_studio_bridge.py`

### `[SPEC-E906-REPL-18]` Status and Trace Reach Studio by Default
* **Requirement**: the app names its coroutines at spawn (`rpmsg_rx`, `repl_rx`, `repl_tx`); a
  MicroPython script runs inside `repl_rx`, so its load shows there. UART3 and TWI1 describe their
  lines (`uart3` with RX bytes dropped, `twi1` with timeouts) so driver errors are in the report.
* **Requirement**: the VM hook services the status deadline as `step()` does, so reports keep coming
  while a script runs ([SPEC-TRACE-08]).
* **Requirement**: when Studio starts the live link it also starts `e906_rpmsg_bridge.py trace` on the
  board, which forwards every `abstractx-trace` packet to Studio unchanged and exits when its ssh
  session closes. The sensor bridge (`board_studio_bridge.py`) also exits when its ssh session
  closes (`EXIT_ON_STDIN_EOF=1`), so a closed Studio never leaves the REPL held.
* **Verified by**: `e906-board-selftest` (`tests/board/test_e906_trace.py`)
* **Implementation Target**: `apps/e906_repl_app/src/main.cpp`, `apps/e906_repl_app/src/e906_periph.cpp`, `apps/e906_repl_app/src/mphalport.cpp`, `tools/visualizer/flight_plugin.py`, `tools/e906_rpmsg_bridge.py`

### `[SPEC-E906-REPL-17]` Scripts Format Their Own Telemetry; Linux Only Forwards
* **Requirement**: a MicroPython script builds its telemetry frames itself and sends them with
  `abstractx.tlp_send()` on the `abstractx-tlp` endpoint; `tools/e906_rpmsg_bridge.py` forwards
  the frames to Studio unchanged. Nothing on Linux builds or rewrites a frame. The script and its
  Studio plugin share one definition of the payload (`sensor_contract.py`).
* **Rationale**: today `board_studio_bridge.py` parses the streamer's `!RAW` text lines and builds
  the TLPs on Linux, so a Studio view can be right while the E906 sends something else.
* **Not implemented yet**: `stream_tlp`
* **Verified by**: `e906-board-selftest`, `host-tests` (`tests/test_board_suite_local.py`)
* **Implementation Target**: `apps/e906_repl_app/raw_sensor_streamer.py`, `apps/e906_repl_app/sensor_contract.py`, `tools/e906_rpmsg_bridge.py`

### `[SPEC-E906-REPL-20]` Interactive SPI & Dual-SPI DMA Python Interface
* **Requirement**: MicroPython exposes hardware SPI1 (PB11..PB14, DRQ 23, `[SPEC-E906-HAL-03]`) via:
  - `abstractx.spi_init(baud=10000000, dual=False)`: Initializes SPI1 master clock, resets FIFOs, gates CCU clock, and configures PB11 (CS0), PB12 (CLK), PB13 (MOSI/IO0), PB14 (MISO/IO1).
  - `abstractx.spi_xfer(tx_bytes, dual=False) -> bytes`: Transceives byte payload using threshold balancing ($\le 64$ B direct FIFO, $> 64$ B dual-channel DMA). Returns received bytes.
  - `abstractx.spi_xfer_into(tx_buf, rx_buf, dual=False) -> int`: Zero-heap transceive reading and writing into user preallocated buffers. Returns number of bytes transferred.
* **Dual-SPI Support**: In Dual mode (`dual=True`), sets `SUN6I_BURST_CTL_CNT_DRM` (`BIT(28)`) in `SPI_BCC`, driving 2-bit transactions compatible with the FPGA switch fabric (`asp_spi_frontend.sv`).
* **Verified by**: `e906-board-selftest`, `host-tests`
* **Implementation Target**: `apps/e906_repl_app/src/e906_periph.cpp`, `apps/e906_repl_app/src/modabstractx.c`

