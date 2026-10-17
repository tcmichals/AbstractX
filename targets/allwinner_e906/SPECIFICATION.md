# AbstractX XuanTie E906 Target Specification & Architecture Matrix

This document is the **Single Source of Truth (SSOT)** for all architectural
contracts, memory layouts, hardware cache rules, and driver specifications
for the **Allwinner T527 / A523 XuanTie E906 RISC-V Co-Processor Target**
(`targets/allwinner_e906/`) in AbstractX.

Each requirement carries a unique **Design ID (`[SPEC-E906-*]`)** that is
directly traceable by implementation source files via `// @impl [SPEC-E906-*]`.

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

---

## 1. System Architecture Specifications (`SPEC-E906-ARCH`)

### `[SPEC-E906-ARCH-01]` Processor Core Architecture
* **Target Core**: T-Head / XuanTie E906 (32-bit RISC-V: RV32IMAFDC).
* **Operating Frequency**: Up to 200 MHz driven by the MCU CCU PLL.
* **Privilege Modes**: Machine (M) and User (U) modes only.
* **MMU Absence**: The E906 does NOT implement Supervisor mode virtual memory
  (no `satp` register, no Sv32/Sv39 page tables).
* **Implementation Target**: `targets/allwinner_e906/bsp/startup.S`

### `[SPEC-E906-ARCH-02]` Fixed Hardware System Memory Map (SYSMAP)
* **Hardware PMA Invariant**: Physical Memory Attributes (PMAs) are hardwired
  into ASIC logic gates at silicon synthesis time (from RTL `sysmap.h`).
* **Attributes**:
  * Peripheral MMIO (`< 0x3FFC0000`): Hardwired to **Strongly Ordered**
    (Non-cacheable). Automatically bypasses L1 cache in silicon.
  * Internal SRAM (`0x3FFC0000`–`0x40080000`):
    Hardwired to **Normal / Cacheable**.
  * External DDR DRAM (`0x40000000`–`0xC0000000`):
    Hardwired to **Normal / Cacheable**.
* **Immutability**: Memory attributes cannot be modified via instructions or
  runtime CSRs on this processor.
* **Implementation Target**: `targets/allwinner_e906/include/memory_map.h`

---

## 2. Memory Architecture & Determinism (`SPEC-E906-MEM`)

### `[SPEC-E906-MEM-01]` Pure Dedicated MCU SRAM Architecture (Zero TCM)
* **Silicon Reality**: There is **NO ITCM and NO DTCM** on the T527 E906.
  Addresses `0x00000000` (ITCM) and `0x00080000` (DTCM) do not exist in silicon.
* **Execution Windows** (E906 core address → Linux host physical address). These
  are the only SRAM windows `sunxi_rproc_da_to_va()` accepts
  (`E906_SRAM_SPACE{0,1}_DA` in `drivers/remoteproc/sunxi_rproc.h`):

  | Region | Core address | Host PA | Size | Use |
  | :--- | :--- | :--- | :--- | :--- |
  | SRAM Space 0 | `0x3FFC0000` | `0x07280000` | 256 KB | vectors, code, data, stack (linker `SRAM_FAST`, first 192 KB) |
  | Space 0 shared tail | `0x3FFF0000` | `0x072B0000` | 64 KB | host IPC: ping channel `+0x0000`, DRAM-SPSC control `+0x2000`, crash dump `+0xFF00` |
  | SRAM Space 1, lower half | `0x40000000` | `0x072C0000` | 128 KB | resource table, trace0, firmware IPC state (`REMAP_CTRL_REG[1]=1`, set by remoteproc) |
  | SRAM Space 1, upper half | `0x40020000` | `0x072E0000` | 128 KB | Linux RPMsg DMA pool: vrings + RPMsg buffers (`[SPEC-E906-IPC-04]`); firmware places nothing here |
  | DRAM carveout | `0x48100000` | `0x48100000` | 1 MB | not used by AbstractX (SRAM-only rule, `[SPEC-E906-CACHE-02]`); allowed by the layout check for legacy images |

* `0x07130000` is the RISC-V CFG register block (4 KB: `STA_ADD_REG` at `+0x204`,
  `WORK_MODE_REG` at `+0x248`), **not SRAM**. Nothing may be stored there.
* **Coroutine Frame Pool Right-Sizing**: The static coroutine frame pool
  (`g_coro_static_frame_pool`) resides in `.bss` inside `SRAM_FAST`. The E906 build
  configures `ABSTRACTX_CORO_POOL_SIZE = 4096` (4 KB), right-sized from the
  measured high-water mark of the multi-rate `gps_imu_app` task set (3,136 B
  measured + 25% headroom = 3,920 B, rounded to 4,096 B / +30.6% headroom),
  freeing 56 KB of SRAM Space 0 compared to the 60 KB default.
* **Zero-Copy Trace Pool Elision**: Because the E906 provides a native zero-copy
  RPMsg trace transport (`RpmsgTraceTransport`, `[SPEC-E906-IPC-04]`, `[SPEC-TRACE-07]`)
  writing directly into host RPMsg buffers in SRAM Space 1, the fallback static trace buffer
  pool is completely elided (`ABSTRACTX_STATIC_TRACE_PACKETS = 0`), freeing 2,112 B
  of SRAM Space 0 `.bss` (from 52,864 B down to 50,752 B; `g_tracer` dropped from 2,104 B to 32 B).
* **Lock-Free SPSC TLP Ring Right-Sizing**: The three inter-domain and telemetry
  TLP rings (`g_tx_ring`, `g_sensor_ring`, `g_telemetry_ring`) reside in `.bss`
  inside `SRAM_FAST`. Because the E906 RPMsg TLP endpoint batches at most 7 TLPs
  per 496-byte payload message (`[SPEC-E906-IPC-04]`), the ring capacity is
  right-sized from the default of 64 entries to the smallest power of 2
  accommodating a full RPMsg message batch (`ABSTRACTX_TLP_RING_CAPACITY = 8`).
  Each 64-byte aligned ring shrinks from 4,224 B (`64 * 64 + 128`) to 640 B
  (`8 * 64 + 128`), saving 3,584 B per ring and freeing an additional 10,752 B
  of SRAM Space 0 `.bss` (from 50,752 B down to 40,000 B).
* **Enforcement**: every E906 firmware target runs `tools/check_e906_layout.py`
  after linking; the build fails if a loadable segment is outside Space 0 (below
  the shared tail), the lower half of Space 1, or the DRAM carveout.
* **Source of truth**: the window constants in the linux-cubie driver and the
  silicon-tested cubie-a5e firmware (`firmware/common/arch_riscv/*.ld`,
  `firmware/common/include/*_protocol.h`). The shared-tail protocol headers in
  `include/` are verbatim copies of cubie-a5e's so its Linux tools interoperate.
* **Verified by**: `e906-build-layout`, `host-tests` (`sim_test_gps_imu_coro_pool`)
* **Implementation Target**: `targets/allwinner_e906/bsp/e906_sram.ld`, `bsp/e906_ddr.ld`, `targets/allwinner_e906/CMakeLists.txt`, `include/memory_map.h`

### `[SPEC-E906-MEM-02]` Control vs. Data Plane Memory Split
* **Control Plane in SRAM**: SPSC ring indices, descriptor states, and doorbell
  status flags MUST reside in on-chip SRAM (the Space 0 shared tail or Space 1).
* **Data Plane in SRAM**: trace and telemetry payloads are RPMsg buffers in
  the upper half of Space 1 (`[SPEC-E906-IPC-04]`). DRAM is not used while the
  D-cache is off; introducing DRAM payloads requires revisiting `[SPEC-E906-CACHE-02]`.
* **Target**: `targets/allwinner_e906/include/dram_spsc_protocol.h`

---

## 3. Hardware Cache Pipeline (`SPEC-E906-CACHE`)

### `[SPEC-E906-CACHE-01]` Vendor Instruction Extension Unlock
* **Requirement**: T-Head custom instruction extensions MUST be unlocked at boot
  to prevent Illegal Instruction exceptions (`mcause = 2`) during cache ops.
* **Mechanism**: In `startup.S`, set bit 22 (`THEADISAEE = 1`) and bit 15
  (`MM = 1`) in `CSR_MXSTATUS` (0x7C0).
* **Implementation Target**: `targets/allwinner_e906/bsp/startup.S`

### `[SPEC-E906-CACHE-02]` L1 Cache Policy: I-Cache On, D-Cache Off (SRAM-Only Firmware)
* **Goal**: fast, low-jitter execution.
* **Requirement**: AbstractX E906 firmware, its data, IPC rings and DMA buffers
  live entirely in on-chip SRAM (Space 0 and Space 1, `[SPEC-E906-MEM-01]`); no
  DRAM is used. Under that rule the D-cache MUST stay disabled.
* **Mechanism**: invalidate caches via `CSR_MCOR` (0x7C2), then write `CSR_MHCR`
  (0x7C1) = `IE | RS | BPE | BTB` (`0x1031`): I-cache, return stack, branch
  prediction and BTB on; `DE`, `WA`, `WB` off.
* **Rationale**:
  * SYSMAP hardwires on-chip SRAM as Normal Cacheable and nothing can mark a
    region uncached (cubie-a5e `docs/TINA_ALLWINNER_COPROCESSOR_SOURCE_OF_TRUTH.md`
    §10.3). With `DE = 1`, every E906 store that Linux or a DMA engine reads
    (trace0, crash dump, telemetry ring, DMA buffers) would need `dcache.cpa`,
    and every buffer they write would need `dcache.iva`.
  * On-chip SRAM is zero-wait-state, so the D-cache buys little speed here,
    while hit/miss and write-back eviction add timing variance. `DE = 0` gives
    deterministic load/store latency and zero maintenance cost.
  * The silicon-tested cubie-a5e firmware runs with `DE = 0`.
* **When DRAM is introduced**: enabling `DE` then requires `[SPEC-E906-CACHE-03]`
  maintenance on every shared buffer (32-byte aligned and sized) before the
  change lands.
* **Not yet verified on silicon**: the I-cache/branch-prediction setting
  (cubie-a5e leaves `mhcr` at reset defaults). Measure loop jitter with
  `mcycle` on the board.
* **Verified by**: `e906-disassembly-check` (`csrw 0x7c1` with `0x1031`)
* **Implementation Target**: `targets/allwinner_e906/bsp/startup.S`

### `[SPEC-E906-CACHE-03]` Verified Machine-Code Cache Maintenance
* **Requirement**: Software cache maintenance must use verified XuanTie line
  operations (32-byte cache lines) matching the Allwinner Tina SDK.
* **Operations**:
  * Clean (Write-Back): `.word 0x0297800b` (`dcache.cpa a5`)
  * Invalidate: `.word 0x02a7800b` (`dcache.iva a5`)
  * Clean & Invalidate: `.word 0x02b7800b` (`dcache.civa a5`)
* **Implementation Target**: `targets/allwinner_e906/src/pmp.cpp`

---

## 4. Polymorphic Inter-Processor Communication (`SPEC-E906-IPC`)

### `[SPEC-E906-IPC-01]` Pure Virtual `IRpmsg` Contract
* **Requirement**: Application tasks, coroutines, and telemetry modules must
  interface with IPC exclusively through the pure virtual `IRpmsg` interface:
  * `init(const struct rpmsg_resource_table *rsc)`
  * `is_driver_ready()`
  * `register_endpoint(uint32_t addr, EndpointCallback cb, void *user_data)`
  * `announce_service(const char *name, uint32_t addr)`
  * `poll()`
  * `reply(const RpmsgMessage &msg, const void *payload, uint16_t len)`
  * `is_rx_pending()`
* **Implementation Target**: `targets/allwinner_e906/include/hal/rpmsg.hpp`

### `[SPEC-E906-IPC-02]` Standard Linux VirtIO RPMsg (`class Rpmsg`)
* **Role**: Driver for Linux `virtio_rpmsg_bus` (`/dev/rpmsg0`).
* **Target Memory**: DDR DRAM carveouts (`0x48000000`+).
* **Cache Coherency Policy**:
  * Invalidate `avail` ring and incoming packet buffers via `dcache.iva` on RX.
  * Clean (write-back) response buffers and `used` ring via
    `dcache.cpa` on TX before kicking MSGBOX Channel 0.
    on TX before kicking MSGBOX Channel 0.
* **Verified by**: `host-tests` (`sim/test_e906_rpmsg_endpoints.cpp` runs the real `rpmsg.cpp`), `e906-build-layout`
* **Implementation Target**: `targets/allwinner_e906/src/rpmsg.cpp`

### `[SPEC-E906-IPC-03]` Lite-Metal Direct Driver (`class RpmsgLiteMetal`)
* **Role**: Direct driver for shared SRAM (`0x3FFC0000`) or UIO.
* **Target Memory**: On-chip SRAM Space 0 / Space 1.
* **Cache Policy**: Zero cache operations (NOP). Directly accesses
  memory at bus speed with zero cache maintenance overhead.
  at bus speed with zero cache maintenance overhead.
* **Implementation Target**: `targets/allwinner_e906/src/rpmsg.cpp`

### `[SPEC-E906-IPC-04]` Zero-Copy TLP and Trace Endpoints over RPMsg in SRAM
* **Two channels, two purposes**: remoteproc `trace0` (`.trace_buffer`, debugfs) is
  the **low-speed processor channel**: boot log, exceptions, crash dumps. RPMsg is
  the **high-speed data channel** for AbstractX: 64-byte TLPs and CTF/telemetry
  queues, encoded compactly and written in place. Never route bulk data through
  `trace0`, and never depend on RPMsg for crash reporting.
* **Baseline**: cubie-a5e `testPingRpmsg` / `testPing` are proofs of concept that
  RPMsg and UIO work on silicon (ring roles, `translate_da`, kick channel). AbstractX
  extends them to its model (zero-copy acquire/commit, TLP and CTF queues).
* **Purpose**: the E906 side of `[SPEC-TRACE-07]`. The trace engine writes CTF
  packets directly into RPMsg buffers that live in on-chip SRAM; sending is a
  used-ring update plus a message box kick. No memcpy on the E906, no DRAM, no
  cache maintenance (`[SPEC-E906-CACHE-02]`). Replaces the old write to fixed
  `0x07131000`, which is outside both SRAM windows and overwrote itself.
* **Zero Static Buffer Overhead**: Because the RPMsg transport supplies DMA buffers
  directly to the trace engine (`[SPEC-TRACE-07]`), `g_tracer` configures
  `ABSTRACTX_STATIC_TRACE_PACKETS = 0`, ensuring that no unused static ping-pong
  buffers are allocated in SRAM `.bss`.
* **Memory**: the upper half of SRAM Space 1, core `0x40020000`–`0x4003FFFF`
  = host `0x072E0000`–`0x072FFFFF` (128 KB), is the Linux remoteproc DMA pool
  (`shared-dma-pool`, `no-map`) from an AbstractX device-tree overlay derived from
  cubie-a5e `cubie-a5e-rpmsg-sram.dtso` (which proved RPMsg with an SRAM pool but
  gives Linux all of Space 1). Linux allocates both vrings and all RPMsg buffers
  there (the resource table uses `FW_RSC_ADDR_ANY`). The linker keeps firmware out
  of it (region `IPC`, nothing placed), and `tools/check_e906_layout.py` rejects
  any load segment there.
* **Sizes**: `VRING_NUM_DESCS = 16`, `RPMSG_BUFFER_SIZE = 512` (16-byte
  `rpmsg_hdr` + **496-byte payload** = one CTF packet). Linux allocates
  2 × 16 buffers = 16 KB plus the vrings, well inside 128 KB.
* **Address translation**: descriptor addresses written by Linux are host
  physical addresses; the E906 converts them with the proven cubie-a5e
  `translate_da()` (host `0x072C0000`+ → core `0x40000000`+, host `0x07280000`+
  → core `0x3FFC0000`+).
* **Two endpoints, one mailbox channel** (option 1 of the 2026-10-14 review;
  keeps cubie-a5e decision D11, `mboxes = <&msgbox 9>`):

  | Endpoint | Address | Carries | Linux device |
  | :--- | :--- | :--- | :--- |
  | `abstractx-tlp` | 1024 | 64-byte TLPs, both directions, batched up to 7 per message (`asp_tlp64_t`, 7 × 64 = 448 of 496 payload bytes) | `/dev/rpmsgN` |
  | `abstractx-trace` | 1025 | CTF (barectf) packets, E906 → Linux, one packet per message (≤ 496 B) | `/dev/rpmsgM` |

  Both share the 16 host buffers that Linux posts in vring[0]. An alternative with
  separate pools (two virtio devices) or a dedicated low-latency TLP doorbell
  (second mailbox channel + SRAM ring, needs a Linux mailbox client) is deferred
  until board measurements show RPMsg latency is insufficient.
* **Buffer budget (no head-of-line blocking)**: the trace endpoint may borrow a
  host buffer only while more than `kTlpReserve = 4` free buffers remain; the TLP
  endpoint may use any free buffer. Several buffers may be outstanding at once
  (`tx_acquire()` returns a token naming its descriptor; `tx_commit()` takes it),
  so the trace engine holding a half-filled packet never blocks TLPs. Virtio
  allows out-of-order completion (the `used` entry names the descriptor).
* **Host address**: each endpoint learns its Linux peer address from the first
  message Linux sends to it; until then it cannot transmit (trace events and TLPs
  are counted as dropped, never blocking).
* **Transport API** (implements `[SPEC-TRACE-07]`):
  1. `Rpmsg::tx_acquire(min_free_after)`: take the next free buffer from vring[0]
     if at least `min_free_after` would remain free; returns `{payload span,
     descriptor}` or empty. Never blocks.
  2. The caller writes the payload in place (CTF events, or `asp_tlp64_t` slots).
  3. `Rpmsg::tx_commit(token, used, src, dst)`: fill `rpmsg_hdr`, release fence,
     publish the descriptor in the `used` ring, release fence, bump `used.idx`,
     kick message box Channel1 (Linux channel 9).
* **TLP endpoint API**: `emplace()` returns an in-place `asp_tlp64_t*` slot in the
  current batch (nullptr if no buffer); `send(const Tlp64&)` copies an
  already-built TLP into a slot (one 64-byte copy, for callers that build TLPs by
  value); `flush()` commits a partial batch (called every `step()` via
  `platform_poll_network()`). Received host batches are pushed into an optional
  ingress `SpscTlpRing`; TLPs that do not fit are counted as dropped.
* **Message encoding**: TLP messages are a whole number of 64-byte TLPs (length
  multiple of 64); a length that is not is rejected by the receiver.
* **Context**: main cooperative loop only; never from an ISR.
* **TLP Ring Capacity Parity**: Ingress and egress TLP rings (`g_tx_ring`,
  `g_sensor_ring`, `g_telemetry_ring`) are sized with `ABSTRACTX_TLP_RING_CAPACITY = 8`,
  matching the 7-TLP RPMsg batch payload (`7 * 64 = 448` B payload out of 496 B available)
  and eliminating 10,752 B of unneeded queue buffering in SRAM Space 0.
* **Linux side**: one reader per endpoint opens `/dev/rpmsg_ctrl0`, creates the
  endpoint (`RPMSG_CREATE_EPT_IOCTL`, `dst` = 1024 or 1025), sends one message so
  the E906 learns its address, then reads. The trace reader forwards each message
  as one UDP datagram to the Studio (`127.0.0.1:9870`); the TLP reader splits
  messages into 64-byte TLPs. The kernel copies into user space; nothing on the
  E906 copies.
* **Status**: E906 side implemented and host-tested
  (`sim/test_e906_rpmsg_endpoints.cpp`); Linux side `tools/e906_rpmsg_bridge.py`
  (`trace` and `tlp` modes) host-tested (`tests/test_e906_rpmsg_bridge.py`; ioctl
  number checked against `linux/rpmsg.h`). Device-tree overlay
  `apps/gps_imu_app/platforms/allwinner_e906/cubie-a5e-abstractx-rpmsg-sram.dtso`
  (pool at host `0x072E0000`, 128 KB) compiles with `dtc`. Nothing verified on
  silicon.
* **Verified by**: `host-tests` (`sim/test_e906_rpmsg_endpoints.cpp`), `sanitizer-gate`, `e906-build-layout`, `board-bringup` (not yet run)
* **Implementation Target**: `targets/allwinner_e906/src/rpmsg.cpp`, `include/hal/rpmsg.hpp`, `include/abstractx/trace/tracer.hpp`, `src/platform.cpp`, `include/spsc_tlp_ring.hpp`, `targets/allwinner_e906/CMakeLists.txt`

---

## 5. Event-Driven Coroutines & ISR Binding (`SPEC-E906-CORO`)

### `[SPEC-E906-CORO-01]` Zero-Polling Asynchronous RPMsg Awaiter
* **Requirement**: Tasks waiting for incoming RPMsg packets MUST NOT busy-wait
  in polling loops.
* **Mechanism**: Tasks suspend via `co_await rpmsg.async_receive()`:
  * If packet is already pending: `await_ready()` returns true (0 ns latency).
  * If queue is empty: Stores the coroutine handle, enables MSGBOX Channel 1 RX
    interrupt, and suspends until the cooperative dispatcher resumes it.
* **Implementation Target**: `targets/allwinner_e906/include/hal/rpmsg.hpp`

### `[SPEC-E906-CORO-02]` MSGBOX ISR-to-Dispatcher Wakeup
* **Requirement**: The MSGBOX ISR MUST NOT call `.resume()` directly. It MUST
  acknowledge the interrupt and post the suspended coroutine handle through
  the ISR-safe dispatcher queue; `abstractx::step()` MUST resume it from the
  cooperative context on the next dispatcher drain.
* **Mechanism**: PLIC IRQ 48 executes `fc_msgbox_doorbell_isr()`:
  1. Clears hardware MSGBOX interrupt status.
  2. Transfers `s_rpmsg_coroutine_handle` to `IsrDispatcher::post()`.
  3. Leaves all coroutine resumption to `Dispatcher::process()`.
* **Implementation Target**: `targets/allwinner_e906/src/rpmsg.cpp`

---

### `[SPEC-E906-CRASH-01]` Trap Classification & Crash Dump
* **Requirement**: The trap entry (`bsp/startup.S`) MUST classify a trap by bit 31 of **`mcause`** (1 = interrupt, 0 = synchronous exception). Interrupts go to `riscv_trap_dispatcher` and return with `mret`; every synchronous exception MUST go to `hal_crash_dispatcher`, which records the `CrashFrame` and never returns.
* **Rationale**: `mstatus` bit 31 is the SD summary bit, which is always 1 here because `_start` sets `mstatus.FS = Dirty`. Branching on it sent faults down the interrupt path, where they were ignored and `mret` re-executed the faulting instruction forever, with no crash dump.
* **Verification**: Disassemble `default_trap_entry` and confirm the branch operand is loaded from the saved `mcause` slot (`128(sp)`).
* **Verified by**: `e906-disassembly-check` (`lw t0,128(sp); bltz t0`)

## 6. Threshold-Balanced Peripheral HAL (`SPEC-E906-HAL`)

### `[SPEC-E906-HAL-01]` Direct FIFO vs. DMA Threshold Balancing
* **Requirement**: Peripheral drivers must balance latency vs. bandwidth:
  * Payloads below threshold use direct CPU FIFO writes (zero DMA overhead).
  * Payloads above threshold arm the dedicated co-processor DMA controller
    (Channels 8..15) and suspend calling coroutines.
* **Thresholds**:
  * `hal::Spi`: `<= 4 bytes` (Fast FIFO) vs. `> 4 bytes` (DMA Awaiter)
  * `hal::I2c`: `<= 4 bytes` (Fast FIFO) vs. `> 4 bytes` (DMA Awaiter)
  * `hal::Uart`: `<= 32 bytes` (Fast FIFO) vs. `> 32 bytes` (DMA Awaiter)
* **Implementation Target**: `targets/allwinner_e906/src/{spi,i2c,uart,dma}.cpp`

---

## 7. Binary Telemetry & Observability (`SPEC-E906-TRACE`)


## 8. Traceability Matrix

| Design Requirement | Description | Primary Implementation File |
| :--- | :--- | :--- |
| `[SPEC-E906-ARCH-01]` | E906 RV32IMAFDC Core (No MMU) | `bsp/startup.S` |
| `[SPEC-E906-ARCH-02]` | Hardwired ASIC SYSMAP PMAs | `include/memory_map.h` |
| `[SPEC-E906-MEM-01]`  | Dedicated MCU SRAM (Zero TCM) | `bsp/e906_sram.ld` |
| `[SPEC-E906-MEM-02]`  | SPSC in SRAM / DDR Buffers | `dram_spsc_protocol.h` |
| `[SPEC-E906-CACHE-01]`| THEADISAEE Vendor Opcode Unlock | `bsp/startup.S` |
| `[SPEC-E906-CACHE-02]`| I-Cache On, D-Cache Off (0x1031) | `bsp/startup.S` |
| `[SPEC-E906-CACHE-03]`| Tina SDK Machine Opcodes | `src/pmp.cpp` |
| `[SPEC-E906-IPC-01]`  | Pure Virtual `IRpmsg` | `include/hal/rpmsg.hpp` |
| `[SPEC-E906-IPC-02]`  | Linux VirtIO RPMsg with D-Cache | `src/rpmsg.cpp` |
| `[SPEC-E906-IPC-03]`  | Lite-Metal Direct SRAM Driver | `src/rpmsg.cpp` |
| `[SPEC-E906-IPC-04]`  | Zero-Copy TLP + Trace Endpoints over RPMsg | `src/rpmsg.cpp`, `src/rpmsg_*` |
| `[SPEC-E906-CORO-01]` | Async `async_receive()` | `include/hal/rpmsg.hpp` |
| `[SPEC-E906-CORO-02]` | MSGBOX Doorbell ISR Waking | `src/rpmsg.cpp` |
| `[SPEC-E906-HAL-01]`  | FIFO vs. DMA Balancing | `src/{spi,uart,i2c}.cpp` |
