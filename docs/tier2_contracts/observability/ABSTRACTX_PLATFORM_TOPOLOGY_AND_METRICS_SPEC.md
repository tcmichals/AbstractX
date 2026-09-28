# AbstractX Platform Topology & Observability Studio Specification

**Document ID**: SPEC-ARCH-TOPOLOGY-01  
**Status**: Authoritative Design Specification  
**Author**: Tim Michals  
**Date**: September 2026  
**License**: GPL-3.0-or-later  

---

## 1. Executive Summary & Problem Statement

Embedded, robotic, and avionics flight stacks must deploy across wildly heterogeneous silicon architectures:
1. **Pure Host Linux / SITL Simulation**: Single-core or SMP host threads with POSIX background workers.
2. **Heterogeneous Linux Host + XuanTie E907 Co-Processor**: Allwinner A5E / A7A (ARM64 host running Linux + 32-bit RISC-V E907 real-time coprocessor connected via Shared SRAM and `sun6i-msgbox` doorbells).
3. **Linux Host + XuanTie E907 + Tang Primer 20K FPGA**: High-speed PCIe or SPI switch fabric connecting host and coprocessor to FPGA-synthesized hardware accelerators (IMU Auto-DMA, DShot 4-CH, NeoPixel, PWM).
4. **Linux Host + Direct FPGA Switch Fabric**: Host connects directly over PCIe Gen2/Gen3 or SPI DMA to the FPGA switch fabric with zero coprocessor MCU.
5. **Dual-Core Asymmetric Microcontroller**: Raspberry Pi Pico 2 W (Dual RP2350 RISC-V/Cortex-M33 cores with Core 0 running the I/O processor + CYW43439 Wi-Fi and Core 1 running the cooperative C++20 coroutine loop).
6. **Dual-Core Microcontroller with RTOS**: Espressif ESP32-P4 / ESP32 running FreeRTOS tasks (Wi-Fi/BT, networking) alongside the AbstractX coroutine engine.

### The Problem
Previously, observability tools (such as Percepio Tracealyzer or generic serial loggers) were either:
- Hardcoded for a single monolithic OS (FreeRTOS or Linux),
- Blind to co-processors (e.g., unaware that an E907 or FPGA is offloading I/O while Linux sleeps),
- Incapable of displaying the separation between hardware I/O driver planes and cooperative coroutine execution planes,
- Unable to map runtime events back to the original C++ source code lines without manual debugging,
- Unable to report true per-processor/per-core CPU/SPU utilization across disparate RTOS, bare-metal, and Linux environments.

### The Solution: AbstractX Topology Table & Visualizer Architecture
This specification formalizes:
1. **The AbstractX Platform Topology Descriptor Table (`PlatformTopologyTable`)**: A standardized compile-time and runtime table defining silicon cores, transport fabrics, and hardware accelerators.
2. **Dynamic Platform Discovery & CTF 1.8 Identity Announcement**: The live system announces its topology to the visualizer studio upon connection.
3. **The Multi-Window Visualizer Studio Architecture**:
   - **Window 1: Platform Topology & Silicon Interconnect Graph**
   - **Window 2: Dual-Plane Timeline (I/O Processor & Drivers vs Coroutine Main Loop) with Integrated Source Code Scanner**
   - **Window 3: Per-Processor SPU/CPU & OS Process Utilization Inspector**

---

## 2. Platform Architecture Matrix & Linux Configurations

In Linux deployments, the application automatically determines or receives its silicon topology configuration. The four canonical Linux deployment models are:

```
┌──────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                CANONICAL LINUX TOPOLOGY CONFIGURATIONS                           │
├──────────────────────────┬───────────────────────┬────────────────────────┬──────────────────────┤
│ Configuration            │ Host Domain           │ Coprocessor Domain     │ Hardware Accelerators│
├──────────────────────────┼───────────────────────┼────────────────────────┼──────────────────────┤
│ Linux_Standard_SITL      │ Linux ARM64/x86 (SMP) │ None (POSIX Threads)   │ Software Mock Drivers│
│ Linux_Host_E907          │ Linux ARM64 (Cubie)   │ XuanTie E907 (RISC-V)  │ Shared SRAM + MsgBox │
│ Linux_Host_E907_FPGA     │ Linux ARM64 (Cubie)   │ XuanTie E907 (RISC-V)  │ FPGA (DShot, NeoPix) │
│ Linux_Host_FPGA_Direct   │ Linux ARM64/x86 (PC)  │ None                   │ PCIe/SPI FPGA Fabric │
└──────────────────────────┴───────────────────────┴────────────────────────┴──────────────────────┘
```

### Architectural Block Diagrams

```mermaid
graph TD
    subgraph Config1["Configuration 1: Standard Linux (SITL / Standalone SBC)"]
        L1[Linux Application] --> P1[POSIX Worker Threads]
        P1 --> D1[Linux /dev/i2c, /dev/spidev, Sockets]
    end

    subgraph Config2["Configuration 2: Linux Host + XuanTie E907 Coprocessor"]
        L2[Linux ARM64 Host] <-->|Shared SRAM 0x40000000<br/>sun6i-msgbox doorbells| E2[XuanTie E907 RISC-V]
        E2 --> H2[Physical Hardware: TWI0, SPI0 DMA, UART]
    end

    subgraph Config3["Configuration 3: Linux Host + XuanTie E907 + FPGA Fabric"]
        L3[Linux ARM64 Host] <-->|Shared SRAM + Mailbox| E3[XuanTie E907 RISC-V]
        E3 <-->|Wishbone / SPI DMA| F3[Tang Primer 20K FPGA]
        F3 --> A3[IMU Auto-DMA, DShot 4-CH, NeoPixel Cores]
    end

    subgraph Config4["Configuration 4: Linux Host + Direct FPGA Switch Fabric"]
        L4[Linux Host ARM64/x86] <-->|PCIe Gen2 x1 / AXI| F4[FPGA Fabric]
        F4 --> A4[Hardware SPSC Rings & Accelerators]
    end
```

---

## 3. AbstractX Platform Topology Table C++ API (`platform_topology.hpp`)

The platform topology is codified in a zero-allocation, lightweight C++ structure that lives in `include/abstractx/platform_topology.hpp`.

```cpp
#pragma once
#include <cstdint>
#include <string_view>
#include <etl/array.h>

namespace abstractx::topology {

// 1. Top-Level Platform Silicon Architecture
enum class PlatformArch : uint8_t {
    Linux_Standard_SITL    = 0x01, // Pure host Linux with POSIX I/O workers
    Linux_Host_E907        = 0x02, // Linux host + XuanTie E907 coprocessor (Shared SRAM)
    Linux_Host_E907_FPGA   = 0x03, // Linux host + XuanTie E907 + FPGA fabric
    Linux_Host_FPGA_Direct = 0x04, // Linux host + Direct PCIe/SPI FPGA switch fabric
    RP2350_DualCore_Pico2W = 0x05, // Raspberry Pi Pico 2 W (RP2350 Dual-Core + CYW43)
    ESP32P4_FreeRTOS       = 0x06, // Espressif ESP32-P4 (Dual RISC-V 400MHz + FreeRTOS)
    Zynq_PS_PL             = 0x07  // Xilinx Zynq-7020 PS (ARM) + PL (FPGA)
};

// 2. Processing Unit / Core Type
enum class CoreRole : uint8_t {
    Host_Linux_SMP         = 0x01, // High-level Linux flight supervisor / network
    Coprocessor_IO_Worker  = 0x02, // XuanTie E907 or RP2350 Core 0 dedicated to I/O & DMA
    Coroutine_Main_Runner  = 0x03, // Core executing the cooperative C++20 coroutine loop
    Hardware_FPGA_Engine   = 0x04, // Synthesized FPGA hardware state machine
    RTOS_Background_Task   = 0x05  // FreeRTOS task (Wi-Fi, Bluetooth, TCP/IP stack)
};

// 3. Transport Interconnect Type
enum class InterconnectType : uint8_t {
    None                   = 0x00,
    Shared_SRAM_MsgBox     = 0x01, // Allwinner Shared SRAM A3/C + sun6i-msgbox
    RP2350_SIO_HardwareFifo= 0x02, // RP2350 Core 0 <-> Core 1 SIO FIFO
    PCIe_Gen2_TLP          = 0x03, // PCIe Switch Fabric (64-byte TLP)
    HighSpeed_SPI_DMA      = 0x04, // SPI DMA bus with IRQ handshake
    Loopback_POSIX_IPC     = 0x05, // Linux eventfd + shm / domain socket
    UDP_Network_Stream     = 0x06  // UDP Port 9870 wireless/Ethernet stream
};

// 4. Synthesized or On-Chip Hardware Accelerators Mask
enum HardwareAccelMask : uint32_t {
    ACCEL_NONE             = 0,
    ACCEL_IMU_AUTO_DMA     = (1 << 0), // FPGA IMU Auto-DMA IP core
    ACCEL_DSHOT_4CH        = (1 << 1), // FPGA 4-Channel DShot300/600 IP core
    ACCEL_NEOPIXEL         = (1 << 2), // FPGA NeoPixel status LED core
    ACCEL_SUN6I_MSGBOX     = (1 << 3), // Allwinner hardware mailbox doorbells
    ACCEL_CYW43_WIFI_PIO   = (1 << 4), // RP2350 PIO CYW43 Wi-Fi offloader
    ACCEL_FREERTOS_HOOKS   = (1 << 5)  // FreeRTOS runtime stats instrumentation
};

// 5. Individual Core Descriptor
struct CoreDescriptor {
    uint8_t     core_id{0};
    CoreRole    role{CoreRole::Coroutine_Main_Runner};
    const char* name{"MainCore"};
    uint32_t    nominal_clock_mhz{0};
};

// 6. Complete Master Platform Topology Table
struct PlatformTopologyTable {
    PlatformArch       arch{PlatformArch::Linux_Standard_SITL};
    const char*        platform_name{"AbstractX Generic"};
    const char*        board_model{"Unknown Board"};
    InterconnectType   primary_transport{InterconnectType::UDP_Network_Stream};
    uint32_t           accel_mask{ACCEL_NONE};
    
    // Core allocation (max 4 cores in static embedded table)
    uint8_t            core_count{1};
    etl::array<CoreDescriptor, 4> cores{};

    // Inter-domain memory configuration
    uintptr_t          shared_sram_base{0};
    uint32_t           shared_sram_size{0};
    uint16_t           spsc_ring_capacity{64};
};

} // namespace abstractx::topology
```

---

## 4. Binary CTF 1.8 Platform Topology Announcement Packet

When the AbstractX runtime initializes (`abstractx::init()`), it transmits a **Platform Topology Announcement Packet** over CTF Channel `0x01` (Control & System Identity) or UDP port 9870:

### Packet Definition (Event ID `0x0001: PLATFORM_TOPOLOGY_ANNOUNCE`)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      CTF Magic: 0xC1FC1FC1                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Stream ID (1) |  Event ID (1) | PlatformArch  | Primary Trans |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Hardware Accel Mask                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Core Count   | Reserved (0)  |        SPSC Ring Capacity     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|           Platform Name String (16 Bytes UTF-8 ASCII)         |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|           Board Model String (16 Bytes UTF-8 ASCII)           |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

When the Visualizer Studio receives this packet:
1. It immediately sets the **Platform Header Banner** (e.g. `[PLATFORM: Allwinner A5E + XuanTie E907 + Tang Primer 20K FPGA]`).
2. It allocates the corresponding execution timelines for each detected processing unit.
3. It displays the active interconnect transport gauges (Shared SRAM fill rate, MSGBox doorbell latency, or SIO FIFO depth).

---

## 5. Visualizer Studio Multi-Window Architecture (`tools/visualizer/`)

The Visualizer Studio is structured into three dedicated, synchronized windows:

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

## 6. Detailed Window Specifications

### Window 1: Platform Topology & Interconnect Inspector
1. **Auto-Configuration Display**:
   - Parses the incoming `PLATFORM_TOPOLOGY_ANNOUNCE` packet.
   - Renders the exact hardware topology: whether it is standalone Linux, Linux + E907, Linux + E907 + FPGA, RP2350 Pico 2 W, or ESP32-P4.
2. **Interconnect Gauges**:
   - Live visual saturation bar of inter-core SPSC ring buffers.
   - Round-trip doorbell interrupt latency (measured in microseconds).
   - Packet drop and overflow counter (verifying 0.00% packet loss).

---

### Window 2: Dual-Plane Timeline & Source Code Scanner
1. **Dual-Plane Separation**:
   - **Plane 1 (I/O Processor & Drivers)**: Shows raw hardware interrupts (ISRs), DMA completions, and peripheral drivers (`hal_spi`, `hal_i2c`, `hal_uart`, MSGBox).
   - **Plane 2 (Cooperative Coroutines)**: Shows application-level C++20 coroutine state machines (`imu_pipeline`, `attitude_ekf`, `flight_control`, `telemetry_tx`).
2. **Interactive Source Code Scanner**:
   - **Source Mapping Engine**: The visualizer incorporates a built-in Python source code scanner.
   - **Debug Metadata**: Traces carry source file IDs or relative paths (`apps/gps_imu_app/src/main.cpp`) and line numbers (`L42`), or match against the application ELF binary using DWARF debug lines via `pyelftools` / `addr2line`.
   - **Integrated Code Viewer**: Clicking any coroutine or driver event in the timeline immediately opens the source code pane, jumps to the exact source file, and highlights the specific line of code that triggered the suspension or ISR!

---

### Window 3: Per-Processor SPU/CPU & Process Utilization

Measuring CPU and SPU (Silicon Processing Unit) utilization across disparate architectures without perturbing real-time flight control:

#### 1. Linux Host Environment (ARM64 / x86)
- **Source**: Sampled via POSIX `/proc/stat` and `/proc/self/stat`.
- **Metrics Collected**:
  - **System-wide CPU%**: Total user, system, I/O wait, and idle time.
  - **AbstractX Process CPU%**: Precise CPU ticks consumed by the `abstractx` process.
  - **Per-Thread Breakdown**: Individual CPU consumption of the coroutine thread vs background I/O worker threads.
  - **Other Processes**: Reports background OS interference (e.g., Linux kernel threads, system daemons).

#### 2. XuanTie E907 Co-Processor Environment (RISC-V 32-bit)
- **Source**: Hardware RISC-V Machine Performance Counters (`rdcycle`, `rdinstret`).
- **Mechanism**:
  - During idle periods, the E907 executes the `__asm__ volatile("wfi")` (Wait For Interrupt) instruction.
  - A timer hook measures cumulative cycles spent in active ISR/drain processing versus cycles spent asleep in `wfi`.
  - Transmitted in periodic CTF heartbeat packets (Event ID `0x0003: CPU_USAGE_REPORT`).

#### 3. Raspberry Pi Pico 2 W (RP2350 Dual-Core)
- **Source**: Hardware SysTick / RISC-V `mtime` cycle counters.
- **Mechanism**:
  - **Core 0 (I/O)**: Tracks time executing peripheral ISRs and CYW43 Wi-Fi polling vs `wfe` sleep cycles.
  - **Core 1 (Coroutines)**: Tracks time executing coroutine dispatch loops vs sleeping waiting for SIO FIFO IRQ.

#### 4. Espressif ESP32-P4 / ESP32 (FreeRTOS)
- **Source**: FreeRTOS trace hooks and runtime statistics.
- **Mechanism**:
  - Uses `vTaskGetRunTimeStats()` and the FreeRTOS idle task hook (`vApplicationIdleHook`).
  - Measures runtime percentages for:
    - AbstractX Coroutine Worker Task
    - FreeRTOS IDLE Tasks (Core 0 & Core 1)
    - ESP-IDF Wi-Fi Driver Task (`wifi`)
    - TCP/IP Stack (`sys_evt`)
    - Bluetooth Controller (`btController`)

#### 5. FPGA Hardware Fabric (Tang Primer 20K / Gowin)
- **Source**: Hardware synthesis register reports and hardware cycle counters.
- **Metrics**:
  - Logic LUT and Block RAM utilization %.
  - DMA bus bandwidth and burst duty cycle %.

---

## 7. Configuration Table Implementation Examples

### Example A: Linux Host + XuanTie E907 + FPGA Fabric (`cubie_a5e_top.cpp`)

```cpp
#include "abstractx/abstractx.hpp"
#include "abstractx/platform_topology.hpp"

using namespace abstractx::topology;

const PlatformTopologyTable g_cubie_topology = {
    .arch = PlatformArch::Linux_Host_E907_FPGA,
    .platform_name = "Allwinner A5E Heterogeneous",
    .board_model = "Radxa Cubie A5E",
    .primary_transport = InterconnectType::Shared_SRAM_MsgBox,
    .accel_mask = ACCEL_IMU_AUTO_DMA | ACCEL_DSHOT_4CH | ACCEL_SUN6I_MSGBOX,
    .core_count = 3,
    .cores = {{
        {0, CoreRole::Host_Linux_SMP, "Linux ARM64 Host", 1400},
        {1, CoreRole::Coprocessor_IO_Worker, "XuanTie E907 RISC-V", 600},
        {2, CoreRole::Hardware_FPGA_Engine, "Tang Primer 20K FPGA", 50}
    }},
    .shared_sram_base = 0x40000000,
    .shared_sram_size = 64 * 1024,
    .spsc_ring_capacity = 64
};

int main() {
    abstractx::Config config{
        .trace = {
            .sink_type = abstractx::TraceSinkType::Udp,
            .sink_target = "192.168.1.100",
            .port = 9870
        },
        .topology = g_cubie_topology
    };

    abstractx::init(config);
    abstractx::run(application_main());
    return 0;
}
```

### Example B: Raspberry Pi Pico 2 W Dual-Core (`pico2w_main.cpp`)

```cpp
#include "abstractx/abstractx.hpp"
#include "abstractx/platform_topology.hpp"

using namespace abstractx::topology;

const PlatformTopologyTable g_pico_topology = {
    .arch = PlatformArch::RP2350_DualCore_Pico2W,
    .platform_name = "Raspberry Pi Pico 2 W",
    .board_model = "RP2350 Dual-Core",
    .primary_transport = InterconnectType::RP2350_SIO_HardwareFifo,
    .accel_mask = ACCEL_CYW43_WIFI_PIO,
    .core_count = 2,
    .cores = {{
        {0, CoreRole::Coprocessor_IO_Worker, "Core 0 (I/O + Wi-Fi)", 150},
        {1, CoreRole::Coroutine_Main_Runner, "Core 1 (Coroutines)", 150}
    }},
    .spsc_ring_capacity = 32
};
```

---

## 8. Summary of Engineering Guarantees

1. **Topology Transparency**: Any client connecting to AbstractX immediately receives full visibility into whether the target is standard Linux, Linux+E907, Linux+E907+FPGA, or a dual-core MCU.
2. **Dual-Plane Clarity**: Developers can clearly see both the low-level hardware I/O driver context and the high-level cooperative coroutine context.
3. **Instant Source Code Mapping**: Clicking a timeline block directly displays the source code line responsible for the event.
4. **Comprehensive CPU/SPU Profiling**: True per-processor utilization and external OS process tracking across bare-metal, RTOS, and Linux.
