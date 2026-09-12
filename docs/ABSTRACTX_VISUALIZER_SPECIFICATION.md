# AbstractX Visualizer Specification: The Next-Gen Trace & Coroutine Studio

The **AbstractX Visualizer** is a modern, real-time observability and timeline studio designed to surpass **Percepio Tracealyzer** and **Linux LTTng / Trace Compass** by combining **C++20 Coroutine-First Execution Modeling** with **High-Rate Flight Telemetry & TLP Packet Inspection**.

---

## 1. Why AbstractX Visualizer Surpasses Existing Tools

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                 KEY ARCHITECTURAL ADVANTAGES                            │
├──────────────────────────┬─────────────────────────────┬────────────────────────────────┤
│ Feature                  │ Percepio Tracealyzer / LTTng│ AbstractX Visualizer Studio    │
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
| ABSTRACTX STUDIO | Platform: Linux + XuanTie E907 + FPGA | Target: Radxa Cubie A5E | Stream: 8.2 kHz |
+===================================================================================================+
| [WINDOW 1: PLATFORM TOPOLOGY & SILICON FABRIC]                                                   |
| Architecture: Linux_Host_E907_FPGA | Transport: Shared SRAM A3/C (0x40000000) + sun6i-msgbox     |
| Silicon Cores:                                                                                   |
|  - Core 0 [Host ARM64] : Linux Flight Supervisor (PID, EKF, Telemetry, UDP :9870)                |
|  - Core 1 [RISC-V E907]: Real-Time I/O Reactor (TWI0 I2C, SPI0 DMA, Mailbox Doorbell)            |
|  - Fabric [Gowin FPGA] : IMU Auto-DMA IP, 4-CH DShot300/600 Core, NeoPixel IP                    |
| Interconnect Saturation:                                                                         |
|  - E907 -> Linux SPSC Ring: [████████░░░░░░░░░░░░] 22 / 64 pkts (Avg Doorbell Delay: 1.1 µs)    |
|  - FPGA -> E907 SPI DMA   : [████░░░░░░░░░░░░░░░░] 12 / 64 pkts (Transfer Latency: 0.8 µs)       |
+===================================================================================================+
| [WINDOW 2: DUAL-PLANE EXECUTION TIMELINE & SOURCE CODE SCANNER]                                  |
| Plane 1: I/O Processor & Drivers (E907 PLIC / ISRs & DMA)                                        |
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
| Core 1 [XuanTie E907 RISC-V]:                                                                    |
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
  2. `Linux_Host_E907` (Linux ARM64 + XuanTie E907 Co-Processor via Shared SRAM)
  3. `Linux_Host_E907_FPGA` (Linux ARM64 + XuanTie E907 + Tang Primer 20K FPGA)
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
- **XuanTie E907**: Tracks active RISC-V instruction cycles (`rdcycle`, `rdinstret`) versus WFI sleep cycles.
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
```


