# AbstractX Project TODO & In-Flight Roadmap

This document tracks active engineering tasks, upcoming milestones, and verification targets for the AbstractX framework.

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
- [x] **Runtime Verification Suite**: Created [`sim/test_abstractx_runtime.cpp`](sim/test_abstractx_runtime.cpp) (verified 100% passing across 23/23 CTest targets).


### 2. `IIoProcessor` Coroutine & Init Interface Enhancement
- [x] **Update Interface (`include/abstractx/hal/io_processor.hpp`)**:
  - [x] Add `bool init(const IoProcessorSetup& setup)` taking startup channel mappings, ring pointers, and trace sink config.
  - [x] Add `Task<void> run_coroutine()` to allow the I/O processor reactor to execute cooperatively inside the main coroutine loop.
- [x] **Target Implementations**:
  - [x] Update Linux target (`targets/linux/src/io_processor.cpp`) to implement `run_coroutine()` via `epoll_reactor` awaiters.
  - [x] Update E907 target (`targets/allwinner_e907/src/io_processor.cpp`) with `init()` and cooperative coroutine execution.
  - [x] Update RP2350 target (`targets/pico2w_rp2350/src/io_processor.cpp`) with Core 0 `run_coroutine()` runner.

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

### 4. Heterogeneous Co-Processor Pipeline (E907 to Linux) `[SPEC-TRACE-05]`
- [x] **E907 Co-Processor Egress**:
  - [x] Route E907 trace TLPs to shared SRAM (`0x40000000`).
  - [x] Assert `sun6i-msgbox` hardware doorbell interrupt to notify Linux.
- [x] **Linux Ingestion Coroutine (`linux_trace_receiver_task`)**:
  - [x] Map shared SRAM via `/dev/uio` or `/dev/mem`.
  - [x] Await MSGBox doorbell `eventfd` non-blockingly and drain 64-byte TLPs into the configured host sink (`UdpTraceSink` or `FileTraceSink`).

### 5. Application Modernization (`apps/gps_imu_app/`)
- [x] **Refactor to Unified `abstractx::` API**:
  - [x] Replace manual thread spawning and loop pumping in `apps/gps_imu_app/src/main.cpp` with `abstractx::init()` and `abstractx::run()`.
  - [x] Transition application to a 100% event-driven loop with zero manual byte polling or `platform_poll_network()` calls.
  - [x] Validate that the exact same application file compiles and runs on Linux SITL, Pico 2 W, and XuanTie E907 with zero `#ifdef`s.

### 6. Visualizer Studio & Platform Topology Observability (`tools/visualizer/abstractx_studio.py`)
- [x] **Platform Topology Specification & C++ API**:
  - [x] Create authoritative design specification [`docs/ABSTRACTX_PLATFORM_TOPOLOGY_AND_METRICS_SPEC.md`](docs/ABSTRACTX_PLATFORM_TOPOLOGY_AND_METRICS_SPEC.md).
  - [x] Implement [`include/abstractx/platform_topology.hpp`](include/abstractx/platform_topology.hpp) with `PlatformTopologyTable`, `PlatformArch`, and interconnect definitions.
  - [x] Wire `PlatformTopologyTable` into `abstractx::Config` in [`include/abstractx/abstractx.hpp`](include/abstractx/abstractx.hpp).
- [x] **Multi-Window Visualizer Studio Architecture**:
  - [x] **Window 1 (Platform Topology & Silicon Interconnect Fabric)**: Auto-displays detected silicon topology (Linux SITL, Linux+E907, Linux+E907+FPGA, RP2350, ESP32-P4), active cores, and transport interconnects.
  - [x] **Window 2 (Dual-Plane Timeline & Source Code Scanner)**: Visualizes separation between low-level hardware I/O driver context (Plane 1) and cooperative C++20 coroutine tasks (Plane 2), with interactive source code scanner jumping directly to the exact file and line number.
  - [x] **Window 3 (Per-Processor SPU/CPU & Process Utilization)**: Tracks host Linux CPU% and external OS processes, XuanTie E907 active vs WFI sleep duty cycles, and FPGA logic LUT / DMA bandwidth.
- [ ] **Dear ImGui Oscilloscope Optimization**:
  - [ ] Benchmark and verify 60-120 FPS real-time plotting under high data rates (8 kHz IMU stream).
- [ ] **GPS 3D Track Visualization**:
  - [ ] Validate real-time UBX-NAV-PVT track rendering and position history.
- [ ] **Queue Watermark Gauges**:
  - [ ] Add live saturation and watermark gauges for inter-domain TLP rings.

### 7. Target Hardware Bring-up & Validation
- [ ] **Raspberry Pi Pico 2 W (RP2350)**:
  - [ ] Physical board validation of Core 0 (I/O + CYW43439 Wi-Fi UDP stream) and Core 1 (Coroutines).
- [ ] **Allwinner XuanTie E907 (Cubie A5E / A7A)**:
  - [ ] Physical validation of TWI0 I2C ISR state machine, SPI DMA burst, and shared SRAM ring with Linux `remoteproc`.
- [ ] **ESP32-P4 Platform Target**:
  - [ ] Implement ESP-IDF HAL driver platform bindings (`II2c`, `ISpi`, `IUart`, `IGpio`).

### 8. Continuous Integration & Regression Testing
- [ ] **Automated CTF Testbench**: Expand `sim/test_tlp_msg.cpp` to verify CTF packet headers, event IDs, and payload serialization.
- [ ] **Runtime Testbench (`sim/test_abstractx_runtime.cpp`)**: Verify `abstractx::init()`, `abstractx::spawn()`, and `abstractx::step()` under simulated event conditions.
- [ ] **Multi-Preset CMake CI**: Ensure all presets build cleanly with 100% specification traceability.

---

## 📋 Completed Milestones

- [x] **Milestone 6**: Universal Cross-Platform HAL, I2C, Atomic GPIO TLP & Single `gps_imu_app` (2026-09-11)
- [x] **Milestone 5**: 64-Bit Master Timestamp, PCIe Identity, 4-Channel DShot & Python VIP (2026-08-11)
- [x] **Milestone 4**: DShot150/300/600 & NeoPixel IP Cores on Wishbone Switch Fabric (2026-08-11)
- [x] **Milestone 3**: Portable Flight Stack & Decoupled PCIe Architecture (2026-08-11)
- [x] **Milestone 2**: QMTECH Zynq-7020 Bring-up Pivot & Early Bitstreams (2026-08-10)
- [x] **Milestone 1**: Initial Project Setup, Dual-License Model, and AXIS Pipeline Migration (2026-04-19)
