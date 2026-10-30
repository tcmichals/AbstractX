# AbstractX Project Status, Roadmap & Hardware Bring-up (TODO)

This document is the authoritative roadmap and active engineering task tracker for the AbstractX framework.

---

## 🚀 Active In-Flight Roadmap (Milestone 7 Architecture)

### 1. Unified Master Runtime API (`abstractx::`) `[SPEC-ARCH-06]`
- [x] **Declare Master Interface**: Created [`include/abstractx/abstractx.hpp`](include/abstractx/abstractx.hpp) with unified `init()`, `spawn()`, `step()`, `step_async()`, and `run()`.
- [x] **Implement Runtime Orchestrator (`src/runtime.cpp`)**:
  - [x] Implement `abstractx::init(const Config&)` to configure HAL, SPSC rings, and autonomous silicon placement.
  - [x] Implement `abstractx::spawn(Task<void>)` to queue application coroutines into the cooperative Dispatcher.
  - [x] Implement `abstractx::step()` and `abstractx::step_async()` to pump AbstractX I/O and trace dispatchers cooperatively.
  - [x] Implement `abstractx::run(Task<void>)` to execute the user application and internal service tasks to completion.
  - [x] Implement `abstractx::service_task()` to pump `io_processor`, drain CTF trace packets, and route inter-domain TLP rings.
- [x] **Runtime Verification Suite**: Created [`sim/test_abstractx_runtime.cpp`](sim/test_abstractx_runtime.cpp). Host suite is 27/27 in Debug and ASan/UBSan builds as of 2026-10-13.

### 2. `IIoProcessor` Coroutine & Init Interface Enhancement
- [x] **Update Interface (`include/abstractx/hal/io_processor.hpp`)**:
  - [x] Add `bool init(const IoProcessorSetup& setup)` taking startup channel mappings, ring pointers, and trace sink config.
  - [x] Add `Task<void> run_coroutine()` to allow the I/O processor reactor to execute cooperatively inside the main coroutine loop.
- [x] **Target Implementations**:
  - [x] Update Linux target (`targets/linux/src/io_processor.cpp`) to implement `run_coroutine()` via `epoll_reactor` awaiters.
  - [x] Update E906 target (`targets/allwinner_e906/src/io_processor.cpp`) with `init()` and cooperative coroutine execution.
  - [x] Update RP2350 target (`targets/pico2w_rp2350/src/io_processor.cpp`) with Core 0 `run_coroutine()` runner.
- [x] **One engine in core (`[SPEC-ARCH-13]`, 2026-10-28)**: `abstractx::hal::IoEngine` (`include/abstractx/hal/io_engine.hpp`) owns the TLP decode, auto channels, bus lockout, bridge slot pool and completion framing; Linux, E906 and Pico 2 W `io_processor.cpp` are now `IIoBackend`s only (`sim/test_io_engine.cpp`). Zynq stays a pass-through (engine in RTL).
- [ ] Port the ESP32-P4 stub to an `IIoBackend` once its SPI/GDMA driver exists.
- [ ] Async I2C on E906 (`E906I2c`) and Pico (`PicoI2c`): the engine's I2C bridge blocks in `start_transfer()` on both.

### 2a. Sensor Latency Comparison: DRDY Auto-DMA on Every Runtime (`[SPEC-E906-IRQ-07]`, `[SPEC-E906-HAL-05]`, `[SPEC-E906-BENCH-05]`)
Goal: the same ICM-42688-P work (1 kHz, DRDY-triggered read, gyro + accel) on each runtime, measured from the DRDY edge to the sample in the consumer, plus compass and GPS drained between IMU samples. Order: C++ first (MicroPython depends on it).
- [x] Measure the DRDY path on the board: IMU on SPI1 CS0, INT1 -> PI8 (pin 29) -> GIC 115 -> S_INTC REG2 bit 19 -> CLIC line 100 (2026-10-29, `14e8a0f`).
- [x] E906 bare metal + AbstractX: `hal::gpio_irq`, SPI1 auto-DMA per edge, `Icm42688p` DRDY init, bench `A`; 3 x 10000 samples, 0 drops, edge -> coroutine 31.0 us avg (`c97c331`, `docs/verification/evidence/e906_imu_drdy_baremetal.json`).
- [ ] E906 FreeRTOS + AbstractX: dispatch CLIC lines 88 (DMA) and 100 (GPIO) through `hal::irq` in `freertos_risc_v_application_interrupt_handler`, then run `bench_e906_runtime.py --imu 10000` on `e906_freertos_app`.
- [ ] E906 MicroPython: `abstractx` binding for a DRDY auto-DMA channel (await the next sample from Python), spec in `apps/e906_repl_app/SPECIFICATION.md`; same report keys as `A`.
- [ ] Stream each sample to Linux over RPMsg with its edge timestamp and stage timings (today `A` only reports totals); Linux records arrival p50/p99/max.
- [ ] Linux A55 + AbstractX: bank-I GPIO edges latch `EINT_STA` but never reach Linux's GIC (kernel 7.1, `gpiomon` count 0); fix or route DRDY to another bank, then `gpiod` edge -> `spidev` with the `cubie-a5e-flight-stack-linux.dtso` overlay. Optional: plain Python (`spidev`, `gpiod`) column.
- [ ] One clock for edge -> Linux user space on every runtime: jumper DRDY to a second GPIO in another bank that Linux timestamps (needs the Linux GPIO interrupt above), or a logic analyzer on DRDY + a "done" GPIO.
- [ ] Common JSON-lines output (one schema for all runtimes) and one script that builds the tables and SVGs.
- [ ] Compass (TWI1) and GPS (UART3) as secondary streams drained between IMU samples (primary-paced pattern, AGENTS §5); check they do not disturb the IMU timing.
- [ ] Board: compass not found on TWI1 (`SCAN []`) on 2026-10-29 with both this tree and `14e8a0f`; check the GPS puck power and SDA/SCL wiring.
- [ ] DRDY period spread: 971..1012 us at 1 kHz while the handler is steady (edge -> DMA 1165..1362 cycles); IMU clock or interrupt-entry delay? (logic analyzer).
- [ ] Board-test the `A` timeout paths (no IMU, DRDY stops) ([SPEC-E906-BENCH-05]).
- [ ] Shrink `run_imu` (13 KB `.text`, mostly inlined `Report` calls).

### 2b. Articles (`docs/articles/`)
- [ ] Part 4, `baremetal-vs-freertos-e906/README.md`: replace the bare-metal idle claim ("0 wake-ups, 0.01 %": evidence says `idle=not-instrumented`); the "5.5 % tick cost" is the busy share of the whole run incl. the benchmark, not the tick (measure an idle-only window); heap wording vs `_malloc_r`/`_sbrk` linked (EL-1027-31); evidence timestamps are 1970 (set the board clock). Add the FreeRTOS vs bare-metal IMU DRDY numbers when 2a has them.
- [ ] New article: where should sensor I/O live on the T527, Linux on the A55 or the E906 (bare metal, FreeRTOS, MicroPython)? Same IMU work on each runtime, edge -> data latency (p50/p99/max), lost samples, CPU and RAM. Needs 2a.
- [ ] Optional: native FreeRTOS version (task per sensor, blocking drivers, `xQueue`) vs AbstractX coroutines: RAM, latency, code shape.

### 3. Binary CTF 1.8 Tracing & Configurable Sinks `[SPEC-TRACE-04]` `[SPEC-TRACE-06]`
- [x] **Startup Trace Configuration (`TraceConfig`)**:
  - [x] Wire `TraceSinkType` (`None`, `Udp`, `File`, `SharedSramRing`) through `abstractx::init()`.
  - [x] Add `BufferProfile` class enum (`PingPong_1K_x2`, `Ring_1K_x4`, `Compact_512B_x2`, `Large_2K_x4`).
- [x] **1 KB Ping-Pong Buffer Architecture (`CtfTraceEngine<1024, 2>`)**:
  - [x] Non-blocking producer fill buffer & consumer transmit buffer separation with atomic pointer swap.
  - [x] Sized to 1,024 B to fit unfragmented within 1,500 B Ethernet/Wi-Fi MTU and batch ~35-45 events.
- [x] **Trace Dispatcher Coroutine (`trace_dispatcher_task`)**:
  - [x] Implement non-blocking coroutine that monitors CTF trace buffer watermarks and periodic flush timers (e.g. 10 ms).
  - [x] Flushes binary CTF 1.8 event packets into 64-byte TLPs on channels `0x02` (Telemetry) and `0x04` (Debug).
- [x] **C++ Trace Sinks**:
  - [x] Implement `UdpTraceSink` for live network streaming (UDP port 9870 to Visualizer Studio).
  - [x] Implement `FileTraceSink` for local binary logging (`trace.ctf`) compatible with Babeltrace 2 and Trace Compass.
- [x] **HAL Driver Tracing**:
  - [x] Update HAL drivers (`hal_uart.cpp`, `hal_spi.cpp`, `hal_i2c.cpp`, `hal_gpio.cpp`) to emit binary CTF trace events instead of raw formatted strings (`uart.puts`).

### 4. Heterogeneous Co-Processor Pipeline (E906 to Linux) `[SPEC-TRACE-05]`
- [x] **E906 Co-Processor Egress**:
  - [x] Route E906 trace TLPs to shared SRAM (`0x07280000` ARM64 physical / `0x3FFC0000` E906 local).
  - [x] Assert `sun6i-msgbox` hardware doorbell interrupt to notify Linux.
- [x] **E906 Device Tree Overlay (`cubie_a5e_e906_overlay.dtso`)**:
  - [x] Standardized overlay extension to `.dtso` for kernel Kbuild / Buildroot compatibility.
  - [x] Configured onboard AIC8800 SDIO Wi-Fi 6 power regulators (PL7 `3v3-wifi`, PM1 `wifi-en`) and `&mmc1` node.
  - [x] Bound `&spi0`, `&spi1`, `&uart2`, and `&i2c1` to `generic-uio` with default `pinctrl` so Linux configures clocks/pins without driver contention.
  - [x] Added automated `e906_dtbo` compilation target in CMake.
- [ ] **Linux Ingestion Coroutine (`linux_trace_receiver_task`)** (was marked done; not implemented, see review findings):
  - [ ] Map shared SRAM via `/dev/uio` or `/dev/mem`.
  - [ ] Await MSGBox doorbell `eventfd` non-blockingly and drain 64-byte TLPs into the configured host sink (`UdpTraceSink` or `FileTraceSink`).
- [ ] **SRAM-to-UDP Bridge Daemon**:
  - [ ] Adapt `fast_sram_telemetry.py` (cubie-a5e repo: `firmware/e906-riscv/apps/testStringBinaryTrace0/`) to read popped SRAM telemetry packets and forward them over UDP (`sock.sendto(data, ("127.0.0.1", 9870))`).
  - [ ] Wire to `/dev/uio0` for zero-overhead doorbell wakeups.

### 5. Application Modernization (`apps/gps_imu_app/`)
- [x] **Refactor to Unified `abstractx::` API**:
  - [x] Replace manual thread spawning and loop pumping in `apps/gps_imu_app/src/main.cpp` with `abstractx::init()` and `abstractx::run()`.
  - [x] Transition application to a 100% event-driven loop with zero manual byte polling or `platform_poll_network()` calls.
  - [x] Validate that the exact same application file compiles and runs on Linux SITL, Pico 2 W, and XuanTie E906 with zero `#ifdef`s.
- [x] **Eliminated GPS UART Debug Corruption**:
  - [x] Decoupled debug logging from dedicated GPS serial port (`hal::IUart`).
  - [x] Diagnostic messages route strictly to system stdout / trace buffer, keeping the binary UBX line clean.

### 6. Visualizer Studio & Platform Topology Observability (`tools/visualizer/abstractx_studio.py`)
- [x] **Platform Topology Specification & C++ API**:
  - [x] Create authoritative design specification [`docs/ABSTRACTX_PLATFORM_TOPOLOGY_AND_METRICS_SPEC.md`](docs/ABSTRACTX_PLATFORM_TOPOLOGY_AND_METRICS_SPEC.md).
  - [x] Implement [`include/abstractx/platform_topology.hpp`](include/abstractx/platform_topology.hpp) with `PlatformTopologyTable`, `PlatformArch`, and interconnect definitions.
  - [x] Wire `PlatformTopologyTable` into `abstractx::Config` in [`include/abstractx/abstractx.hpp`](include/abstractx/abstractx.hpp).
- [x] **Multi-Window Visualizer Studio Architecture**:
  - [x] **Window 1 (Platform Topology & Silicon Interconnect Fabric)**: Auto-displays detected silicon topology (Linux SITL, Linux+E906, Linux+E906+FPGA, RP2350, ESP32-P4), active cores, and transport interconnects.
  - [x] **Window 2 (Dual-Plane Timeline & Source Code Scanner)**: Visualizes separation between low-level hardware I/O driver context (Plane 1) and cooperative C++20 coroutine tasks (Plane 2), with interactive source code scanner jumping directly to the exact file and line number.
  - [x] **Window 3 (Per-Processor SPU/CPU & Process Utilization)**: Tracks host Linux CPU% and external OS processes, XuanTie E906 active vs WFI sleep duty cycles, and FPGA logic LUT / DMA bandwidth.
- [x] **Dynamic External Domain Plugin Architecture (`--plugin <path>`)**:
  - [x] Decouple application domain UI/configurators from Core Studio; plugins live in external repositories (e.g. `tcmichals/inav`, `tcmichals/python-imgui-esc-configurator`).
  - [x] Support simultaneous multi-plugin loading (`--plugin p1.py --plugin p2.py`) with dedicated tabs.
- [x] **CI Quality Gate Stabilization & Automated Benchmarking**:
  - [x] Guard untracked parallel targets in CMake (`test_zynq_target`, `check_register_parity.py`).
  - [x] Gate visualizer tests under `_studio_deps` and add `numpy` to CI environment.
  - [x] Initialize and push orphan `origin/gh-pages` branch for `benchmark-action/github-action-benchmark`.
- [ ] **Python Package Management Modernization with `uv`**:
  - [ ] Update `tools/setup_venv.sh` to auto-detect and use `uv` for sub-second virtualenv creation.
  - [ ] Adopt `uv` in `.github/workflows/spectrace_dashboard.yml` to reduce dependency installation time.
- [ ] **Dear ImGui Oscilloscope Optimization**:
  - [ ] Benchmark and verify 60-120 FPS real-time plotting under high data rates (8 kHz IMU stream).
- [ ] **GPS 3D Track Visualization**:
  - [ ] Validate real-time UBX-NAV-PVT track rendering and position history.
- [ ] **Queue Watermark Gauges**:
  - [ ] Add live saturation and watermark gauges for inter-domain TLP rings.

### 7. Flight Avionics I/O Pipeline, Multi-Device Auto-DMA & Bus Recovery (`[SPEC-HAL-08]`, `[SPEC-HAL-09]`, `[SPEC-HAL-13]`, `[SPEC-HAL-14]`)
- [ ] **I2C 9-Clock Recovery & Stuck SDA Clearing (`II2c::recover_bus_async`) `[SPEC-HAL-09]`**:
  - [ ] Implement `capabilities_.bus_recovery = true` and dynamic GPIO re-muxing in `targets/allwinner_e906/src/i2c.cpp`.
  - [ ] Sequence: Soft-reset TWI (`TWI0_SRST = 1`), re-mux SCL/SDA to software GPIO (`Pn_CFG`), clock SCL up to 9 cycles at 100 kHz via `csrr rdtime` until SDA returns high, issue STOP condition, and restore TWI mode.
  - [ ] Port parity recovery sequence to RP2350 Pico 2 (`targets/pico2w_rp2350/src/i2c.cpp`) using SIO open-drain bitbang.
  - [ ] Implement FPGA I2C bus recovery via 64-byte control TLP (`Type 0x05`, Command `0x09` `RESET_9CLK`), enabling E906/host `co_await i2c.recover_bus_async()` calls to trigger the FPGA hardware `IOBUF` 9-pulse unstick FSM over Dual-SPI.
- [ ] **Multi-Device Chained SPI Auto-DMA (`[SPEC-HAL-13]`)**:
  - [ ] Extend `include/abstractx/hal/spi.hpp` with `TriggeredTarget` struct and `arm_chained_triggered_read()` to support multi-device chained DMA on a single SPI controller.
  - [ ] **Allwinner E906 (`targets/allwinner_e906/src/spi.cpp`)**: Implement Sunxi DMA scatter-gather linked-list descriptors (`sunxi_dma_lli_t`) linking Descriptor 0 (IMU 1 on CS0, 13 bytes) directly to Descriptor 1 (IMU 2 on CS1, 13 bytes) for deterministic $20.8\,\mu\text{s}$ back-to-back streaming with 0% CPU load.
  - [ ] **RP2350 Pico 2 (`targets/pico2w_rp2350/src/spi.cpp`)**: Configure hardware DMA channel chaining (`channel_config_set_chain_to()`) and PIO state machine arbiter.
  - [ ] **ESP32-P4 (`targets/esp32p4/src/spi.cpp`)**: Configure Event Task Matrix (ETM) and GDMA linked-list descriptors (`gdma_link_list_t`).
- [ ] **Multi-Transaction SPI & I2C Bus Locking (`BusLock` / `BusLockGuard` `[SPEC-HAL-14]`)**:
  - [ ] Validate intrusive FIFO waiter queue (`bus_lock_.wait()`) and `.under(guard)` across concurrent coroutines sharing SPI0/TWI0.
- [ ] **QMTECH Zynq-7000 / FPGA Parallel Dual-SPI Architecture**:
  - [ ] Instantiate dedicated dual parallel AXI SPI master cores in FPGA fabric, eliminating bus sharing and sampling both IMUs at the exact same nanosecond with zero phase jitter.
  - [ ] Package dual-IMU bursts into aligned 64-byte TLP frames (`Tlp64`) streamed directly into host DDR SPSC ring buffers via AXI DMA.

---

## 🔍 Code Review Findings (2026-10-13 / 2026-10-14)

Fixed (see `engineering_log.md` 2026-10-13):
- [x] Coroutine frames were never freed (bump allocator, no-op `operator delete`); pool exhausted in long-running loops.
- [x] HAL awaiters (SPI/I2C/UART/mailbox/IMU sample) consumed whichever completion was next instead of their own; inline `resume()` on `QueueFull`.
- [x] Dispatcher silently dropped wake-ups when its 32-entry queue was full; now counted in `Dispatcher::dropped_resumes()`.
- [x] Linux io_processor SPI bridge handed a loop-local TLP span to the worker thread (ASan stack-use-after-return).
- [x] `cpputest_test_sitl_imu` drove coroutines incorrectly (re-resumed outer task).

Fixed (see `engineering_log.md` 2026-10-14):
- [x] E906 trap entry classified traps by `mstatus` instead of `mcause`: every fault hung in a trap loop with no crash dump.
- [x] `asp_axi_lite_bridge.sv` multi-driven `egr_buf_valid` / `reg_irq_status` (Yosys: conflicting drivers); added its first testbench.
- [x] `run()` idled with ready coroutines; E906 check-then-`wfi` race; `spawn()` dropped tasks silently.
- [x] Cocotb targets `sim-test-reg-bank` / `sim-test-tlp64-top` never compiled a design; IMU auto-DMA and AXI-Lite tests now registered.
- [x] Duplicate unused E906 trace buffer (4 KB).

Fixed / decided (see `engineering_log.md` 2026-10-14, E906 memory map):
- [x] E906 linker scripts were shifted +256 KB from the silicon map (Space 0 at `0x40000000`, "Space 1" at nonexistent `0x40040000`); `.resource_table`/`.trace_buffer` were outside every window `sunxi_rproc` accepts, so remoteproc would refuse the firmware. Restored to `0x3FFC0000` / `0x40000000` (verified against `linux-cubie` `sunxi_rproc.h`); `tools/check_e906_layout.py` now fails the build on any out-of-window segment.
- [x] Stale shared-memory headers (`shm_ping_protocol.h`, `dram_spsc_protocol.h`) pointed at the CFG register block (`0x07130000`/`0x07131000`); synced to cubie-a5e's silicon-tested versions. Unused `IPC_*` offsets that overlaid the vector table removed.
- [x] E906 D-cache policy decided: OFF (I-cache on) because all firmware, IPC and DMA data live in SRAM ([SPEC-E906-CACHE-02]); matches cubie-a5e.
- [x] E906 `platform_send_telemetry()` no longer writes to `0x07131000` (outside SRAM); drops and counts until the RPMsg transport lands.

Remaining:
- [x] Added ASan/UBSan host gate, full ctest suite, and benchmark export to CI workflow (2026-10-17).
- [ ] Fail the configure step clearly when `third_party/etl` is not initialised (currently a missing-header error); document `git submodule update --init third_party/etl`.
- [x] `Task<T>`: `co_await` on a Task whose frame allocation failed dereferenced a null handle; now reports through `set_pool_exhausted_hook()` and traps ([SPEC-ARCH-09], 2026-10-28). (`std::function` / `exception_ptr` were removed earlier.)
- [x] Linux `LinuxIoProcessor` keeps a single `pending_spi_tag_` / `bridge_rx_buf_`; fixed with in-flight descriptor pool `[SPEC-LINUX-SPI-01]` (2026-10-17).
- [x] Removed synchronous SPI HAL fallbacks (`transfer_sync`, `transfer_byte`) and `Icm42688p::init()` across all platforms (2026-10-17).
- [ ] README: remove or substantiate unmeasured claims ("formally verified", 15-cycle switch, <100 B frames, sub-16 KB), and shorten it.
- [x] Move `docs/tier1_vision/GEMINI_*_PROMPT.md` to `docs/prompts/`; remove the empty `hw/zynq7000/buildroot_external/REAMME`; remove the old `cmake/toolchain-*.cmake` (use `cmake/toolchains/`). Done 2026-10-23 (`docs/design/repo-file-audit.md`).
- [ ] Fix `-Wall -Wextra` warnings (examples, `sim/sitl_coro_sim.cpp`, `targets/linux/include/i2c_worker.hpp:182`).
- [x] **Linux-target data races (ThreadSanitizer)** fixed 2026-10-15: hosted `InterruptLock` is a real recursive lock; dispatcher/driver use locked queue calls on hosted builds; test-side races fixed. TSan 30/30.
- [x] **Zero-copy trace transport ([SPEC-TRACE-07])** implemented and host-verified (2026-10-17):
  1. [x] Trace engine: replaced private buffer pool with `ITraceTransport` `acquire(max)` / `commit(span, used)` interface; `StaticPoolTraceTransport` for Linux/SITL; host tests in `test_trace_zero_copy.cpp`.
  2. [x] E906 ([SPEC-E906-IPC-04]): implemented `acquire()` / `commit()`, RPMsg endpoints, verified by `sim/test_e906_rpmsg_endpoints.cpp`.
  3. [x] Device tree: `apps/gps_imu_app/platforms/allwinner_e906/cubie-a5e-abstractx-rpmsg-sram.dtso` (pool `0x072E0000`, 128 KB; compiles with dtc; tagged `@impl [SPEC-E906-IPC-04]`).
  4. [x] Linux reader: `tools/e906_rpmsg_bridge.py` (`trace` → UDP 9870; `tlp` ↔ UDP 9871/9872), host-tested 2026-10-15.
  5. [x] Pico 2 W ([SPEC-PICO-TRACE-01]) and ESP32-P4 ([SPEC-ESP32P4-TRACE-01]): `PicoUdpTraceTransport`, `hal_platform.cpp`, elided static pool on Pico 2 W `[SPEC-PICO-TRACE-01]`, verified by `test_pico_zero_copy_trace.cpp`.
  6. [ ] Board test on the Cubie A5E: trace reaches the Studio; measure E906 loop jitter with `mcycle` (awaiting physical hardware bring-up).
- [x] **E906 trap entry FPU state preservation & crash autopsy** fixed (2026-10-17, `[SPEC-E906-CRASH-01]`): allocated 416-byte `CrashFrame`, saving and restoring all 32 64-bit FP registers (`f0`..`f31`), `fcsr`, CSRs, and integer GPRs on trap entry/exit, preventing FP register corruption on ISR return with full autopsy logging.
- [ ] **Pico Core 1 is never launched** (`platform_launch_processing_domain()` has no callers): `[SPEC-ARCH-05]` dual-core split is not implemented; the ISR's SIO FIFO doorbell has no reader. The cross-core lock it needs is in place since 2026-10-28 (`SmpInterruptLock`, [SPEC-ARCH-01]).
- [ ] **ESP32-P4 target is a stub** (141 lines; no io_processor/platform). Mark it as such in README/TODO.
- [ ] Trace flushes in `step()` do blocking `fwrite`+`flush` and UDP send in the loop (AGENTS.md §7).
- [ ] `targets/allwinner_e906/src/rpmsg.cpp` (502 lines) is not in the E906 CMake source list, so it is never compiled. Toolchain files hardcode `/home/tcmichals/...`; use `$ENV{HOME}`.
- [x] RTL: Added cocotb testbenches for `asp_axi_lite_bridge`, `asp_imu_auto_dma`, `asp_dshot_core` (12M, 27M, 50M), and `asp_neopixel_core` (12M, 27M, 50M). Remaining: `asp_top` reset synchronizer, `asp_axi_dma`, `asp_ila_trace`, `asp_pwm_decoder`.
- [ ] Add a synthesis-level RTL check to CI: `yosys -p "read_verilog -sv <file>; proc; check -assert"` per module, plus `verilator --lint-only -Wall`.
- [x] `tools/audit_specs.py` and `tools/spectrace_drift.py` modernized: repository-wide multi-spec discovery, zero drift (100.0% spec-to-code coverage), and multi-format scanning across `.hpp`, `.cpp`, `.h`, `.c`, `.sv`, `.py`, `.dts`, `.dtso`, `.yaml`, `.yml`, `.ld`, `.S`, `.s`.
- [x] **SpecTrace Enterprise Requirements Engineering & Parity Engine**:
  - [x] **Device Tree & Config YAML Traceability (`[SPEC-AUDIT-08]`)**: Integrated `.dts`, `.dtsi`, `.dtso`, `.yaml`, `.yml` into scanner engines and pre-commit change reviews.
  - [x] **Requirement Derivation Hierarchy (`Refines: [SPEC-*]`) (`[SPEC-AUDIT-06]`)**: Requirements form a directed acyclic graph (DAG) linking system invariants down to tier-2 and application contracts (22 linked derivation nodes).
  - [x] **Formal Verification Methods (`Method: Test | Analysis | Inspection | Demonstration`) (`[SPEC-AUDIT-06]`)**: Formal compliance with DO-178C / ISO 26262 classifications.
  - [x] **Interactive Mermaid Traceability DAG Export (`--graph [TAG]`) (`[SPEC-AUDIT-07]`)**: Outputs visual dependency graphs connecting requirements to code locations, tests, and evidence verdicts.
  - [x] **OMG ReqIF XML Interchange (`--export-reqif <file.reqif>`) (`[SPEC-AUDIT-10]`)**: Exports requirements, hierarchy, methods, and implementation anchors into standard OMG ReqIF 1.0/1.2 XML for corporate ALM tools (DOORS, Polarion, Capra, StrictDoc).
  - [x] **Multi-Format Code Scanner Support**: Added `.s`, `.S`, and `.ld` comment scanning across audit engines.
- [ ] **Tang Nano 20K real pins**: assign link, IMU, 4 DShot, 6 PWM, NeoPixel, reset in `hw/tang_nano20k/tang_nano20k.cst` (the real bitstream needs every pin). Fit on the real part is already measured with the fit-only pins (22% LUT, 121.85 MHz routed).
- [ ] **Resync `targets/allwinner_e906/include/shm_ping_protocol.h`** after the cubie-a5e protocol change lands (2026-10-15: A55->E906 doorbell write removed because it never reached the E906 FIFO under UIO; SRAM sequence number only; E906->A55 doorbell coalesced). AbstractX keeps a verbatim copy for interoperability with cubie-a5e's Linux tools.
- [ ] **Tang Nano 20K A5E test target** (`hw/tang_nano20k/SPECIFICATION.md`): board top, `.cst` from the Sipeed schematic, `hw/Makefile` target, fit check, bench bring-up. Open: A5E header SPI controller and FPGA header pins.
- [ ] FPGA FIFOs map to LUT/FF, not block RAM (BSRAM 0 used on GW2A-18): check inference for `asp_axis_fifo` if area grows.
- [ ] Studio GUI shows hardcoded demo metrics (e.g. `coro_pool_used_bytes = 23040`, a fixed "23.4 µs overrun") next to live data; label simulated values.

## 🔍 Code Review Findings (2026-10-28)

Fixed (see `engineering_log.md` 2026-10-28; the upstream tracker EL-1027-NN in the 2026-10-27 entry covers the same review from the other PC):
- [x] `when_any`: a loser that never started was detached and never freed (~128 B per call; pool exhausted after ~470 calls, then a null-handle segfault). `Task::detach()` now destroys never-started tasks ([SPEC-ARCH-11]).
- [x] `Semaphore`: a waiter woken by `release()` also took the next permit, so two releases woke it once ([SPEC-ARCH-10]).
- [x] Awaiting an empty `Task` (pool exhausted) now reports and traps ([SPEC-ARCH-09]).
- [x] Three copied io_processors replaced by `IoEngine` ([SPEC-ARCH-13]). Bugs that went with them: tx buffer (32 B) clocked for up to 64 B; host `tx_len` (<= 255) used as the offset into a 64 B rx buffer; one shared buffer and `active_auto_ch_` for all channels; a trigger restarting a DMA in flight; config rewritten while the ISR read it; E906 started every auto channel on any edge; E906/Pico answered no SPI/I2C/GPIO bridge request (bridge TLPs are MemRead/MemWrite and hit the virtual-BAR branch first); Linux SPI window check could never match; Pico auto-DMA completion was never called; GPIO `1 << pin` with pin >= 32.
- [x] Linux `SpiWorker`: unequal tx/rx buffers let spidev overflow rx (now `InvalidArgument`); `speed_hz_` raced with the worker; `stop()` stranded queued requests (9/200 rounds; now completed `Cancelled`, also `I2cWorker`).
- [x] E906 `Spi1::start_dma_transfer`: a second caller overwrote the in-flight LLI and callback; now refused. (The same bug in `Uart2::write_async_dma` / `AsyncTxAwaiter` went away with UART2, removed upstream in `15f125c`.)
- [x] Pico 2 W did not compile: `MpscRingBuffer<uint8_t, 512>` exceeded ETL's small memory model; the model now follows the capacity.
- [x] `UartRxRing` / `TriggerRing` lost wakes (store->load handshake without a seq_cst fence; `sim_test_uart_rx_ring` hung ~1% of runs, TSan silent). Fenced ([SPEC-HAL-10], [SPEC-HAL-13]); `sim/test_wake_handshake.cpp`; `boundary_review.py` now checks waiter slots (`HANDSHAKE`). E906 `Uart2::AsyncRxPacketAwaiter` had the same race (UART2 removed upstream, `15f125c`); dead E906 `Timer::set_compare` path removed; FreeRTOS soft-IRQ enable order; bounded `Trace::putc` LSR spin.
- [x] Eleven sim tests ran with `assert()` compiled out (host Release `-DNDEBUG`); `-UNDEBUG` for sim/tests/examples and `sim_test_asserts_live` guard.
- [x] All 22 shared variables without an ownership note answered (`boundary_review.py --check`: 0 missing, 0 handshakes).
- [x] E906 Release was really `-O3` (CMake appends its default after `*_RELEASE_INIT`); `e906_repl_app` overflowed SRAM_FAST by 6,256 B at `c5a1eaa`. The toolchain now seeds `-O2`; all E906 apps link (`e906_repl_app` SRAM_FAST 97.3%). **Not run on the board yet.**

Open:
- [x] Board run of the `-O2` E906 firmware (2026-10-28): 31 passed, 2 failed (both fail on the previous firmware too), 2 skipped (bench). Found and fixed: no RPMsg channel announced since `d8bcbac` (firmware refused the board's DDR `vdev@48000000` pool, [SPEC-E906-IPC-02]).
- [ ] Board: `test_streamer_keeps_running_with_heartbeat` fails with "NMEA sentences failed their checksum" (also on the 2026-10-24 firmware): UART3 GPS stream corrupt after ~25 s (DMA ring overrun? baud?). Investigate on the board.
- [ ] Board: `test_profiler_and_timeline_when_idle` fails, "1021 idle spans: sample interrupts woke the loop" (1028 on the 2026-10-24 firmware): profiler sample interrupts end the idle wait ([SPEC-E906-IRQ-06]).
- [ ] Board: `e906_bench_app` ran on 2026-10-29 (`R` + echo + `A`, exit 0); still to run: `e906_freertos_app`, and the rest of EL-1027-30 (serial_loopback on header pins, TWI1 new CCR).
- [ ] E906 SRAM_FAST budget for `e906_repl_app` in `[SPEC-E906-MEM-01]` (EL-1027-29).
- [x] `delay_*` / `sleep_*` / `wait_until` moved to `examples/hand_clocked.hpp` (they keep no handle); TimerService removed; refused timer sleep reports `Error`; lint check for dropped handles ([SPEC-ARCH-10]).
- [x] `CoroutineIoEngine::poll_completions()` posts through the Dispatcher; second stream waiter refused; lint check 6 (every `.resume()` needs a `// resume:` reason) ([SPEC-ARCH-10]).
- [x] Pico 2 W cross-core lock: `SmpInterruptLock` (per-core depth/saved state + shared spinlock), `_from_isr` paths locked on SMP, `ABSTRACTX_SMP_CORES=2` on the Pico ([SPEC-ARCH-01]); `sim/test_smp_lock.cpp`. Not run on RP2350 silicon.
- [x] `inav_e906_app` removed upstream (`2b3970a`, EL-1027-02). Was: no `SPECIFICATION.md`; links `malloc`/`strtod` (`atof` in the NMEA parser); `now_ns = ticks * 1000` (24 MHz ticks, ~24x off); its own ICM-42688 / compass / GPS drivers duplicate `applib/` and are not `IImuDriver`/`IMagDriver`; superloop with blocking SPI/I2C and `delay_ms`.
- [ ] `e906_repl_app` already links `_malloc_r` / `free` (also in the 2026-10-25 build): find the caller. Same as EL-1027-31 (deferred by the project owner, 2026-10-27).
- [x] Fixed upstream (`2b3970a`, `75001f8`, `15f125c`; EL-1027-03..05, -10): one TWI driver, one UART3 driver. Was: E906 drivers duplicated: UART3 in `platform.cpp`, `e906_periph.cpp` and INAV `GpsUartDriver`; TWI1 in three places. `platform.cpp` `E906Twi1Driver` sets PB0/PB1 function 4 (UART2's pins) and a 200 kHz divider; the tested `e906_periph.cpp` uses PB4/PB5 function 2, `0x12` for 400 kHz. `E906Uart3Driver::write_byte` spins on LSR with no bound.
- [ ] DShot register map: `pcie_bar_map.hpp` `reg::esc` puts Motor1 at 0x08 (RTL: throttles at 0x10..0x1C, no telemetry registers); `asp_tlp64.h` `ASP_ADDR_DSHOT_BASE` 0x40000300 is the PWM decoder (RTL DShot is 0x40000200). `check_register_parity.py` does not cover it.
- [ ] PIO / ESC pin mux (in progress upstream as EL-1027-20, [SPEC-PIO-*]; check `rtl/pio/SPECIFICATION.md` before working on it): `asp_top` leaves `u_pio_tile.o_gpio_out` / `o_gpio_pindir` unconnected; motor pins are output-only from `asp_dshot_core` (no output enable, no DShot / serial passthrough / PIO select register, no bidirectional DShot receive); Zynq top has no PIO; no C++ driver for DShot, PIO or the mux. Spec the pin-ownership state machine first (`rtl/pio/SPECIFICATION.md`, Tang Nano 20K and Zynq specs).
- [x] PIO tile moved to its own repo, [pio2](https://github.com/tcmichals/pio2) (2026-10-29), used here as the `rtl/pio` submodule: RTL, programs, `[SPEC-PIO-*]` spec, cocotb tests, pioasm/DShot tools. PIO work (incl. the pin mux above and the visual debugger, `rtl/pio/docs/debugger.md`) continues there.
- [ ] Other PC: rtl/pio is now a submodule; port any local PIO changes made since `559022f` to pio2 before pulling.
- [x] pio2 tile register port is AXI4-Lite: AbstractX connects through `rtl/asp_pio_wrap.sv` (Wishbone classic -> ZipCPU `wbm2axilite` -> tile, [SPEC-TLP-05]); third-party licenses in `docs/THIRD_PARTY.md`. Clone with `--recurse-submodules` (nested `rtl/pio/third_party/`).
- [ ] Python duplicates: `flight_sensors.py`, `sensor_contract.py` exist in `applib/python/drivers/` and `apps/e906_repl_app/`. Linux `spi_worker.hpp` / `hal_uart.cpp` embed ICM-42688 / UBX simulators in the target HAL (move to `sim/`).
- [x] ESP32-P4 stubs refuse (SPI/mailbox `NotSupported`, sleep `Error`; timer stub was abstract) ([SPEC-ESP32P4-HAL-00], `sim/test_esp32p4_stubs.cpp`).
- [ ] ESP32-P4: set `ABSTRACTX_SMP_CORES=2` + `abstractx_core_id()` when both cores share queues; no ESP-IDF build has been run.
- [ ] SpecTrace (EL-1027-32): 65 `@verifies` claims after the merge with upstream with no test behind them (`--check-verifies`); `audit_specs.py` only reads `docs/DESIGN_SPECIFICATION.md` (47 of 221 requirements).

---

## 🎯 Staged Hardware Bring-up & Field Testing Roadmap

### Zynq-7000 AbstractX IMU Validation Proof of Concept

* [x] Implement the shared ICM-42688-P test suite and selectable
  Linux GPIO-event → spidev / AbstractX PL DRDY → SPI → Auto-DMA backends
  specified in
  [`hw/zynq7000/testApps/imu_backend_validation_poc/SPECIFICATION.md`](hw/zynq7000/testApps/imu_backend_validation_poc/SPECIFICATION.md).
  Host unit tests and the focused AbstractX IMU Auto-DMA RTL regression are in
  place; hardware validation remains open.
* [ ] Verify the board-specific PS GPIO line for IMU DRDY and a safe way to
  route the same sensor between PS SPI1 and PL SPI.
* [ ] Run both backends on the same board, sensor, configuration, and duration;
  preserve JSON results and report sample loss, latency, throughput, and CPU
  cost.
* [ ] Write the accompanying article only after real measurements exist.

Target execution follows a 3-stage validation progression:

### Stage 1 (Recommended First Baseline): Raspberry Pi Pico 2 W (RP2350)
* **Detailed Guide**: [`apps/gps_imu_app/platforms/pico2w/HOWTO.md`](apps/gps_imu_app/platforms/pico2w/HOWTO.md)
* **Rationale**: Single address space (520 KB SRAM), zero OS/kernel layers, instant flashing (~2s via Picoprobe SWD / UF2). Ideal for isolating and proving C++20 coroutine rate loops and sensor drivers first.
* [ ] Wire ICM-42688-P (SPI1 GP10-GP13, DRDY GP20) and U-Blox GPS (UART0 GP0/GP1).
* [ ] Build target:
  ```bash
  cmake -B build-pico2w -S . -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake -DABSTRACTX_TARGET=pico2w -DPICO_PLATFORM=rp2350-arm-s -DPICO_BOARD=pico2_w -DWIFI_SSID="YourNetwork" -DWIFI_PASSWORD="YourPassword"
  cmake --build build-pico2w --target gps_imu_app -j$(nproc)
  ```
* [ ] Flash `gps_imu_app.uf2` via USB BOOTSEL or SWD probe.
* [ ] Validate Core 0 (CYW43439 Wi-Fi UDP port 9870 telemetry stream) and Core 1 (C++20 coroutine Mahony fusion).
* [ ] Verify live CTF 1.8 frames in `abstractx_studio.py`.

### Stage 2 (High-Throughput Microcontroller): Espressif ESP32-P4
* **Detailed Guide**: [`apps/gps_imu_app/platforms/esp32p4/HOWTO.md`](apps/gps_imu_app/platforms/esp32p4/HOWTO.md)
* **Rationale**: High-compute dual-core 400 MHz RISC-V with hardware FPU in a pure microcontroller environment (no Linux complexity). One-cable USB-Serial-JTAG debugging.
* [ ] Complete ESP-IDF HAL platform bindings (`targets/esp32p4/`) for SPI2 GDMA auto-burst and I2C0.
* [ ] Build and flash via `idf.py -p /dev/ttyACM0 flash monitor`.
* [ ] Validate 8 kHz IMU GDMA rate and Wi-Fi/Ethernet telemetry egress.

### Stage 3 (Heterogeneous Linux AMP): Radxa Cubie A5E (Allwinner A527 / E906)
* **Detailed Guide**: [`apps/gps_imu_app/platforms/allwinner_e906/HOWTO.md`](apps/gps_imu_app/platforms/allwinner_e906/HOWTO.md)
* **Rationale**: Production hybrid architecture (Linux flight supervisor on Cortex-A55 + hard real-time I/O reactor on XuanTie E906 RISC-V).
* [ ] Deploy compiled `cubie_a5e_e906_overlay.dtbo` to `/boot/dtb/overlay/`.
* [ ] Configure `/boot/config.txt`:
  ```text
  dtoverlay=cubie_a5e_e906_overlay
  cmdline=uio_pdrv_genirq.of_id=generic-uio clk_ignore_unused
  ```
* [ ] Verify onboard AIC8800 Wi-Fi 6 comes up (`wlan0`) via MMC1 power regulators.
* [ ] Copy `build-e906/apps/e906_coprocessor/e906_coprocessor.bin` to `/lib/firmware/` and launch via RemoteProc:
  ```bash
  echo "e906_coprocessor.bin" > /sys/class/remoteproc/remoteproc0/firmware
  echo start > /sys/class/remoteproc/remoteproc0/state
  ```
* [ ] Validate shared SRAM A3 (`0x07280000`) lock-free rings and MSGBOX doorbells.

---

## 📋 Completed Milestones

- [x] **Milestone 6**: Universal Cross-Platform HAL, I2C, Atomic GPIO TLP & Single `gps_imu_app` (2026-09-11)
- [x] **Milestone 5**: 64-Bit Master Timestamp, PCIe Identity, 4-Channel DShot & Python VIP (2026-08-11)
- [x] **Milestone 4**: DShot150/300/600 & NeoPixel IP Cores on Wishbone Switch Fabric (2026-08-11)
- [x] **Milestone 3**: Portable Flight Stack & Decoupled PCIe Architecture (2026-08-11)
- [x] **Milestone 2**: QMTECH Zynq-7020 Bring-up Pivot & Early Bitstreams (2026-08-10)
- [x] **Milestone 1**: Initial Project Setup, Dual-License Model, and AXIS Pipeline Migration (2026-04-19)
