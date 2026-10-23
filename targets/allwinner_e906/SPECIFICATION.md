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
* **Target Core**: T-Head / XuanTie E906, RV32IMAFCX: single-precision FPU, **no D**
  (`misa = 0x40901125`, measured by cubie-a5e). Firmware is built with
  `-march=rv32imafc... -mabi=ilp32f`; a double instruction (`fld`/`fsd`/`*.d`) is an illegal
  instruction on this core.
* **Operating Frequency**: Up to 200 MHz driven by the MCU CCU PLL.
* **Privilege Modes**: Machine (M) and User (U) modes only.
* **MMU Absence**: The E906 does NOT implement Supervisor mode virtual memory
  (no `satp` register, no Sv32/Sv39 page tables).
* **Refines**: `[SPEC-ARCH-04]`
* **Method**: `Analysis`
* **Verified by**: `host-tests` (`tests/test_adversarial_invariants.py`)
* **Rationale**: until 2026-10-21 the toolchain file built `rv32imafdc`/`ilp32d`. The first
  interrupt ever taken (CLIC line 17) hit `fsd` in the trap entry's FP save and locked the core.
* **Implementation Target**: `targets/allwinner_e906/bsp/startup.S`, `cmake/toolchains/riscv-none-elf.cmake`

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
* **Refines**: `[SPEC-ARCH-04]`
* **Method**: `Analysis`
* **Verified by**: `host-tests` (`tests/test_adversarial_invariants.py`)
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
  | SRAM Space 1, lower half | `0x40000000` | `0x072C0000` | 128 KB | resource table, trace0, firmware IPC state, cold code (`.text_cold`) and profiler/timeline rings (`.sram_c`) (`REMAP_CTRL_REG[1]=1`, set by remoteproc) |
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
* **Cold Code in SRAM Space 1 (2026-10-22)**: SRAM_FAST reached 99.3 % (1.3 KB free). Code that
  runs only while a MicroPython script is being compiled (the lexer, parser, compiler, bytecode
  emitter, scope and number parsing, `mpz`) is linked into `.text_cold` in the lower half of
  Space 1: same on-chip SRAM, PMP RWX, loaded by remoteproc like the resource table. Hot paths
  (interrupts, `Dispatcher`, the VM, RPMsg) stay in `SRAM_FAST`. Buffers that need no zeroing
  (profiler sample ring, timeline event ring) go to `.sram_c` (NOLOAD, Space 1).
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
  * `enable_rx_kick(bool enable)`
  * `get_rx_count()`, `get_tx_count()`
  * `get_ring_errors()`, `get_tx_full_waits()`, `get_mbox_full_waits()`, `get_host_kicks()`
* **Verified by**: `host-tests` (`sim/test_e906_rpmsg_flow_control.cpp`), `e906-build-layout`
* **Implementation Target**: `targets/allwinner_e906/include/hal/rpmsg.hpp`

### `[SPEC-E906-IPC-02]` Standard Linux VirtIO RPMsg (`class Rpmsg`)
* **Role**: Driver for Linux `virtio_rpmsg_bus` (`/dev/rpmsg0`).
* **Target Memory**: DDR DRAM carveouts or shared on-chip SRAM Space 1 (`0x40020000`).
* **Natural Vring Alignment Invariant**:
  * VirtIO ring descriptors (`VirtioDesc`, `VirtioAvail`, `VirtioUsedElem`, `VirtioUsed`)
    MUST NOT use `__attribute__((packed))`. Packing causes the RISC-V compiler to emit
    split byte loads (`lbu`/`lbu`) for 16-bit indices, which tear when Linux stores
    `avail->idx` concurrently, producing corrupted indices (e.g. 0x01FF -> 0x0200 read as 0x02FF)
    and replaying ~65k stale descriptors.
  * Must be naturally aligned (2-byte for `VirtioAvail`, 4-byte for `VirtioUsed`, 8-byte for `VirtioDesc`)
    validated by static asserts.
* **Torn Index Detection & Ring Bounds (`avail_pending`)**:
  * The pending count is checked via `avail_pending()`: if $(avail\_idx - last\_avail\_idx) > num$,
    the index is corrupted or torn; the driver increments `s_ring_errors` and rejects processing.
  * Ring slots must be computed with a power-of-two bitmask (`idx & mask`) instead of division.
* **Flow Control & Non-Dropping Backpressure**:
  * **Reply buffer starvation**: In `reply()`, if host free buffers in `s_tx_vq` are exhausted
    ($free\_bufs == 0$), the driver MUST NOT drop the packet. It must clear `s_tx_vq.used->flags = 0`,
    increment `s_tx_full_waits`, notify the host, drain incoming host doorbells, and wait until
    buffers are restored.
  * **Hardware Mailbox FIFO space**: When kicking the host (`notify_host()`), the driver must poll in a
    loop until hardware FIFO space is available (`s_mbox_full_waits`), never dropping a doorbell.
  * **Doorbell FIFO Draining**: In `poll()`, all pending incoming doorbells across both Channel 0
    and Channel 1 must be drained in a tight loop (`s_host_kicks`) to avoid filling the 8-slot
    hardware FIFO and overflowing Linux's software mailbox queue (`MBOX_TX_QUEUE_LEN`).
* **VirtIO Kick Suppression**:
  * In polling mode, `s_tx_vq.used->flags` and `s_rx_vq.used->flags` default to `VRING_USED_F_NO_NOTIFY`
    to suppress redundant host kicks. `enable_rx_kick(bool)` toggles `s_rx_vq.used->flags`.
* **Verified by**: `host-tests` (`sim/test_e906_rpmsg_endpoints.cpp`, `sim/test_e906_rpmsg_flow_control.cpp`), `e906-build-layout`
* **Implementation Target**: `targets/allwinner_e906/src/rpmsg.cpp`

### `[SPEC-E906-IPC-03]` Lite-Metal Direct Driver (`class RpmsgLiteMetal`)
* **Role**: Direct driver for shared SRAM (`0x3FFC0000`) or UIO.
* **Target Memory**: On-chip SRAM Space 0 / Space 1.
* **Cache Policy**: Zero cache operations (NOP). Directly accesses
  memory at bus speed with zero cache maintenance overhead.
* **Parity Invariant**: Adheres to identical natural vring alignment, `avail_pending()` bounds check,
  non-dropping mailbox FIFO send, incoming doorbell queue draining, and kick suppression contracts
  as `[SPEC-E906-IPC-02]`.
* **Verified by**: `host-tests` (`sim/test_e906_rpmsg_flow_control.cpp`), `e906-build-layout`
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
* **Refines**: `[SPEC-TRACE-07]`
* **Method**: `Test`
* **Verified by**: `host-tests` (`sim/test_e906_rpmsg_endpoints.cpp`), `sanitizer-gate`, `e906-build-layout`, `board-bringup` (not yet run)
* **Implementation Target**: `targets/allwinner_e906/src/rpmsg.cpp`, `include/hal/rpmsg.hpp`, `include/abstractx/trace/tracer.hpp`, `src/platform.cpp`, `include/spsc_tlp_ring.hpp`, `targets/allwinner_e906/CMakeLists.txt`

---

## 5. Event-Driven Coroutines & ISR Binding (`SPEC-E906-CORO`)


### `[SPEC-E906-IPC-05]` Trace Endpoint Sends Only While a Host Listens (3 s Hello Lease)
* **Requirement**: the trace endpoint sends only while Linux has a reader on it: every message the
  host sends to endpoint 1025 (the 4-byte hello) renews a 3 s lease; without a lease
  `RpmsgTraceTransport::acquire()` returns no buffer. Hosts that read the trace endpoint
  (`e906_rpmsg_bridge.py trace`, the board tests' `Plane`) send the hello when they open it and
  then once a second. While no host listens, the runtime empties the profiler and timeline rings
  without counting drops (`platform_trace_listening()`, [SPEC-TRACE-09], [SPEC-TRACE-10]).
* **Rationale (2026-10-22)**: the first profiler firmware kept sending ~20 packets a second to the
  host address it learned from an earlier hello after that reader had closed; Linux logged
  `virtio_rpmsg_bus: msg received with no recipient` for each on the serial console
  (`loglevel=8`), the console became unusable and the board was rebooted twice. The status report
  had the same flaw at 1 packet a second.
* **Verified by**: `e906-board-selftest` (`tests/board/test_e906_trace.py`: no new "no recipient"
  lines once the reader has closed)
* **Implementation Target**: `targets/allwinner_e906/src/rpmsg_endpoints.cpp`, `targets/allwinner_e906/src/platform.cpp`, `tools/e906_rpmsg_bridge.py`, `tests/board/e906_link.py`
### `[SPEC-E906-CORO-01]` Zero-Polling Asynchronous RPMsg Awaiter
* **Requirement**: Tasks waiting for incoming RPMsg packets MUST NOT busy-wait
  in polling loops.
* **Mechanism**: Tasks suspend via `co_await rpmsg.async_receive()`:
  * If packet is already pending: `await_ready()` returns true (0 ns latency).
  * If queue is empty: Stores the coroutine handle, unsuppresses host kicks via
    `driver->enable_rx_kick(true)` so Linux rings the hardware doorbell on packet arrival,
    enables MSGBOX Channel 1 RX interrupt, and suspends until the cooperative dispatcher resumes it.
  * On resume: `await_resume()` suppresses redundant kicks via `driver->enable_rx_kick(false)`
    during active draining, resets handle, and drains the pending packets.
* **Verified by**: `host-tests` (`sim/test_e906_rpmsg_flow_control.cpp`), `e906-build-layout`
* **Implementation Target**: `targets/allwinner_e906/include/hal/rpmsg.hpp`

### `[SPEC-E906-CORO-02]` MSGBOX ISR-to-Dispatcher Wakeup
* **Requirement**: The MSGBOX ISR MUST NOT call `.resume()` directly. It MUST
  acknowledge the interrupt and post the suspended coroutine handle through
  the ISR-safe dispatcher queue; `abstractx::step()` MUST resume it from the
  cooperative context on the next dispatcher drain.
* **Mechanism**: CLIC line 17 (`[SPEC-E906-IRQ-04]`) executes `fc_msgbox_doorbell_isr()`:
  1. Masks and clears the MSGBOX read interrupt (the line is level-triggered).
  2. Sets the doorbell flag and transfers `s_rpmsg_coroutine_handle` to `IsrDispatcher::post()`.
  3. Leaves all coroutine resumption to `Dispatcher::process()`.
* **Verified by**: `e906-board-selftest` (`tests/board/test_e906_irq.py`)
* **Implementation Target**: `targets/allwinner_e906/src/rpmsg.cpp`

---

## 5a. Interrupt Delivery (`SPEC-E906-IRQ`)

Measured on the Cubie A5E (T527) on 2026-10-21 through the REPL (`peek`/`poke`, CLIC IE left 0)
and by `testFreeRTOS` (cubie-a5e, 2026-10-20). Numbers not in this table are not verified.

| Source | GIC | CLIC line | Route | Evidence |
|---|---|---|---|---|
| Core timer (`mtimecmp`) | - | 7 | direct | testFreeRTOS tick |
| MSGBOX RV read | - | 17 | direct | testFreeRTOS ping 2000/2000 |
| DMAC channels 8-15 (`DMAC_IRQ1_NS`) | - | 88 | `DMA_MCU_IRQ_CTRL` | manual Table 2-16, not measured |
| UART3 | 37 | 90 | S_INTC `GINTC_CONFIG_REG0` bit 5 | IP 0→1 with bit set, RX data |
| TWI1 | 43 | 91 | S_INTC `GINTC_CONFIG_REG0` bit 11 | IP 0→1 after START with INT_EN |

### `[SPEC-E906-IRQ-01]` CLIC Mode and Dispatch
* **Requirement**: the E906 has a T-Head CLIC at `0xE0800000` and no PLIC. `mtvec` MUST be written
  with MODE = 3 (CLIC, non-vectored) and a 64-byte aligned base; `mie` bits are ignored in this
  mode. A line is enabled by writing 1 to byte +1 (IE) of `0xE0801000 + 4*line`.
* **Requirement**: the dispatcher selects the handler by `mcause & 0xFFF` (the E906 mirrors
  MPP/MPIE/MPIL into `mcause[29:16]`). Handlers are attached per line at run time
  (`hal::irq::attach(line, fn, ctx)`); a line may carry several handlers (shared S_INTC group lines).
  A line that fires with no handler is disabled and counted, never a hang.
* **Requirement**: every line keeps a 32-bit interrupt count readable from the firmware
  (`hal::irq::count(line)`), so a board test can prove the interrupt path ran.
* **Verified by**: `e906-board-selftest` (`tests/board/test_e906_irq.py`), `e906-build-layout`
* **Implementation Target**: `targets/allwinner_e906/include/hal/clic.hpp`, `targets/allwinner_e906/src/irq_dispatcher.cpp`, `targets/allwinner_e906/bsp/startup.S`

### `[SPEC-E906-IRQ-02]` Main-Domain Peripherals via the S_INTC Group Router
* **Requirement**: main-domain peripherals reach the CLIC only through the S_INTC group router
  (`0x07021000`, `GINTC_CONFIG_REGn` at `+0xC0 + 4n`): GIC IRQ `g` (32..159) is bit `(g-32) % 32` of
  register `(g-32) / 32`, and arrives on CLIC line `90 + (g-32)/8`. Eight GIC sources share one
  CLIC line, so each handler checks its own peripheral's status register.
* **Requirement**: routing is done with `hal::clic::route_gic(g, on)` (read-modify-write under an
  interrupt guard); nothing else writes `GINTC_CONFIG_REGn`.
* **Verified by**: `e906-board-selftest` (`tests/board/test_e906_irq.py`)
* **Implementation Target**: `targets/allwinner_e906/include/hal/clic.hpp`

### `[SPEC-E906-IRQ-03]` Core Timer: `mtime` at 24 MHz, `mcycle` at 200 MHz
* **Requirement**: `mtime` is at `0xE000BFF8` and `mtimecmp` at `0xE0004000` (not `0x1400xxxx`), both
  counting the 24 MHz oscillator; `hal::Timer` ticks are `mtime` ticks. `mcycle` counts the 200 MHz
  core clock (measured: `mcycle`/200000 per ms matched 24,000,410 `mtime` ticks over 1 s).
* **Requirement**: the timer interrupt (line 7) is level while `mtime >= mtimecmp`; the handler
  re-arms or parks `mtimecmp` at all-ones before returning, never leaves it matched.
* **Verified by**: `e906-board-selftest` (`tests/board/test_e906_irq.py`)
* **Implementation Target**: `targets/allwinner_e906/src/timer.cpp`

### `[SPEC-E906-IRQ-04]` RPMsg Doorbell Interrupt (No Polling)
* **Requirement**: Linux kicks arrive on MSGBOX channels 0 and 1 (CLIC line 17, level until the
  FIFO is read). The ISR masks the read interrupt, clears status, sets a doorbell flag, counts,
  and posts the waiting coroutine. Consumers call `hal::rpmsg_take_doorbell()` and, after draining
  the vrings, `hal::rpmsg_arm_doorbell()` to unmask.
* **Requirement**: no application or platform loop calls `poll()` unconditionally; RX vrings are
  read only after a doorbell (or when an awaiter's `await_ready` sees pending descriptors).
* **Verified by**: `e906-board-selftest` (`tests/board/test_e906_irq.py`), `host-tests` (`tests/test_adversarial_invariants.py`)
* **Implementation Target**: `targets/allwinner_e906/src/rpmsg.cpp`, `targets/allwinner_e906/src/platform.cpp`

### `[SPEC-E906-IRQ-05]` Runtime Status: Cycle Clock, Idle Time and Per-Line ISR Time
* **Requirement**: the cycle clock for [SPEC-TRACE-08] is `mcycle` (200 MHz, low 32 bits).
  `platform_idle_wait()` reads it before and after `wfi` and reports the difference minus the ISR
  cycles taken in that window as idle time.
* **Requirement**: `riscv_trap_dispatcher` adds the `mcycle` cost of each line's handlers to that
  line's cycle total next to its count ([SPEC-E906-IRQ-01]). `hal::irq::describe(line, name, errors)`
  gives a line a short name and an optional error counter owned by its driver; the platform names
  `timer` (7) and `msgbox` (17).
* **Requirement**: `platform_runtime_stats()` reports up to six lines that fired, most interrupts
  first. The idle loop arms a timer wake for the next status deadline so a report goes out once a
  second even when nothing else happens.
* **Verified by**: `e906-board-selftest` (`tests/board/test_e906_trace.py`), `e906-build-layout`
* **Implementation Target**: `targets/allwinner_e906/src/platform.cpp`, `targets/allwinner_e906/src/irq_dispatcher.cpp`, `targets/allwinner_e906/include/hal/clic.hpp`

---


### `[SPEC-E906-IRQ-06]` Profiler Sample Timer and Interrupt Spans
* **Requirement**: the core timer comparator serves a third deadline, the profiler sample period
  (`profile_hz`, [SPEC-TRACE-09]), beside the coroutine sleep and the wfi wake. On each sample the
  timer handler records `mepc` of the interrupted code, which `riscv_trap_dispatcher` keeps for the
  handler (interrupts do not nest, so it is the code that was running).
* **Requirement**: a timer interrupt that only took a sample (no coroutine deadline, no wake
  deadline) is invisible to the code it interrupted: if that was the idle `wfi`, the trap returns
  to the `wfi` instead of past it (`g_irq_sample_only`, read and cleared in the trap return of
  `startup.S`). "Was the idle `wfi`" means `mepc` is `e906_idle_wfi` **or `e906_idle_wfi + 4`**:
  this E906 reports the instruction after the `wfi` (measured 2026-10-22: `mepc = 0x3FFD4AA2` with
  the `wfi` at `0x3FFD4A9E`; the first build checked only `== e906_idle_wfi` and the idle loop still
  woke ~1050 times a second), so the idle loop does not wake 1000 times a second, idle stays one span and busy %
  does not include the profiler's loop wakes. It is not recorded as a timeline span (the profiler
  shows it). Any other interrupt still ends the `wfi` as before ([SPEC-E906-IRQ-01]).
* **Requirement**: `dispatch_clic_line` records an interrupt span (line, start cycle, cycles) for
  [SPEC-TRACE-10] from the same measurement it charges to [SPEC-E906-IRQ-05].
* **Requirement**: the sample ring (256 x 8 B) and timeline ring (512 x 16 B) live in `.sram_c`
  (SRAM Space 1, [SPEC-E906-MEM-01]).
* **Verified by**: `e906-build-layout`, `e906-board-selftest` (`tests/board/test_e906_trace.py`)
* **Implementation Target**: `targets/allwinner_e906/src/timer.cpp`, `targets/allwinner_e906/src/irq_dispatcher.cpp`, `targets/allwinner_e906/src/platform.cpp`
### [SPEC-E906-CRASH-01] Trap Classification, FPU State Preservation & Crash Dump
* **Requirement**:
  1. The trap entry (`bsp/startup.S`) MUST allocate a 416-byte stack frame (`CrashFrame`) and save all 31 integer GPRs (`x1`..`x31`), 4 CSRs (`mepc`, `mcause`, `mtval`, `mstatus`), all 32 hardware floating-point registers (`f0`..`f31`, single precision `fsw` into 8-byte slots: the E906 has no D, `ilp32f`), and `fcsr` BEFORE branching to or calling any C/C++ code.
  2. The trap entry MUST classify the trap by bit 31 of **`mcause`** (1 = interrupt, 0 = synchronous exception).
  3. Interrupts branch to `.Lhandle_irq`, dispatch to `riscv_trap_dispatcher(mcause, mepc)`, completely restore all FP registers (`f0`..`f31`), `fcsr`, CSRs, and integer GPRs, deallocate the 416-byte stack frame, and return with `mret`.
  4. Synchronous exceptions MUST pass the `CrashFrame*` (`sp`) in `a0` to `hal_crash_dispatcher`, which records the full GPR, CSR, and FPR crash dump to `Trace` and SRAM and halts.
* **Rationale**:
  - In `ilp32d` ABI, C/C++ compilers generate floating-point instructions in any subroutine and can clobber caller-saved FP registers (`ft0`..`ft11`, `fa0`..`fa7`). Without saving FP registers in the trap prologue, an asynchronous interrupt clobbers live floating-point computations in interrupted coroutines (e.g. sensor fusion math), causing silent corruption upon `mret`.
  - In synchronous exceptions, saving FP registers ensures the crash autopsy dump captures the full FPU state at the moment of the fault, and prevents formatting routines in `hal_crash_dispatcher` from destroying the fault context.
  - Branching on `mcause` bit 31 is mandatory because `mstatus` bit 31 is the SD summary bit, which is always 1 when `mstatus.FS = Dirty`.
* **Verified by**: `e906-disassembly-check` (`fsw f0, 144(sp)`, `lw t0, 128(sp); bltz t0`, `flw f0, 144(sp)`, no double-precision instruction anywhere in the ELF), `e906-build-layout`, `e906-board-selftest` (`tests/board/test_e906_irq.py`: an interrupt is taken and returns)
* **Implementation Target**: `targets/allwinner_e906/bsp/startup.S`, `targets/allwinner_e906/include/hal/crash.hpp`, `targets/allwinner_e906/src/crash.cpp`

## 6. Threshold-Balanced Peripheral HAL (`SPEC-E906-HAL`)

### `[SPEC-E906-HAL-01]` Choosing DMA or Interrupt-Driven FIFO per Transfer
* **Requirement**: every driver picks DMA or ISR-driven FIFO I/O per transfer by rule, not by habit:
  * **ISR + FIFO** when the transfer fits the peripheral FIFO (UART1-7 128 B, UART0 / S_UART 64 B,
    SPI 64 B; T527 User Manual V0.94 §8.19.1, §8.16.1), when the
    protocol needs a decision per byte (I2C ACK/NACK, repeated START), or when the average
    interrupt rate stays below one per 50 µs (e.g. GPS NMEA at 9600 baud: one RX-timeout interrupt
    per sentence).
  * **DMA** when the transfer is larger than the FIFO and contiguous (SPI IMU bursts, flash), for
    continuous streams that would interrupt more often than every 50 µs (UART at >= 921600 baud as a
    cyclic ring), and for memory copies of at least `kDmaCopyMinBytes`.
  * The thresholds (`kDmaCopyMinBytes`, the per-peripheral byte limit) are measured on the board
    by `abstractx.dma_bench()` (DMA setup + completion interrupt vs. CPU copy) and recorded with
    the commit; until measured they are marked "not measured" and drivers use ISR + FIFO.
* **Requirement (the rule, decided 2026-10-22)**: **a transfer larger than the peripheral's hardware
  FIFO uses DMA; a transfer that fits the FIFO uses the FIFO and one completion interrupt.** One
  driver per bus applies the rule per transfer (per segment), so device drivers (IMU, flash, GPS)
  never choose. FIFO sizes (T527 manual V0.94): SPI0/SPI2 64 B TX + 64 B RX (§8.16.1); UART1-7
  128 B, UART0 / S_UART 64 B (§8.19.1); TWI driver mode: send/receive FIFO (§8.14, depth to be
  read from the manual). A UART receive stream counts as larger than the FIFO when more than a
  FIFO of data can arrive before the reader takes it, which is always the case for continuous
  streams. FIFO transfers use the widest access the register allows (SPI: 32-bit, 4 bytes per
  access) and never busy-wait. Replaces the unmeasured thresholds (SPI 4 B, the 32 B draft).
* **Requirement**: the E906 owns DMAC channels 8..15 at `0x03002000`, routed to CLIC line 88 by
  `DMA_MCU_IRQ_CTRL` (`+0x38` = `0xFF00`); the E906 overlay limits Linux's `sun6i-dma` to channels
  0..7 (`dma-channels = <8>`). Descriptors and buffers are handed to the DMAC as physical
  addresses (SRAM space 0 DA `0x3FFC0000` = PA `0x07280000`, space 1 DA `0x40000000` = PA `0x072C0000`).
* **Not implemented yet**: `dma_bench`, `da_to_pa`
* **Rationale**: the previous thresholds (SPI/I2C 4 B, UART 32 B) were never measured; the DMA
  driver passed E906 addresses to the DMAC and never routed its interrupt; Linux owns all 16
  channels of the same controller.
* **Verified by**: `e906-board-selftest` (`tests/board/test_e906_irq.py`)
* **Implementation Target**: `targets/allwinner_e906/src/dma.cpp`, `apps/e906_repl_app/cubie-a5e-flight-stack-e906.dtso`

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
