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
* **Bounded Backpressure (no wait without a limit)** (2026-10-26, replaces the earlier "wait until
  restored" rule that let the E906 hang inside `step()` when Linux stopped draining):
  * **Reply buffer starvation**: In `reply()`, if no host buffer is free, the driver clears
    `s_tx_vq.used->flags = 0` (so Linux kicks when it returns one), increments `s_tx_full_waits`,
    kicks the host and drains doorbells, at most `kReplyWaitRounds` (64) times. It then returns
    `false`: the caller decides whether to drop or retry. `tx_acquire()` never waits.
  * **Hardware Mailbox FIFO space**: the kick (`notify()`) tries `MsgBox::send` at most `kMboxSendTries`
    (256) times, counting each full FIFO in `s_mbox_full_waits`. If the FIFO stays full, the kick is
    *owed*, not lost: the used ring is already published, and the owed kick is retried by the next
    `poll()`, `tx_commit()`, `reply()` or `flush_kick()` (called every loop by
    `platform_poll_network()`).
* **Ring Error Recovery (no permanent stall)**:
  * A descriptor index `>= num` in either ring is skipped (`last_avail_idx` advances; no used entry is
    written because the id is invalid) and counted in `s_ring_errors`.
  * A pending count `> num` is treated first as a torn read and rejected. After `kRingResyncAfter` (3)
    such reads in a row, the driver resyncs `last_avail_idx = avail->idx` and counts the error.
* **Host-Controlled Addresses and Lengths Are Untrusted**:
  * `translate_buf(host_addr, len)` maps a host physical range to an E906 pointer only if the *whole*
    range lies inside one RPMsg pool window. The windows are the two pools the overlays define, and
    nothing else:
    | Overlay | Pool (host) | Core address | Size |
    | :--- | :--- | :--- | :--- |
    | `apps/e906_repl_app/cubie-a5e-flight-stack-e906.dtso` (on the board) | `vdev@48000000` DDR carveout | `0x48000000` (same) | 1 MB |
    | `cubie-a5e-abstractx-rpmsg-sram.dtso` | SRAM Space 1 upper half `0x072E0000` | `0x40020000` | 128 KB |
    Otherwise it returns `nullptr` and the descriptor is a ring error. Addresses outside both pools
    (firmware SRAM, MMIO, other DRAM) are never passed through. Host test builds (non-RISC-V)
    register their own test window with `rpmsg_set_host_window()`.
  * **Board fact (2026-10-28)**: kernel #10 with the flight-stack overlay places both vrings at
    `0x48000000` / `0x48002000` (`/sys/kernel/debug/remoteproc/remoteproc0/resource_table`). From
    `d8bcbac` to this fix the firmware accepted only the SRAM pool, so `setup_vq()` refused the vrings
    and no channel was ever announced; every host test passed because the test window is set by the
    test. A window change is verified on the board (`e906-board-selftest`
    `test_three_channels_announced`), not only by host tests.
  * Every length that the host can change (`desc->len`, `rpmsg_hdr::len`) is read **once** into a
    local, bounded (`desc->len` to `RPMSG_BUFFER_SIZE`, `hdr->len` to `desc_len - sizeof(rpmsg_hdr)`),
    and only the local copy is used afterwards.
  * `tx_commit()` checks that the driver is ready and that the token's descriptor is in range before
    writing anything.
* **No Zero-Length Give-Back**: a buffer taken by `tx_acquire()` that turns out to be unusable is
  handed back with `tx_cancel()`, which un-takes the most recently acquired buffer, instead of
  publishing a 0-byte message (Linux logs a malformed name-service message, or delivers an empty
  datagram). The only exception: when cancel is refused because a later buffer was taken meanwhile
  (the trace packet case), an empty commit is still better than losing one of the 16 host buffers
  for good.
  * **Doorbell FIFO Draining**: In `poll()`, all pending incoming doorbells across both Channel 0
    and Channel 1 must be drained in a tight loop (`s_host_kicks`) to avoid filling the 8-slot
    hardware FIFO and overflowing Linux's software mailbox queue (`MBOX_TX_QUEUE_LEN`).
* **VirtIO Kick Suppression**:
  * In polling mode, `s_tx_vq.used->flags` and `s_rx_vq.used->flags` default to `VRING_USED_F_NO_NOTIFY`
    to suppress redundant host kicks. `enable_rx_kick(bool)` toggles `s_rx_vq.used->flags`.
* **Re-init never keeps old vrings** (2026-10-22): `init()` clears both vring layouts
  (`s_tx_vq`, `s_rx_vq`; `s_lite_*` for `RpmsgLiteMetal`) together with the ready flag, and
  `enable_rx_kick()` writes the used ring only when `is_driver_ready()` is true (which maps the
  rings of the *current* resource table first). A vring pointer kept from an earlier resource table
  (remoteproc restart, or a previous host test block whose memory is unmapped) is never written.
* **Verified by**: `host-tests` (`sim/test_e906_rpmsg_endpoints.cpp`, `sim/test_e906_rpmsg_flow_control.cpp`; the tests `#undef NDEBUG` so their checks also run in `RelWithDebInfo` builds), `ipc-transport-audit`, `thread-sanitizer`, `e906-build-layout`
* **Implementation Target**: `targets/allwinner_e906/src/rpmsg.cpp`

### `[SPEC-E906-IPC-03]` Lite-Metal Direct Driver (`class RpmsgLiteMetal`)
* **Role**: Direct driver for shared SRAM (`0x3FFC0000`) or UIO.
* **Target Memory**: On-chip SRAM Space 0 / Space 1.
* **Cache Policy**: Zero cache operations (NOP). Directly accesses
  memory at bus speed with zero cache maintenance overhead.
* **Parity Invariant**: Adheres to identical natural vring alignment, `avail_pending()` bounds check,
  bounded mailbox send and reply wait, ring error recovery, address window checks, incoming doorbell
  queue draining, and kick suppression contracts as `[SPEC-E906-IPC-02]`. In particular `poll()`
  re-enables host kicks (`used->flags = 0`) before its final re-check, exactly as `Rpmsg::poll()` does;
  leaving `NO_NOTIFY` set loses every later wakeup (2026-10-26: the Lite copy had drifted).
  `register_endpoint()` updates an existing address even when the table is full.
* **Verified by**: `host-tests` (`sim/test_e906_rpmsg_flow_control.cpp`), `ipc-transport-audit`, `e906-build-layout`
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
  physical addresses. The E906 converts them with `translate_buf()`, which accepts
  only ranges that lie wholly inside the RPMsg pool (host `0x072E0000`+ → core
  `0x40020000`+, [SPEC-E906-IPC-02]). The cubie-a5e `translate_da` mapped all of
  Space 0 and Space 1 and passed every other address through, which let a bad or
  hostile descriptor overwrite firmware.
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
  are counted as dropped, never blocking). A buffer that is too small for the
  message is handed back with `tx_cancel()`, never committed with length 0.
* **Reply addressing**: `reply(incoming, ...)` sends to `incoming.src` from
  `incoming.dst`. A caller that builds its own `RpmsgMessage` (for example the REPL
  console) must put the **host** address in `.src` and its own endpoint in `.dst`.
  The Linux tools create their endpoint with `src == dst == E906 address`, which hid
  a swap of the two until 2026-10-26.
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
* **Implementation Target**: `targets/allwinner_e906/src/rpmsg.cpp`, `include/hal/rpmsg.hpp`, `include/abstractx/trace/tracer.hpp`, `src/platform.cpp`, `include/tlp_ring.hpp`, `targets/allwinner_e906/CMakeLists.txt`

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
* **Wake slot** (2026-10-26): the suspended handle is shared with `fc_msgbox_doorbell_isr()`, so it is
  a `std::atomic<void*>`, and the ISR takes it with `exchange(nullptr)`. There is one slot:
  `await_suspend()` returns `false` (does not suspend) if another coroutine already waits, so a second
  waiter can never silently overwrite the first. `await_resume()` does not clear the doorbell flag;
  the flag belongs to `platform_poll_network()`, and `poll()` reads the vrings directly.
* **Verified by**: `host-tests` (`sim/test_e906_rpmsg_flow_control.cpp`), `ipc-transport-audit`, `e906-build-layout`
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
| GPIO bank I (PI8 = IMU INT1) | 115 | 100 | S_INTC `GINTC_CONFIG_REG2` bit 19 | 2026-10-29: IP 0→1 on a DRDY edge, 0 after W1C of `EINT_STA` bit 8, 1 again on the next sample |

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
* **Requirement**: `hal::Timer::SleepAwaiter` inherits `etl::bidirectional_link<0>` and nodes form a
  deadline-sorted `etl::intrusive_list<SleepAwaiter, etl::bidirectional_link<0>>` so any number of
  coroutines and `E906TimerDriver` (`ITimer`) requests may sleep concurrently without overwriting
  each other, `cancel_waiter()` unlinks in $O(1)$ without scanning the list, and expired nodes are
  posted to `IsrDispatcher` from the line-7 handler.
* **Verified by**: `e906-board-selftest` (`tests/board/test_e906_irq.py`), `host-tests`
* **Implementation Target**: `targets/allwinner_e906/src/timer.cpp`, `targets/allwinner_e906/include/hal/timer.hpp`, `targets/allwinner_e906/src/platform.cpp`

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

### `[SPEC-E906-IRQ-07]` GPIO Edge Interrupt: IMU DRDY on PI8
* **Why**: the IMU's own sample clock must start each read (auto-DMA, [SPEC-E906-HAL-05]), so that
  every runtime is paced by the same edge and edge-to-data latency can be measured.
* **Hardware** (measured 2026-10-29 through the REPL and Linux `gpiomon`/`devmem`):
  - ICM-42688-P INT1 reaches **PI8** (header pin 29): with INT1 latched, `PI_DAT` (`0x02000190`)
    bit 8 followed the INT1 polarity setting. The IMU answers on SPI1 **CS0** (PB11), WHO_AM_I `0x47`.
  - Edge-interrupt registers are at `0x02000200 + 0x20 * bank` (PA = 0, so PF = `+0x2A0`, PI =
    `+0x300`): `EINT_CFG0..3` (`+0x00..+0x0C`, 4 bits per pin: 0 rising, 1 falling, 2 high, 3 low,
    4 both), `EINT_CTL` (`+0x10`), `EINT_STA` (`+0x14`, write 1 to clear), `EINT_DEB` (`+0x18`).
    PI8 is `EINT_CFG1` bits [3:0], `EINT_CTL`/`EINT_STA` bit 8, and `PI_CFG1` (`0x02000184`)
    bits [3:0] = `0xE` (EINT function). Linux armed exactly these bits for PI8 (and `+0x2A0`/`+0x2B0`
    for its PF6 card detect).
  - Bank I is GIC 115 (Linux `pinctrl@2000000` interrupt list: PB..PJ = GIC 101..117 step 2, PK = 172),
    routed by `hal::clic::route_gic(115)` to CLIC line 100. The line is level: it stays pending
    until `EINT_STA` bit 8 is cleared.
* **Requirement**: the driver writes only PI8's 4-bit fields and bit 8, read-modify-write under
  `InterruptGuard`: `PI_CFG1` also holds the UART3 pins (PI11/PI12) and the bank's other EINT bits
  belong to whoever owns those pins.
* **Requirement**: the line-100 handler checks `EINT_STA` bit 8 (the line is shared by the eight
  GIC sources 112..119), clears it (W1C) before starting the read, records `mcycle` at entry as
  the edge timestamp, counts the edge, and never resumes a coroutine ([SPEC-E906-IRQ-01]).
* **Requirement**: in the E906 profile PI8 belongs to the E906; Linux must not request it as a
  GPIO interrupt at the same time (both would program `EINT_CTL`).
* **Not yet known**: with Linux `gpiomon` armed on PI8, `EINT_STA` bit 8 latched but the Linux
  interrupt count stayed 0, so bank-I edges are not delivered to Linux's GIC on kernel 7.1. This
  blocks the Linux DRDY column, not the E906.
* **Verified by**: `e906-board-selftest`, `e906-build-layout`, `boundary-race-review`
* **Implementation Target**: `targets/allwinner_e906/include/hal/gpio_irq.hpp`, `targets/allwinner_e906/src/gpio_irq.cpp`, `targets/allwinner_e906/include/hal/clic.hpp`

### [SPEC-E906-CRASH-01] Trap Classification, FPU State Preservation & Crash Dump
* **Requirement**:
  1. The trap entry (`bsp/startup.S`) MUST allocate a 416-byte stack frame (`CrashFrame`) and save all 31 integer GPRs (`x1`..`x31`), 4 CSRs (`mepc`, `mcause`, `mtval`, `mstatus`), all 32 hardware floating-point registers (`f0`..`f31`, single precision `fsw` into 8-byte slots: the E906 has no D, `ilp32f`), and `fcsr` BEFORE branching to or calling any C/C++ code.
  2. The trap entry MUST classify the trap by bit 31 of **`mcause`** (1 = interrupt, 0 = synchronous exception).
  3. Interrupts branch to `.Lhandle_irq`, dispatch to `riscv_trap_dispatcher(mcause, mepc)`, completely restore all FP registers (`f0`..`f31`), `fcsr`, CSRs, and integer GPRs, deallocate the 416-byte stack frame, and return with `mret`.
  4. Synchronous exceptions MUST pass the `CrashFrame*` (`sp`) in `a0` to `hal_crash_dispatcher`, which records the full GPR, CSR, and FPR crash dump to `Trace` and SRAM and halts.
* **Rationale**:
  - In `ilp32f` ABI, C/C++ compilers generate single-precision floating-point instructions in any subroutine and can clobber caller-saved FP registers (`ft0`..`ft11`, `fa0`..`fa7`). Without saving FP registers in the trap prologue, an asynchronous interrupt clobbers live floating-point computations in interrupted coroutines (e.g. sensor fusion math), causing silent corruption upon `mret`.
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
* **Requirement**: the E906 owns DMAC channels 8..15 at `0x03002000`; the E906 overlay limits
  Linux's `sun6i-dma` to channels 0..7 (`dma-channels = <8>`). Note on hardware reservation (measured on silicon 2026-10-24):
  channels 9 and 10 are hardware-firewalled/reserved on Allwinner T527 (OP-TEE / TrustZone / DSP firewall; hardware
  ignores LLI descriptor fetches and leaves CFG=0). Available E906 channels are 8, 11, 12, 13, 14, 15.
  `DmaController::allocate_channel()` skips channels 9 and 10.
  Interrupt routing (T527 manual
  §2.8.6.8-9; reset value of both is `0xFFFF`, measured on the board 2026-10-22, and Linux never
  writes them): the E906 sets `DMAC_IRQ_MCU_EN_REG` (+0x38) = `0xFF00` and clears bits 8..15 of
  `DMAC_IRQ_CPU_EN_REG` (+0x34, i.e. `0x00FF`) **before** enabling any channel interrupt. If
  channels 8..15 still reached the Linux CPU, `sun6i-dma`'s handler (which reads only status
  register 0 when it has 8 channels) would see an interrupt it cannot clear and Linux could
  disable the shared DMA interrupt line. The E906 never touches the main DMAC's clock gate or
  reset (Linux owns them); the earlier `Ccu::enable_module(McuDma)` call gated the separate
  MCU_DMAC (0x07121000), not this controller.
* **Constraint**: Linux's sun6i_kill_tasklet() (kernel drivers/dma/sun6i-dma.c) writes 0 to both IRQ-enable registers when the
  driver is removed or fails to probe (`drivers/dma/sun6i-dma.c`), which disables the E906's
  channel interrupts too; the E906 must detect a lost enable (status set, no interrupt) rather
  than assume it. Descriptors and buffers are handed to the DMAC as physical
  addresses (SRAM space 0 DA `0x3FFC0000` = PA `0x07280000`, space 1 DA `0x40000000` = PA `0x072C0000`).
* **Requirement (addresses)**: the DMAC sees physical addresses. SRAM Space 0 and Space 1 share
  one offset: PA = DA - `0x38D40000` (DA `0x3FFC0000` = PA `0x07280000`, DA `0x40000000` = PA
  `0x072C0000`); `DmaController::pa()` converts every descriptor field (`make_mem2periph`,
  `make_periph2mem`) and the descriptor address itself, and `current_dst_pa()` / `pkg_num()` query
  the active channel registers. Peripheral registers have the same address on both sides.
  `E906SpiDriver` retains `active_req_` while DMA is in flight and completes `active_req_` before
  calling `set_hardware_idle_from_isr()`.
* **Requirement (bring-up measurement)**: `abstractx.dma_bench(n, port=1)` copies `n` bytes
  (<= 1024) from SRAM Space 0 to Space 1 on an E906 channel, waits for the line-88 completion
  interrupt and returns `(ok, dma_cycles, cpu_cycles)` (mcycle at 200 MHz; dma = start to
  completion seen by the waiting code, cpu = `memcpy` of the same bytes). `port` is the memory DRQ
  port for both sides (1 = "DRAM/SDRAM" as Linux uses for all memory, 0 = "SRAM" in the manual
  table); which one works for SRAM is measured, not assumed.
* **Rationale**: the previous thresholds (SPI/I2C 4 B, UART 32 B) were never measured; the DMA
  driver passed E906 addresses to the DMAC and never routed its interrupt; Linux owns all 16
  channels of the same controller.
* **Verified by**: `e906-board-selftest` (`tests/board/test_e906_irq.py`)
* **Implementation Target**: `targets/allwinner_e906/src/dma.cpp`, `apps/e906_repl_app/cubie-a5e-flight-stack-e906.dtso`

---


### `[SPEC-E906-HAL-02]` UART Receive Is Always Circular DMA into 4 Buffers; Transmit by FIFO or DMA with a Hardware Gap
* **Decision (project owner, 2026-10-22)**: every UART receive uses DMA at every baud rate; there is
  no interrupt-per-FIFO receive path in production (a receive stream is always larger than the
  FIFO, [SPEC-E906-HAL-01]). Rationale with board numbers: one UART register read costs ~0.29 us,
  so interrupt receive costs ~0.5 us per byte (~5 % CPU at 1 Mbit/s, ~25 % at 5 Mbit/s,
  estimated) and leaves under 1 ms before an overrun above 1 Mbit/s.
* **Requirement (ring)**: 16 receive buffers of 64 B each (1,024 B ring) in SRAM Space 1 (`.sram_c`),
  linked as a circular DMA descriptor chain (descriptor 15 links to descriptor 0) on one E906 DMAC channel
  (8..15), source = the UART's RX DRQ (UART3: port 17), destination = memory, byte-wide. The 1,024 B ring
  provides 88.8 ms of overrun headroom at 115200 baud, guaranteeing zero dropped bytes during MicroPython
  garbage collection sweeps (5–15 ms). Each buffer's "package end" interrupt hands the full buffer to the reader.
* **Requirement (end of frame)**: the UART runs in DMA1 mode with `UART_DMA_REQ_EN` = RX + DMA
  timeout (bit 2), so bytes left in the FIFO at the end of a frame are moved by DMA; the RX-timeout
  interrupt then reads `DMAC_CUR_DEST_REG` (+0x114 + N*0x40) and hands the partly filled buffer's
  new bytes to the reader. A frame is delivered within 4 character times plus the interrupt,
  whatever the buffer size. While circular DMA is active:
  1. `UART_IER` keeps `ERBFI | ELSI` (`0x05`) enabled so the 4-character RX timeout (`IIR = 0x0C`)
     and threshold events increment CLIC line 90 (`uart3`), while the UART ISR never reads
     `UART_RBR` with the CPU when circular DMA is active;
  2. On `IIR = 0x04` (64 B half-full trigger), `E906Uart::on_irq()` waits only until `UART_RFL < 64` (with
     32-`nop` spacing between APB1 reads so it does not contend with the DMAC) and lets the line-88
     `DMA_IRQ_PKG` completion commit the 64 B buffer; on `IIR = 0x0C` (RX character timeout),
     `E906Uart::on_irq()` waits for `UART_RFL == 0` plus 64 `nop`s for the DMAC MBUS write buffer to drain
     before calling `E906Uart::dma_sync(true)`;
  3. `DmaLli::para` (`WAIT_CYC`) MUST be `>= 0x40` (64 DMAC clocks): on the T527, UART1-7 run on the
     24 MHz APB1 clock while the DMAC runs on the high-speed AHB/MBUS clock, so `para <= 0x20`
     re-samples `DRQ_UART3` before the 24 MHz UART deasserts `DRQ` after the last FIFO byte and
     reads the last byte twice; and
  4. On the T527 DMAC, `DMA_PKG_NUM_REG` (`+0x30`) increments when a 64 B LLI burst starts (~145 µs
     before `DMA_CUR_DEST_REG` advances to the next LLI), and `DMA_CUR_DEST_REG` (`+0x14`) advances
     by 4 bytes (`mod64 = 0..3`) when words enter the DMAC internal FIFO before the MBUS write lands
     in SRAM. Therefore `E906Uart::dma_sync(end_of_frame)` subtracts `kDmaBufBytes` when `pkg_bytes` is
     one buffer ahead of `DMA_CUR_DEST_REG`, and only includes partial-LLI `ahead` bytes when
     `end_of_frame` is true (`IIR = 0x0C` or `UART_RFL == 0` with the UART RX FIFO drained).
* **Requirement (overrun)**: if the DMA laps a buffer the reader has not released (detected with
  `DMAC_PKG_NUM_REG`, +0x130 + N*0x40), the overwritten bytes are counted as dropped, never
  delivered as data. UART line errors (overrun, framing, parity, break) are taken from the
  line-status interrupt and counted per port.
* **Requirement (reader)**: the reader is a coroutine that awaits "bytes available" and gets spans
  into the ring (no copy), then releases them; MicroPython's `uart_read` / `uart_read_into` copies
  from the same spans. For measurement only, a MicroPython switch (`abstractx.uart_dma([enable])`)
  puts a port back on the interrupt receive path, so DMA and interrupt receive can be compared on
  the same stream.
* **Requirement (transmit)**: a message that fits the 128-byte FIFO is written into it in one go;
  a larger one is sent by DMA ([SPEC-E906-HAL-01]). Either way the UART sends back-to-back unless
  a gap is configured. The gap between bytes is the UART's own `TX_DELAY` register (+0xCC: idle
  time after each stop bit, 0-255 units of 16 UART clocks = 0.67 us steps, max 170 us with
  APB1 = 24 MHz); the driver converts `UartConfig::tx_gap_us` with the current UART clock and
  reports the range in `UartCapabilities` ([SPEC-HAL-06]). Two stop bits come from `LCR`. A gap
  longer than `TX_DELAY` allows is refused (`NotSupported`), never emulated by the driver.
* **Requirement (transmit completion)**: a write completes when the transmitter is empty (`LSR`
  bit 6, last stop bit on the pin), not when the FIFO is empty, so direction switching (RS-485),
  baud changes and message gaps start after the last byte has left.
* **Depends on**: E906 DMA working ([SPEC-E906-HAL-01]: physical addresses, CLIC line 88 via
  `DMAC_IRQ_MCU_EN_REG` (+0x38), channels 8..15 owned by the E906 through the overlay).
* **Verified by**: `e906-board-selftest` (GPS stream on UART3: no drops, per-wake cost from the
  timeline, DMA vs interrupt comparison)
* **Implementation Target**: `targets/allwinner_e906/src/dma.cpp`, `targets/allwinner_e906/src/uart.cpp`

### `[SPEC-E906-HAL-03]` Dual-Mode (Single-SPI 1-Bit & Dual-SPI 2-Bit) DMA Engine on SPI1 (PB11..PB14)
* **Hardware Routing & Bus Pinmux**:
  - The Radxa Cubie A5E 40-pin header routes exclusively to **SPI1** (`SPI1_BASE = 0x04026000`), not SPI0 (Port C is unrouted).
  - Physical pins on Port B:
    - Pin 24: PB11 (SPI1_CS0)
    - Pin 23: PB12 (SPI1_CLK)
    - Pin 19: PB13 (SPI1_MOSI / SPI1_IO0)
    - Pin 21: PB14 (SPI1_MISO / SPI1_IO1)
  - Multiplex function is Function 4 (`allwinner,pinmux = <4>`). Bound to `generic-uio` in device tree to prevent Linux kernel driver contention while E906 controls the bus.
* **Clock & Reset Control (CCU)**:
  - Bus Gating & Reset: `CCU_SPI_BGR_REG = 0x0200196C`. Bit 17 deasserts SPI1 reset (`1U << 17`); Bit 1 enables SPI1 clock gating (`1U << 1`).
  - Module Clock: `CCU_SPI1_CLK_REG = 0x02001944`. Gating bit is bit 31 (`1U << 31`), parent clock selected as `osc24M` (bits 26..24 = 0), divider M/N configured for requested baud.
* **DMA Endpoint & Physical Address Mapping**:
  - SPI1 DMA Request Line is **DRQ 23** (`DRQ_SPI1 = 23`).
  - E906 owns DMA channels 8..15 (`0x03002000`).
  - Physical address translation: `hal::DmaController::pa()` translates SRAM Space 0 (`0x3FFC0000` -> host PA `0x07280000`) and Space 1 (`0x40000000` -> host PA `0x072C0000`) for all LLI descriptor fields (`dst`, `src`, `p_lli_next`).
  - Register endpoints: `SPI1_RXD_8 = 0x04026300` and `SPI1_TXD_8 = 0x04026200`.
  - Interrupt completion: signaled on CLIC line 88 (`DMAC_IRQ1_NS`) via `DMA_IRQ_MCU_EN_REG = 0xFF00`.
* **Dual-Mode Operation**:
  - **Single-SPI (4-Wire Full Duplex)**: Standard 1-bit MOSI/MISO protocol on PB13/PB14 for ICM-42688-P IMU and standard SPI peripherals. Burst counters `SPI_MBC = len`, `SPI_MTC = len`, `SPI_BCC = len`.
  - **Dual-SPI (2-Bit Half Duplex / Dual-Lane)**: High-speed 2-bit protocol on IO0/IO1 (PB13/PB14) for the FPGA crossbar switch fabric (`asp_spi_frontend.sv`, commands `0xA0`, `0xA1`, `0xA2`). `SPI_BCC = (1U << 28) | len` (`SUN6I_BURST_CTL_CNT_DRM = BIT(28)`).
* **Threshold-Balanced Execution**:
  - Transfers $\le 64$ bytes (hardware FIFO depth) execute directly via low-latency FIFO MMIO (`transceive_fifo_sync`), bypassing DMA setup overhead.
  - Transfers $> 64$ bytes stream via dual-channel DMA (TX channel + RX channel) with zero CPU bus cycles.
* **Verified by**: `e906-board-selftest`, `host-tests`
* **Implementation Target**: `targets/allwinner_e906/include/memory_map.h`, `targets/allwinner_e906/include/hal/dma.hpp`, `targets/allwinner_e906/include/hal/spi.hpp`, `targets/allwinner_e906/src/spi.cpp`, `targets/allwinner_e906/src/platform.cpp`, `apps/e906_repl_app/src/spi.cpp`

### `[SPEC-E906-HAL-05]` DRDY-Triggered IMU Auto-DMA on SPI1
* **Why**: the CPU must not be in the IMU read. The DRDY edge starts the SPI1 DMA burst from the
  interrupt handler and the waiting coroutine wakes with the sample already in SRAM; this is the
  `IoEngine` auto channel with `TriggerMode::GpioEdge` ([SPEC-ARCH-13]).
* **Requirement**: on each PI8 edge ([SPEC-E906-IRQ-07]) the handler starts one 15-byte DMA
  transfer on SPI1 CS0 (`0x9D` = `TEMP_DATA1 | 0x80`, then 14 dummy bytes). This transfer uses DMA
  although it is below `DMA_THRESHOLD`: a FIFO transfer would keep the handler busy for the whole
  burst (~12 µs at 10 MHz). It is the one stated exception to [SPEC-E906-HAL-01].
* **Requirement**: TX and RX buffers are members of the auto-channel object in SRAM (DMA-reachable,
  never the stack). If SPI1 is still busy when an edge arrives, the edge is counted as an overrun
  and dropped; it is never queued or retried, so a late read cannot be mistaken for a new sample.
* **Requirement**: each sample carries the edge `mcycle`, the DMA start and DMA done `mcycle`, and a
  sequence number; the DMA completion posts the waiting coroutine through `IsrDispatcher`.
* **Requirement**: the IMU is configured as in [SPEC-IMU-01]: INT1 pulsed push-pull active high,
  DRDY cleared by the data read, and a known ODR. The DMA burst itself clears DRDY, so no status
  read is needed.
* **Verified by**: `e906-board-selftest`, `host-tests`, `boundary-race-review`
* **Implementation Target**: `targets/allwinner_e906/src/io_processor.cpp`, `targets/allwinner_e906/src/gpio_irq.cpp`, `targets/allwinner_e906/src/spi.cpp`

### `[SPEC-E906-HAL-04]` Bus Ports Exposed by the E906 Target (`get_uart_port` / `get_i2c_port`)
* **Port list** (the per-target list [SPEC-HAL-10] asks for):

  | Call | Port | Driver | Pins (40-pin header) |
  | :--- | :--- | :--- | :--- |
  | `get_uart_port(0)`, `get_uart_port(3)`, `get_uart_driver()` | UART3 | `E906Uart` | PI11 TX / PI12 RX (pins 16, 31), function 2, pull-up on RX |
  | `get_uart_port(2)` | none (null) | | UART2's PB0/PB1 are not connected on the Cubie A5E |
  | `get_i2c_port(0)` | TWI0 | `E906I2c` (`get_e906_i2c()`) | none set: not routed to the header |
  | `get_i2c_port(1)` | TWI1 | `E906I2c` | PB4 SCL / PB5 SDA (pins 5, 3), function 2 |

  UART2: the board schematic leaves PB0/PB1 unconnected and header pins 11/13 go to PI9/PI10
  (`apps/e906_repl_app/cubie-a5e-flight-stack-e906.dtso`), so UART2 has no port, `Pio::init` does
  not mux PB0/PB1, and the old `fc::hal::Uart2` driver (interrupt never attached) is removed.
* **One driver per controller type, the address passed in** (decided 2026-10-27): a driver is one
  file in `targets/allwinner_e906/src/` and takes a port description (register base, GIC source,
  CCU module, pin register / mask / function). TWI0 and TWI1 are two `E906I2c` objects in
  `i2c.cpp`, not two drivers. Apps hold no register addresses; the REPL's `src/i2c.cpp` / `src/uart.cpp` keep only the
  MicroPython glue (staging buffers that survive Ctrl-C, the wait that runs the VM hook).
* **One wait hook for all E906 drivers** (`hal/driver_wait.hpp`): a synchronous driver call that
  must wait (TWI transfer, UART TX ring full, UART TX DMA) calls `driver_wait(deadline)`; the
  default arms a timer wake and calls `platform_idle_wait()`; the REPL replaces it with
  `repl_wait_step` (Ctrl-C, RPMsg). Every such wait has a deadline.
* **UART (`E906Uart`, `uart.cpp`)**, taken from the UART3 code that works on the board:
  1. Port entry: base, GIC, CCU module, DMA DRQ, pins and pull, FIFO bytes. Buffers live in
     `.sram_c` (SRAM Space 1): 16 x 64 B DMA receive ring + descriptors, 256 B TX DMA bounce
     buffer, 1024 B polled-read buffer, 256 B TX ring.
  2. Receive by circular DMA ([SPEC-E906-HAL-02]) into the core `UartRxRingBase` (so
     `read_async` works); when no coroutine waits, completed bytes move to the polled-read buffer
     for `read()`. `set_dma(false)` switches to interrupt receive (bring-up only).
  3. Baud change uses the T527 configure-at-busy sequence (`UART_HALT`), so a streaming GPS does
     not block it; line-status errors (overrun, parity, framing, break) reach the ring.
  4. TX: up to the FIFO through the TX ring and the THR-empty interrupt; larger writes by DMA in
     256 B chunks, 50 ms deadline per chunk. A full TX ring waits through `driver_wait`.
  5. The UART interrupt (GIC 37) is on CLIC line 90 with the other GIC 32..39 sources; the
     handler returns at once when IIR reports nothing pending, and init never disables the line.
* **TWI (`E906I2c`, `i2c.cpp`)**, taken from the TWI1 code that works on the board:
  1. One `INT_FLG` interrupt per bus step; the flag is cleared by **writing 1** to `CNTR` bit 3.
  2. TWI0 (GIC 42) and TWI1 (GIC 43) share CLIC line 91 (`line_for_gic`): each port attaches
     its own handler and returns at once when its own `INT_FLG` is clear. Init never disables
     the shared line.
  3. Clock: `CCR = (M << 3) | N`, `f = 24 MHz / (10 * (M + 1) * 2^N)` (Linux `i2c-mv64xxx`,
     sun6i); 400 kHz = `0x11`, 100 kHz = `0x2A`. The earlier TWI1 values `0x12` / `0x25` give
     200 kHz / 15 kHz by this formula (to be confirmed on the board).
  4. Every error status ends the transfer with STOP: `0x20`/`0x48` address NACK, `0x30` data
     NACK, `0x38` arbitration lost, anything else bus error.
  5. A synchronous transfer waits through a replaceable wait hook (default: `arm_wake` +
     `platform_idle_wait`) with a 20 ms deadline; on timeout it sends STOP, counts it and returns
     `Timeout`. A transfer still busy when the next one starts is aborted first.
  6. No TWI DMA until its threshold is measured ([SPEC-E906-HAL-01]): register reads fit the FIFO.
  7. `capabilities()`: up to 2 messages (write, read, or write then read with repeated START),
     address-only probe.
  8. An async transfer has a deadline ([SPEC-HAL-09]): `I2cRequest::timeout_us` (0 =
     `kI2cDefaultTimeoutUs`) from its start. The start arms a timer wake (`Timer::arm_wake`) so a
     sleeping loop wakes; `platform_poll_network()` (every `step()`) calls `poll_timeout()` on each
     TWI, which, under an interrupt guard, ends a late transfer with STOP and completes it with
     `Timeout`. The guard makes the timeout and the completion interrupt mutually exclusive: the
     request completes exactly once, and no interrupt resumes a coroutine.
* **Rules for every port driver** (2026-10-27, after `c5a1eaa`):
  1. Interrupt- or DMA-driven ([SPEC-E906-HAL-01]); `start_hardware_transfer_from_isr` starts the
     transfer and returns. It never waits on a status bit, and no wait anywhere is unbounded.
  2. Pinmux touches only the port's own pins: PI11/PI12 UART3, PB4/PB5 TWI1, PB11..PB14 SPI1.
  3. Clock and reset go through `Ccu::enable_module` (reset released before the gate opens).
  4. Each driver lives in its own translation unit. `get_uart_port` / `get_i2c_port` in
     `platform.cpp` refer to every port, so every app links every port's driver (measured
     2026-10-27: UART3 adds about 3.9 KB of text to apps that never use it). Buffers go in `.sram_c`
     (SRAM Space 1), not `SRAM_FAST`. Every E906 app must still fit `SRAM_FAST` ([SPEC-E906-MEM-01]).
* **Why**: `c5a1eaa` added polled UART3/TWI1 classes to `platform.cpp`. They ran the whole I2C
  transfer inside the ISR hook, switched UART2's PB0/PB1 to TWI1, and grew every E906 app by
  about 14 KB, so `e906_repl_app` no longer linked (`SRAM_FAST` overflowed by 6192 B).
  The classes were removed.
* **Verified by**: `e906-build-layout` (every E906 app links and fits), `hal-concurrency-audit`,
  `host-tests` (`sim/test_e906_twi.cpp`, `sim/test_e906_uart.cpp`: each driver against a register
  model), `e906-board-selftest` (compass on TWI1, GPS on UART3)
* **Implementation Target**: `targets/allwinner_e906/src/platform.cpp`, `targets/allwinner_e906/src/i2c.cpp`, `targets/allwinner_e906/include/hal/i2c.hpp`, `targets/allwinner_e906/src/uart.cpp`, `targets/allwinner_e906/include/hal/uart.hpp`, `targets/allwinner_e906/src/driver_wait.cpp`, `apps/e906_repl_app/src/i2c.cpp`, `apps/e906_repl_app/src/uart.cpp`

## 7. Binary Telemetry & Observability (`SPEC-E906-TRACE`)


## 8. Traceability Matrix

| Design Requirement | Description | Primary Implementation File |
| :--- | :--- | :--- |
| `[SPEC-E906-ARCH-01]` | E906 RV32IMAFC Core (No MMU, Single-Precision FPU) | `bsp/startup.S` |
| `[SPEC-E906-ARCH-02]` | Hardwired ASIC SYSMAP PMAs | `include/memory_map.h` |
| `[SPEC-E906-MEM-01]`  | Dedicated MCU SRAM (Zero TCM) | `bsp/e906_sram.ld` |
| `[SPEC-E906-MEM-02]`  | Control & Data Plane SRAM Split | `dram_spsc_protocol.h` |
| `[SPEC-E906-CACHE-01]`| THEADISAEE Vendor Opcode Unlock | `bsp/startup.S` |
| `[SPEC-E906-CACHE-02]`| I-Cache On, D-Cache Off (`0x1031`) | `bsp/startup.S` |
| `[SPEC-E906-CACHE-03]`| Tina SDK Machine Opcodes | `src/pmp.cpp` |
| `[SPEC-E906-IPC-01]`  | Pure Virtual `IRpmsg` | `include/hal/rpmsg.hpp` |
| `[SPEC-E906-IPC-02]`  | Standard Linux VirtIO RPMsg | `src/rpmsg.cpp` |
| `[SPEC-E906-IPC-03]`  | Lite-Metal Direct SRAM Driver | `src/rpmsg.cpp` |
| `[SPEC-E906-IPC-04]`  | Zero-Copy TLP + Trace Endpoints over RPMsg | `src/rpmsg.cpp`, `src/rpmsg_endpoints.cpp` |
| `[SPEC-E906-IPC-05]`  | Trace Endpoint 3 s Hello Lease | `src/rpmsg_endpoints.cpp`, `src/platform.cpp` |
| `[SPEC-E906-CORO-01]` | Async `async_receive()` | `include/hal/rpmsg.hpp` |
| `[SPEC-E906-CORO-02]` | MSGBOX Doorbell ISR Waking | `src/rpmsg.cpp` |
| `[SPEC-E906-IRQ-01]`  | CLIC Mode and Dispatch | `include/hal/clic.hpp`, `src/irq_dispatcher.cpp` |
| `[SPEC-E906-IRQ-02]`  | Main-Domain Peripherals via S_INTC | `include/hal/clic.hpp` |
| `[SPEC-E906-IRQ-03]`  | Core Timer (`mtime` 24 MHz, `mcycle` 200 MHz) | `src/timer.cpp`, `include/hal/timer.hpp` |
| `[SPEC-E906-IRQ-04]`  | RPMsg Doorbell Interrupt (No Polling) | `src/rpmsg.cpp`, `src/platform.cpp` |
| `[SPEC-E906-IRQ-05]`  | Runtime Status: Cycle Clock & Per-Line ISR Time | `src/platform.cpp`, `src/irq_dispatcher.cpp` |
| `[SPEC-E906-IRQ-06]`  | Profiler Sample Timer & Interrupt Spans | `src/timer.cpp`, `src/irq_dispatcher.cpp` |
| `[SPEC-E906-CRASH-01]`| Trap Classification, FPU Save & Crash Dump | `bsp/startup.S`, `src/crash.cpp` |
| `[SPEC-E906-HAL-01]`  | FIFO vs. DMA Balancing & DMAC Routing | `src/{dma,spi,uart,i2c,platform}.cpp` |
| `[SPEC-E906-HAL-02]`  | Circular 4x64 B UART DMA RX & TX Gap | `src/dma.cpp`, `src/uart.cpp` |
| `[SPEC-E906-HAL-03]`  | Dual-Mode SPI1 (PB11..PB14) DMA Engine | `src/{spi,platform}.cpp`, `apps/e906_repl_app/src/uart.cpp` |
