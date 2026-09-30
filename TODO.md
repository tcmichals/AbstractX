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
  - [x] Route E907 trace TLPs to shared SRAM (`0x07280000` ARM64 physical / `0x3FFC0000` E907 local).
  - [x] Assert `sun6i-msgbox` hardware doorbell interrupt to notify Linux.
- [x] **E907 Device Tree Overlay (`cubie_a5e_e907_overlay.dtso`)**:
  - [x] Standardized overlay extension to `.dtso` for kernel Kbuild / Buildroot compatibility.
  - [x] Configured onboard AIC8800 SDIO Wi-Fi 6 power regulators (PL7 `3v3-wifi`, PM1 `wifi-en`) and `&mmc1` node.
  - [x] Bound `&spi0`, `&spi1`, `&uart2`, and `&i2c1` to `generic-uio` with default `pinctrl` so Linux configures clocks/pins without driver contention.
  - [x] Added automated `e907_dtbo` compilation target in CMake.
- [x] **Linux Ingestion Coroutine (`linux_trace_receiver_task`)**:
  - [x] Map shared SRAM via `/dev/uio` or `/dev/mem`.
  - [x] Await MSGBox doorbell `eventfd` non-blockingly and drain 64-byte TLPs into the configured host sink (`UdpTraceSink` or `FileTraceSink`).
- [ ] **SRAM-to-UDP Bridge Daemon**:
  - [ ] Adapt `/home/tcmichals/ssdData/projects/home/CubieA5E/cubie-a5e/firmware/e907-riscv/apps/testStringBinaryTrace0/fast_sram_telemetry.py` to read popped SRAM telemetry packets and forward them over UDP (`sock.sendto(data, ("127.0.0.1", 9870))`).
  - [ ] Wire to `/dev/uio0` for zero-overhead doorbell wakeups.

### 5. Application Modernization (`apps/gps_imu_app/`)
- [x] **Refactor to Unified `abstractx::` API**:
  - [x] Replace manual thread spawning and loop pumping in `apps/gps_imu_app/src/main.cpp` with `abstractx::init()` and `abstractx::run()`.
  - [x] Transition application to a 100% event-driven loop with zero manual byte polling or `platform_poll_network()` calls.
  - [x] Validate that the exact same application file compiles and runs on Linux SITL, Pico 2 W, and XuanTie E907 with zero `#ifdef`s.
- [x] **Eliminated GPS UART Debug Corruption**:
  - [x] Decoupled debug logging from dedicated GPS serial port (`hal::IUart`).
  - [x] Diagnostic messages route strictly to system stdout / trace buffer, keeping the binary UBX line clean.

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

---

## 🎯 Staged Hardware Bring-up & Field Testing Roadmap

Target execution follows a 3-stage validation progression:

### Stage 1 (Recommended First Baseline): Raspberry Pi Pico 2 W (RP2350)
* **Detailed Guide**: [`apps/gps_imu_app/platforms/pico2w/HOWTO.md`](apps/gps_imu_app/platforms/pico2w/HOWTO.md)
* **Rationale**: Single address space (520 KB SRAM), zero OS/kernel layers, instant flashing (~2s via Picoprobe SWD / UF2). Ideal for isolating and proving C++20 coroutine rate loops and sensor drivers first.
* [ ] Wire ICM-42688-P (SPI1 GP10-GP13, DRDY GP20) and U-Blox GPS (UART0 GP0/GP1).
* [ ] Build target:
  ```bash
  cmake -B build-pico2w -S . -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-pico2w.cmake -DABSTRACTX_TARGET=pico2w -DPICO_PLATFORM=rp2350-arm-s -DPICO_BOARD=pico2_w -DWIFI_SSID="YourNetwork" -DWIFI_PASSWORD="YourPassword"
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

### Stage 3 (Heterogeneous Linux AMP): Radxa Cubie A5E (Allwinner A527 / E907)
* **Detailed Guide**: [`apps/gps_imu_app/platforms/allwinner_e907/HOWTO.md`](apps/gps_imu_app/platforms/allwinner_e907/HOWTO.md)
* **Rationale**: Production hybrid architecture (Linux flight supervisor on Cortex-A55 + hard real-time I/O reactor on XuanTie E907 RISC-V).
* [ ] Deploy compiled `cubie_a5e_e907_overlay.dtbo` to `/boot/dtb/overlay/`.
* [ ] Configure `/boot/config.txt`:
  ```text
  dtoverlay=cubie_a5e_e907_overlay
  cmdline=uio_pdrv_genirq.of_id=generic-uio clk_ignore_unused
  ```
* [ ] Verify onboard AIC8800 Wi-Fi 6 comes up (`wlan0`) via MMC1 power regulators.
* [ ] Copy `build-e907/apps/e907_coprocessor/e907_coprocessor.bin` to `/lib/firmware/` and launch via RemoteProc:
  ```bash
  echo "e907_coprocessor.bin" > /sys/class/remoteproc/remoteproc0/firmware
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
