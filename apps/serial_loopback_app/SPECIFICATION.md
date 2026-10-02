# AbstractX Serial Loopback & Profiler Application Specification

This document is the **Single Source of Truth (SSOT)** for all architectural
contracts, wire formats, hardware invariants, and requirements for the
**AbstractX Serial Loopback & Profiler Reference Application**
(`apps/serial_loopback_app/`).

Each requirement carries a unique **Design ID (`[SPEC-LOOP-*]`)** directly
traceable in implementation files via `// @impl [SPEC-LOOP-*]`.

---

## 1. Application Overview & Scope

The `serial_loopback_app` is the foundational reference application for
validating the AbstractX C++20 coroutine runtime, hardware threshold-balanced
serial drivers, and bi-directional host telemetry across all target platforms:
1. **Universal Portability**: Compiles without application `#ifdef`s across
   Allwinner E906 (T527), Raspberry Pi Pico 2 W (RP2350), ESP32-P4, and Linux.
2. **Threshold-Balanced I/O**: Direct CPU FIFO for frames `<= 32` bytes vs.
   hardware DMA engines for bulk bursts `> 32` bytes.
3. **Pure Asynchrony**: Zero CPU busy-polling; tasks suspend in `~18 ns` and
   wake on hardware interrupts in `< 25 ns`.
4. **CTF 1.8 / barectf Telemetry**: Emits binary trace packets into on-chip
   SRAM and streams them to the AbstractX Observability Studio.
5. **Hardware CPU Profiling**: Live microsecond calculation of CPU Active%
   versus low-power `wfi` sleep states.

---

## 2. Multi-Target Silicon Execution Invariants

```text
┌─────────────────────────────────────────────────────────────┐
│             Multi-Target Ping & Command Topologies          │
├───────────────────┬───────────────────┬─────────────────────┤
│ Platform Target   │ Core Architecture │ Interconnect Path   │
├───────────────────┼───────────────────┼─────────────────────┤
│ Allwinner CubieA5E│ XuanTie E906 Co-P │ GUI -> UDP :9871 -> │
│                   │ (RV32IMAFDC 200M) │ /dev/rpmsg0 -> E906 │
├───────────────────┼───────────────────┼─────────────────────┤
│ Raspberry Pi      │ Dual Cortex-M33   │ GUI -> UDP :9871 -> │
│ Pico 2 W          │ (ARMv8-M 150 MHz) │ CYW43439 -> Core 0/1│
├───────────────────┼───────────────────┼─────────────────────┤
│ Espressif ESP32-P4│ Dual RISC-V HP    │ GUI -> UDP :9871 -> │
│                   │ (RV32IMAFDC 400M) │ Wi-Fi/ETH -> App    │
├───────────────────┼───────────────────┼─────────────────────┤
│ Desktop SITL      │ x86_64 / AArch64  │ GUI -> UDP 127.0.0.1│
│ (Linux Host)      │ Workstation       │ :9871 -> SITL Sim   │
└───────────────────┴───────────────────┴─────────────────────┘
```

### Invariant Rules:
1. **Zero Dynamic Memory Allocation**: No `malloc`, `free`, or `operator new`
   during execution. All queues, coroutines, and buffers are statically sized.
2. **Zero Synchronous Polling**: Drivers MUST expose awaitable C++20 coroutine
   primitives (`async_read_packet()`, `async_write()`).
3. **Zero Application `#ifdef` Directives**: Top-level coroutine tasks in
   `src/main.cpp` MUST compile unmodified across all target platforms.

---

## 3. 64-Byte TLP Diagnostic Packet Wire Contract

Diagnostic telemetry packets conform to the standardized 64-byte `Tlp64`
layout matching barectf CTF 1.8 (`magic = 0xC1FC1FC1`):

| Byte Offset | Field Name | Type | Scale | Unit | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `0..3` | `magic` | `uint32` | 1.0 | — | CTF Header: `0xC1FC1FC1` |
| `4..7` | `seq` | `uint32` | 1.0 | — | Monotonic sequence number |
| `8..15` | `timestamp_ns` | `uint64` | 1.0 | ns | 64-bit nanosecond timestamp |
| `16..19` | `packets_echoed`| `uint32` | 1.0 | pkts | Total packets processed |
| `20..23` | `bytes_echoed` | `uint32` | 1.0 | bytes| Total bytes transferred |
| `24..27` | `dma_bursts` | `uint32` | 1.0 | bursts| Bursts processed via DMA |
| `28..31` | `cpu_active_pct`| `uint32` | 0.1 | % | Active CPU permille |
| `32..59` | `reserved` | `uint8[28]`| 1.0 | — | Zero padding to 64 bytes |
| `60..63` | `crc32` | `uint32` | 1.0 | — | IEEE 802.3 Frame Check |

---

## 4. Normative Specification Requirements Matrix

### `[SPEC-LOOP-01]` Zero-Allocation C++20 Coroutine Loopback
The application MUST implement serial reception and echo exclusively using
asynchronous C++20 coroutines (`co_await`), drawing coroutine frames from
statically sized memory pools with 0 heap bytes allocated.

### `[SPEC-LOOP-02]` Threshold-Balanced Serial I/O
The serial driver MUST balance latency versus bandwidth:
* Packets `<= 32` bytes MUST write directly to hardware FIFO without DMA.
* Packets `> 32` bytes MUST stream via hardware DMA and suspend calling tasks.

### `[SPEC-LOOP-03]` Receiver Timeout (RTO) Non-Blocking Framing
Variable-length packets MUST be detected via hardware Receiver Timeout (RTO)
after 4 character times of idle line detection, resuming the coroutine with
exact packet length and zero CPU byte-copying.

### `[SPEC-LOOP-04]` Universal Multi-Target Portability
The core loopback logic in `src/main.cpp` MUST compile and run unmodified
across XuanTie E906, RP2350, ESP32-P4, and Linux with zero `#ifdef` directives.

### `[SPEC-LOOP-05]` 64-Byte TLP Wire Contract
Telemetry and heartbeat events MUST be packaged into 64-byte `Tlp64` packets
satisfying `alignas(64)` and containing IEEE 802.3 CRC32 verification.

### `[SPEC-LOOP-06]` barectf CTF 1.8 Telemetry Compliance
Diagnostic events MUST conform to the barectf Common Trace Format 1.8 schema,
enabling automated ingestion by the AbstractX Observability Studio.

### `[SPEC-LOOP-07]` Hardware Performance Counter Cycle Profiling
The application MUST read hardware cycle counters (`mcycle`/`minstret`) every
1000 ms to compute CPU active time versus low-power `wfi` sleep time.

### `[SPEC-LOOP-08]` Bi-Directional Host IPC & Network Ping Bridge
The application MUST support bi-directional ping and command handling:
* On heterogeneous SoCs: Routed via Linux RemoteProc VirtIO (`/dev/rpmsg0`).
* On networked microcontrollers: Routed via UDP socket (port 9871).

---

## 5. Traceability Matrix

| Requirement | Description | Implementation Target |
| :--- | :--- | :--- |
| `[SPEC-LOOP-01]` | Zero-Allocation Coroutines | `src/main.cpp` |
| `[SPEC-LOOP-02]` | Threshold-Balanced I/O | `platforms/e906/main.cpp` |
| `[SPEC-LOOP-03]` | UART RTO Packet Framing | `hal/uart.cpp` |
| `[SPEC-LOOP-04]` | Multi-Target Portability | `src/main.cpp` |
| `[SPEC-LOOP-05]` | 64-Byte TLP Contract | `platforms/e906/main.cpp` |
| `[SPEC-LOOP-06]` | barectf CTF 1.8 Trace | `platforms/e906/main.cpp` |
| `[SPEC-LOOP-07]` | Hardware CPU Profiler | `platforms/e906/main.cpp` |
| `[SPEC-LOOP-08]` | Host IPC & Ping Bridge | `hal/rpmsg.cpp` |
