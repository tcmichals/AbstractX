# AbstractX Visualizer Specification: The Next-Gen Trace & Coroutine Studio

The **AbstractX Visualizer** is a modern, real-time observability and timeline studio designed to surpass **AbstractX AbstractX Studio** and **Linux LTTng / Trace Compass** by combining **C++20 Coroutine-First Execution Modeling** with **High-Rate Flight Telemetry & TLP Packet Inspection**.

---

## 1. Why AbstractX Visualizer Surpasses Existing Tools

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                 KEY ARCHITECTURAL ADVANTAGES                            │
├──────────────────────────┬─────────────────────────────┬────────────────────────────────┤
│ Feature                  │ AbstractX AbstractX Studio / LTTng│ AbstractX Visualizer Studio    │
├──────────────────────────┼─────────────────────────────┼────────────────────────────────┤
│ Execution Paradigm       │ OS Threads & Tasks only     │ C++20 Asynchronous Coroutines │
│ Suspension Reason Aware  │ Generic blocked state       │ Exact `co_await` token reason  │
│ Flight Telemetry Synced  │ No (separate GCS tool)      │ 100% Synced 8 kHz Gyro & EKF   │
│ Protocol Inspection      │ Raw byte dumps              │ 64-Byte PCIe-style TLP decoder │
│ User Interface Tech      │ Java / Eclipse or Win32     │ WebAssembly / WebGL (120 FPS)  │
│ Deployment Model         │ Heavy native desktop app    │ Zero-install Web & Standalone  │
│ License & Standards      │ Expensive Proprietary       │ Open Standard (barectf/CTF 1.8)│
└──────────────────────────┴─────────────────────────────┴────────────────────────────────┘
```

---

## 2. Multi-Window Visualizer Studio Architecture

The AbstractX Visualizer Studio provides three synchronized, multi-pane windows:

```
+===================================================================================================+
| ABSTRACTX STUDIO | Platform: Linux + XuanTie E906 + FPGA | Target: Radxa Cubie A5E | Stream: 8.2 kHz |
+===================================================================================================+
| [WINDOW 1: PLATFORM TOPOLOGY & SILICON FABRIC]                                                   |
| Architecture: Linux_Host_E906_FPGA | Transport: Shared SRAM A3/C (0x40000000) + sun6i-msgbox     |
| Silicon Cores:                                                                                   |
|  - Core 0 [Host ARM64] : Linux Flight Supervisor (PID, EKF, Telemetry, UDP :9870)                |
|  - Core 1 [RISC-V E906]: Real-Time I/O Reactor (TWI0 I2C, SPI0 DMA, Mailbox Doorbell)            |
|  - Fabric [Gowin FPGA] : IMU Auto-DMA IP, 4-CH DShot300/600 Core, NeoPixel IP                    |
| Interconnect Saturation:                                                                         |
|  - E906 -> Linux SPSC Ring: [████████░░░░░░░░░░░░] 22 / 64 pkts (Avg Doorbell Delay: 1.1 µs)    |
|  - FPGA -> E906 SPI DMA   : [████░░░░░░░░░░░░░░░░] 12 / 64 pkts (Transfer Latency: 0.8 µs)       |
+===================================================================================================+
| [WINDOW 2: DUAL-PLANE EXECUTION TIMELINE & SOURCE CODE SCANNER]                                  |
| Plane 1: I/O Processor & Drivers (E906 PLIC / ISRs & DMA)                                        |
| 00:00.120 [===SPI DMA BURST===]         [==TWI0 I2C ISR==]         [===MSGBOX DOORBELL===]       |
|                                                                                                  |
| Plane 2: Main Coroutine Loop (C++20 Asynchronous Tasks)                                          |
| 00:00.120 [imu_pipeline]───────────────>[attitude_ekf]────────────>[flight_control]              |
|           ▲                             ▲                           ▲                            |
|           co_await spi_ring.pop()       co_await timer.sleep(1ms)   co_await imu_data            |
|                                                                                                  |
| >> [CLICKED EVENT: imu_pipeline (co_await spi_ring.pop())]                                       |
| >> [SOURCE CODE INSPECTOR: apps/gps_imu_app/src/main.cpp:42]                                     |
|    40:   while (running) {                                                                       |
|    41:       // Await next 8 kHz sample from I/O processor ring                                  |
| -> 42:       auto sample = co_await g_sensor_ring.pop_async();                                   |
|    43:       attitude_ekf.update(sample.gyro, sample.accel);                                     |
|    44:   }                                                                                       |
+===================================================================================================+
| [WINDOW 3: PER-PROCESSOR SPU/CPU & OS PROCESS UTILIZATION]                                       |
| Core 0 [Host Linux ARM64]:                                                                       |
|   - Overall CPU Usage: 8.4% (System: 2.1%, User: 6.3%, Idle: 91.6%)                              |
|   - AbstractX Process: 5.2% CPU (Thread: coro_main 3.8%, Thread: udp_sink 1.4%)                 |
|   - External Processes: Linux kernel 1.8%, sshd 0.4%, mosquitto 0.3%                             |
| Core 1 [XuanTie E906 RISC-V]:                                                                    |
|   - Active Duty Cycle: 14.8% (Active: 148 µs/ms, WFI Sleep: 852 µs/ms)                           |
|   - Execution Breakdown: TWI0 ISR: 4.2%, SPI DMA ISR: 6.1%, SPSC Drain: 4.5%                     |
| Fabric [Gowin FPGA 20K]:                                                                         |
|   - Logic LUT Utilization: 18.2% (3,640 / 20,000 LUTs)                                           |
|   - Auto-DMA Engine Bandwidth: 12.8 Mbps (Burst Rate: 25.0 MHz)                                  |
+===================================================================================================+
```

---

## 3. Core Observability Pillars

### 1. Platform Topology Auto-Discovery & Table Inspector
The Studio dynamically inspects the top-level **Platform Topology Descriptor Table** (`PlatformTopologyTable` defined in [`docs/ABSTRACTX_PLATFORM_TOPOLOGY_AND_METRICS_SPEC.md`](ABSTRACTX_PLATFORM_TOPOLOGY_AND_METRICS_SPEC.md)).
- Ingests `0x0001: PLATFORM_TOPOLOGY_ANNOUNCE` packet over UDP port 9870 or CTF Channel 1.
- Automatically determines if the target is:
  1. `Linux_Standard_SITL` (Standalone Linux / Host Simulation)
  2. `Linux_Host_E906` (Linux ARM64 + XuanTie E906 Co-Processor via Shared SRAM)
  3. `Linux_Host_E906_FPGA` (Linux ARM64 + XuanTie E906 + Tang Primer 20K FPGA)
  4. `Linux_Host_FPGA_Direct` (Linux ARM64/x86 + Direct PCIe/SPI FPGA switch fabric)
  5. `RP2350_DualCore_Pico2W` (Raspberry Pi Pico 2 W Dual-Core + CYW43 Wi-Fi)
  6. `ESP32P4_FreeRTOS` (Espressif ESP32-P4 Dual RISC-V + FreeRTOS)
- Renders interconnect saturation, ring capacity, and doorbell interrupt latency.

### 2. Dual-Plane Execution Timeline (I/O Processor vs Coroutines)
Separates the low-level hardware interrupt context from the high-level cooperative coroutine context:
- **Plane 1 (I/O Processor & Drivers)**: Shows PLIC ISRs, DMA transfers (SPI, UART, I2C), and hardware mailbox doorbells.
- **Plane 2 (Application Coroutines)**: Shows `Task<void>` timelines, suspension tokens (`co_await`), and dispatch queue latencies.

### 3. Source Code Scanner & Interactive Inspector
- Automatically scans project source files (`apps/`, `include/`, `src/`) or extracts DWARF line records from the application ELF binary using `pyelftools`.
- Clicking or hovering over any event in the timeline immediately opens the **Source Code Inspector** window, jumping directly to the file, line number, and highlighting the exact C++ line where the coroutine awaited or where the driver ISR triggered.

### 4. Per-Processor SPU/CPU & OS Process Utilization
Provides real-time utilization graphs for all silicon domains:
- **Linux**: Reports system CPU% (`/proc/stat`), AbstractX process CPU% (`/proc/self/stat`), per-thread usage, and external background processes (sshd, kernel threads).
- **XuanTie E906**: Tracks active RISC-V instruction cycles (`rdcycle`, `rdinstret`) versus WFI sleep cycles.
- **RP2350 (Pico 2 W)**: Tracks Core 0 I/O / Wi-Fi duty cycle versus Core 1 coroutine duty cycle and WFE sleep.
- **ESP32-P4 / ESP32**: Utilizes FreeRTOS runtime stats (`vTaskGetRunTimeStats`) and idle hook to graph per-task CPU utilization (AbstractX, Wi-Fi, TCP/IP, Bluetooth, IDLE).
- **FPGA Fabric**: Reports logic LUT utilization %, Block RAM usage, and DMA bus occupancy.

---

## 4. Python & Dear ImGui Bundle Implementation (`tools/visualizer/`)

The AbstractX Visualizer is implemented in Python using **`imgui-bundle`** (`Dear ImGui` + `ImPlot` + `ImGuizmo`):

```bash
# 1. Install dependencies
pip install -r tools/visualizer/requirements.txt

# 2. Launch live visualizer studio (listening on UDP port 9870)
python3 tools/visualizer/abstractx_studio.py --port 9870

# 3. Or launch standalone with synthetic multi-plane simulation
python3 tools/visualizer/abstractx_studio.py --sim

# 4. Or launch directly into Level 2 Flight & IMU domain tab
python3 apps/gps_imu_app/tools/flight_display.py --sim
```

---

## 5. Two-Level GUI Architecture

```
┌────────────────────────────────────────────────────────────────────────┐
│                        ABSTRACTX STUDIO (imgui-bundle)                 │
├────────────────────────────────────────────────────────────────────────┤
│ Level 1: Core AbstractX Framework Layer (Standard across all apps)     │
│   • 64-byte TLP Ingestion (UDP, Serial, Shared SRAM)                   │
│   • Platform Topology & Interconnect (Cores, SPU/CPU%, SPSC Rings)     │
│   • Dual-Plane Timeline (Hardware ISR/DMA vs C++20 Tasks)              │
│   • Source Inspector: Jump to __FILE__ : __LINE__ on co_await          │
│   • Memory Observability & Static Section Budgets (MemBrowse)          │
├────────────────────────────────────────────────────────────────────────┤
│ Level 2: User & Domain Extensible Layer (Pluggable App Modules)        │
│   • Pluggable tab interface (e.g. tools/visualizer/flight_plugin.py)   │
│   • For gps_imu_app:                                                   │
│       - Primary Flight Display (PFD) Artificial Horizon & Pitch Ladder │
│       - 3D Quadcopter Perspective Wireframe (Tait-Bryan rotation)      │
│       - Quad-X Motor Mixer Demand Bars (100..1000 µs)                  │
│       - Aviation Navigation Gauges & Stream Rates (IMU/Mag/GPS/AHRS)   │
│       - 8 kHz IMU Oscilloscope (ImPlot real-time waveforms)            │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 6. Continuous Memory Observability with MemBrowse

Because AbstractX strictly enforces **Freestanding C++20 with Zero Heap** (no `malloc`, no `operator new`, no dynamic STL), all runtime memory is statically pooled in `.bss` and `.data`. 

### MemBrowse Integration Workflow
1. **Target Definitions (`tools/membrowse-targets.json`)**:
   Specifies target ELF binary paths, linker maps, and strict RAM/Flash budgets for RP2350 (520 KB SRAM / 4 MB Flash), XuanTie E906 (64 KB SRAM A3/C), ESP32-P4, and Host SITL.
2. **Local Static Tracker (`tools/track_memory_membrowse.py`)**:
   Extracts section sizes (`.text`, `.rodata`, `.data`, `.bss`, `.stack`), verifies zero dynamic heap references in symbol tables, and exports `tools/visualizer/memory_metrics.json`.
3. **Live Studio Visualization (Tab 3)**:
   AbstractX Studio dynamically reads memory metrics to display real-time RAM/Flash saturation gauges and section tables.
4. **CI/CD Continuous Tracking**:
   The `membrowse-action` GitHub Action tracks memory footprint trends commit-over-commit and gates PRs against memory bloat.



