# AbstractX XuanTie E906 Target Specification & Architecture Matrix — Developer Guide

> **SSOT Notice**: This documentation is automatically generated from the authoritative specification [`SPECIFICATION.md`](SPECIFICATION.md). Do not edit manually.

## 1. Architectural Overview

This guide provides operational and implementation details for `AbstractX XuanTie E906 Target Specification & Architecture Matrix`. Total governing requirements: **14**.

## 2. System Architecture Diagrams

### Diagram 1: Dataflow & Architecture
```mermaid
sequenceDiagram
  participant Host as Linux Host
  participant IRQ as E906 MSGBOX ISR
  participant Queue as ISR-safe Dispatcher Queue
  participant Loop as abstractx::step()
  participant Task as Suspended RPMsg Coroutine
  Host->>IRQ: Channel 1 doorbell
  IRQ->>IRQ: Acknowledge interrupt
  IRQ->>Queue: Post suspended coroutine handle
  Loop->>Queue: Drain ready handles
  Queue->>Task: Resume in cooperative context
```

## 3. Normative Requirements Matrix

| Requirement ID | Title | Verification Skill | Implementation Target |
| :--- | :--- | :--- | :--- |
| `SPEC-E906-ARCH-01` | **Processor Core Architecture** | ``host-tests` (`tests/test_adversarial_invariants.py`)` | ``targets/allwinner_e906/bsp/startup.S`` |
| `SPEC-E906-ARCH-02` | **Fixed Hardware System Memory Map (SYSMAP)** | ``host-tests` (`tests/test_adversarial_invariants.py`)` | ``targets/allwinner_e906/include/memory_map.h`` |
| `SPEC-E906-MEM-01` | **Pure Dedicated MCU SRAM Architecture (Zero TCM)** | ``e906-build-layout`, `host-tests` (`sim_test_gps_imu_coro_pool`)` | ``targets/allwinner_e906/bsp/e906_sram.ld`, `bsp/e906_ddr.ld`, `targets/allwinner_e906/CMakeLists.txt`, `include/memory_map.h`` |
| `SPEC-E906-MEM-02` | **Control vs. Data Plane Memory Split** | `unspecified` | `unspecified` |
| `SPEC-E906-CACHE-01` | **Vendor Instruction Extension Unlock** | `unspecified` | ``targets/allwinner_e906/bsp/startup.S`` |
| `SPEC-E906-CACHE-02` | **L1 Cache Policy: I-Cache On, D-Cache Off (SRAM-Only Firmware)** | ``e906-disassembly-check` (`csrw 0x7c1` with `0x1031`)` | ``targets/allwinner_e906/bsp/startup.S`` |
| `SPEC-E906-CACHE-03` | **Verified Machine-Code Cache Maintenance** | `unspecified` | ``targets/allwinner_e906/src/pmp.cpp`` |
| `SPEC-E906-IPC-01` | **Pure Virtual `IRpmsg` Contract** | ``host-tests` (`sim/test_e906_rpmsg_flow_control.cpp`), `e906-build-layout`` | ``targets/allwinner_e906/include/hal/rpmsg.hpp`` |
| `SPEC-E906-IPC-02` | **Standard Linux VirtIO RPMsg (`class Rpmsg`)** | ``host-tests` (`sim/test_e906_rpmsg_endpoints.cpp`, `sim/test_e906_rpmsg_flow_control.cpp`), `e906-build-layout`` | ``targets/allwinner_e906/src/rpmsg.cpp`` |
| `SPEC-E906-IPC-03` | **Lite-Metal Direct Driver (`class RpmsgLiteMetal`)** | ``host-tests` (`sim/test_e906_rpmsg_flow_control.cpp`), `e906-build-layout`` | ``targets/allwinner_e906/src/rpmsg.cpp`` |
| `SPEC-E906-IPC-04` | **Zero-Copy TLP and Trace Endpoints over RPMsg in SRAM** | ``host-tests` (`sim/test_e906_rpmsg_endpoints.cpp`), `sanitizer-gate`, `e906-build-layout`, `board-bringup` (not yet run)` | ``targets/allwinner_e906/src/rpmsg.cpp`, `include/hal/rpmsg.hpp`, `include/abstractx/trace/tracer.hpp`, `src/platform.cpp`, `include/tlp_ring.hpp`, `targets/allwinner_e906/CMakeLists.txt`` |
| `SPEC-E906-CORO-01` | **Zero-Polling Asynchronous RPMsg Awaiter** | ``host-tests` (`sim/test_e906_rpmsg_flow_control.cpp`), `e906-build-layout`` | ``targets/allwinner_e906/include/hal/rpmsg.hpp`` |
| `SPEC-E906-CORO-02` | **MSGBOX ISR-to-Dispatcher Wakeup** | ``e906-disassembly-check` (`fsd f0, 144(sp)`, `lw t0, 128(sp); bltz t0`, `fld f0, 144(sp)`), `e906-build-layout`` | ``targets/allwinner_e906/src/rpmsg.cpp`` |
| `SPEC-E906-HAL-01` | **Direct FIFO vs. DMA Threshold Balancing** | `unspecified` | ``targets/allwinner_e906/src/{spi,i2c,uart,dma}.cpp`` |

## 4. Requirement Specifications Detail

### `SPEC-E906-ARCH-01`: Processor Core Architecture
- **Verification Skill**: ``host-tests` (`tests/test_adversarial_invariants.py`)`
- **Implementation Target**: ``targets/allwinner_e906/bsp/startup.S``
- **Key Contract Invariants**:
  * **Target Core**: T-Head / XuanTie E906 (32-bit RISC-V: RV32IMAFDC).
  * **Operating Frequency**: Up to 200 MHz driven by the MCU CCU PLL.
  * **Privilege Modes**: Machine (M) and User (U) modes only.
  * **MMU Absence**: The E906 does NOT implement Supervisor mode virtual memory
  * **Refines**: `[SPEC-ARCH-04]`

### `SPEC-E906-ARCH-02`: Fixed Hardware System Memory Map (SYSMAP)
- **Verification Skill**: ``host-tests` (`tests/test_adversarial_invariants.py`)`
- **Implementation Target**: ``targets/allwinner_e906/include/memory_map.h``
- **Key Contract Invariants**:
  * **Hardware PMA Invariant**: Physical Memory Attributes (PMAs) are hardwired
  * **Attributes**:
  * Peripheral MMIO (`< 0x3FFC0000`): Hardwired to **Strongly Ordered**
  * Internal SRAM (`0x3FFC0000`–`0x40080000`):
  * External DDR DRAM (`0x40000000`–`0xC0000000`):

### `SPEC-E906-MEM-01`: Pure Dedicated MCU SRAM Architecture (Zero TCM)
- **Verification Skill**: ``e906-build-layout`, `host-tests` (`sim_test_gps_imu_coro_pool`)`
- **Implementation Target**: ``targets/allwinner_e906/bsp/e906_sram.ld`, `bsp/e906_ddr.ld`, `targets/allwinner_e906/CMakeLists.txt`, `include/memory_map.h``
- **Key Contract Invariants**:
  * **Silicon Reality**: There is **NO ITCM and NO DTCM** on the T527 E906.
  * **Execution Windows** (E906 core address → Linux host physical address). These
  * `0x07130000` is the RISC-V CFG register block (4 KB: `STA_ADD_REG` at `+0x204`,
  * **Coroutine Frame Pool Right-Sizing**: The static coroutine frame pool
  * **Zero-Copy Trace Pool Elision**: Because the E906 provides a native zero-copy

### `SPEC-E906-MEM-02`: Control vs. Data Plane Memory Split
- **Verification Skill**: `unspecified`
- **Implementation Target**: `unspecified`
- **Key Contract Invariants**:
  * **Control Plane in SRAM**: SPSC ring indices, descriptor states, and doorbell
  * **Data Plane in SRAM**: trace and telemetry payloads are RPMsg buffers in
  * **Target**: `targets/allwinner_e906/include/dram_spsc_protocol.h`

### `SPEC-E906-CACHE-01`: Vendor Instruction Extension Unlock
- **Verification Skill**: `unspecified`
- **Implementation Target**: ``targets/allwinner_e906/bsp/startup.S``
- **Key Contract Invariants**:
  * **Requirement**: T-Head custom instruction extensions MUST be unlocked at boot
  * **Mechanism**: In `startup.S`, set bit 22 (`THEADISAEE = 1`) and bit 15

### `SPEC-E906-CACHE-02`: L1 Cache Policy: I-Cache On, D-Cache Off (SRAM-Only Firmware)
- **Verification Skill**: ``e906-disassembly-check` (`csrw 0x7c1` with `0x1031`)`
- **Implementation Target**: ``targets/allwinner_e906/bsp/startup.S``
- **Key Contract Invariants**:
  * **Goal**: fast, low-jitter execution.
  * **Requirement**: AbstractX E906 firmware, its data, IPC rings and DMA buffers
  * **Mechanism**: invalidate caches via `CSR_MCOR` (0x7C2), then write `CSR_MHCR`
  * **Rationale**:
  * SYSMAP hardwires on-chip SRAM as Normal Cacheable and nothing can mark a

### `SPEC-E906-CACHE-03`: Verified Machine-Code Cache Maintenance
- **Verification Skill**: `unspecified`
- **Implementation Target**: ``targets/allwinner_e906/src/pmp.cpp``
- **Key Contract Invariants**:
  * **Requirement**: Software cache maintenance must use verified XuanTie line
  * **Operations**:
  * Clean (Write-Back): `.word 0x0297800b` (`dcache.cpa a5`)
  * Invalidate: `.word 0x02a7800b` (`dcache.iva a5`)
  * Clean & Invalidate: `.word 0x02b7800b` (`dcache.civa a5`)

### `SPEC-E906-IPC-01`: Pure Virtual `IRpmsg` Contract
- **Verification Skill**: ``host-tests` (`sim/test_e906_rpmsg_flow_control.cpp`), `e906-build-layout``
- **Implementation Target**: ``targets/allwinner_e906/include/hal/rpmsg.hpp``
- **Key Contract Invariants**:
  * **Requirement**: Application tasks, coroutines, and telemetry modules must
  * `init(const struct rpmsg_resource_table *rsc)`
  * `is_driver_ready()`
  * `register_endpoint(uint32_t addr, EndpointCallback cb, void *user_data)`
  * `announce_service(const char *name, uint32_t addr)`

### `SPEC-E906-IPC-02`: Standard Linux VirtIO RPMsg (`class Rpmsg`)
- **Verification Skill**: ``host-tests` (`sim/test_e906_rpmsg_endpoints.cpp`, `sim/test_e906_rpmsg_flow_control.cpp`), `e906-build-layout``
- **Implementation Target**: ``targets/allwinner_e906/src/rpmsg.cpp``
- **Key Contract Invariants**:
  * **Role**: Driver for Linux `virtio_rpmsg_bus` (`/dev/rpmsg0`).
  * **Target Memory**: DDR DRAM carveouts or shared on-chip SRAM Space 1 (`0x40020000`).
  * **Natural Vring Alignment Invariant**:
  * VirtIO ring descriptors (`VirtioDesc`, `VirtioAvail`, `VirtioUsedElem`, `VirtioUsed`)
  * Must be naturally aligned (2-byte for `VirtioAvail`, 4-byte for `VirtioUsed`, 8-byte for `VirtioDesc`)

### `SPEC-E906-IPC-03`: Lite-Metal Direct Driver (`class RpmsgLiteMetal`)
- **Verification Skill**: ``host-tests` (`sim/test_e906_rpmsg_flow_control.cpp`), `e906-build-layout``
- **Implementation Target**: ``targets/allwinner_e906/src/rpmsg.cpp``
- **Key Contract Invariants**:
  * **Role**: Direct driver for shared SRAM (`0x3FFC0000`) or UIO.
  * **Target Memory**: On-chip SRAM Space 0 / Space 1.
  * **Cache Policy**: Zero cache operations (NOP). Directly accesses
  * **Parity Invariant**: Adheres to identical natural vring alignment, `avail_pending()` bounds check,

### `SPEC-E906-IPC-04`: Zero-Copy TLP and Trace Endpoints over RPMsg in SRAM
- **Verification Skill**: ``host-tests` (`sim/test_e906_rpmsg_endpoints.cpp`), `sanitizer-gate`, `e906-build-layout`, `board-bringup` (not yet run)`
- **Implementation Target**: ``targets/allwinner_e906/src/rpmsg.cpp`, `include/hal/rpmsg.hpp`, `include/abstractx/trace/tracer.hpp`, `src/platform.cpp`, `include/tlp_ring.hpp`, `targets/allwinner_e906/CMakeLists.txt``
- **Key Contract Invariants**:
  * **Two channels, two purposes**: remoteproc `trace0` (`.trace_buffer`, debugfs) is
  * **Baseline**: cubie-a5e `testPingRpmsg` / `testPing` are proofs of concept that
  * **Purpose**: the E906 side of `[SPEC-TRACE-07]`. The trace engine writes CTF
  * **Zero Static Buffer Overhead**: Because the RPMsg transport supplies DMA buffers
  * **Memory**: the upper half of SRAM Space 1, core `0x40020000`–`0x4003FFFF`

### `SPEC-E906-CORO-01`: Zero-Polling Asynchronous RPMsg Awaiter
- **Verification Skill**: ``host-tests` (`sim/test_e906_rpmsg_flow_control.cpp`), `e906-build-layout``
- **Implementation Target**: ``targets/allwinner_e906/include/hal/rpmsg.hpp``
- **Key Contract Invariants**:
  * **Requirement**: Tasks waiting for incoming RPMsg packets MUST NOT busy-wait
  * **Mechanism**: Tasks suspend via `co_await rpmsg.async_receive()`:
  * If packet is already pending: `await_ready()` returns true (0 ns latency).
  * If queue is empty: Stores the coroutine handle, unsuppresses host kicks via
  * On resume: `await_resume()` suppresses redundant kicks via `driver->enable_rx_kick(false)`

### `SPEC-E906-CORO-02`: MSGBOX ISR-to-Dispatcher Wakeup
- **Verification Skill**: ``e906-disassembly-check` (`fsd f0, 144(sp)`, `lw t0, 128(sp); bltz t0`, `fld f0, 144(sp)`), `e906-build-layout``
- **Implementation Target**: ``targets/allwinner_e906/src/rpmsg.cpp``
- **Key Contract Invariants**:
  * **Requirement**: The MSGBOX ISR MUST NOT call `.resume()` directly. It MUST
  * **Mechanism**: PLIC IRQ 48 executes `fc_msgbox_doorbell_isr()`:
  * **Requirement**:
  * **Rationale**:

### `SPEC-E906-HAL-01`: Direct FIFO vs. DMA Threshold Balancing
- **Verification Skill**: `unspecified`
- **Implementation Target**: ``targets/allwinner_e906/src/{spi,i2c,uart,dma}.cpp``
- **Key Contract Invariants**:
  * **Requirement**: Peripheral drivers must balance latency vs. bandwidth:
  * Payloads below threshold use direct CPU FIFO writes (zero DMA overhead).
  * Payloads above threshold arm the dedicated co-processor DMA controller
  * **Thresholds**:
  * `hal::Spi`: `<= 4 bytes` (Fast FIFO) vs. `> 4 bytes` (DMA Awaiter)
