# AbstractX Software Execution Environments & Deployment Architecture

> **See also:** [`ABSTRACTX_DESIGN_GOALS.md`](ABSTRACTX_DESIGN_GOALS.md) — the authoritative design goals and implemented-vs-roadmap status.

## 1. Overview & Core Vision

AbstractX unifies real-time embedded I/O across **three foundational software execution environments**.

Crucially, **the top-level application software (written in C++20 Stackless Coroutines with `co_await`, `when_all`, and `when_any`) is portable across all three environments with the same API**. The underlying hardware/driver dispatch mechanism, frame pool allocator, and transport layer are platform-specific — but the application code above them does not change.


```
┌───────────────────────────────────────────────────────────────────────────────────┐
│              UNIFIED TOP LEVEL: SINGLE PROCESS, SINGLE COROUTINE THREAD           │
│                                                                                   │
│   auto [sensor_a, sensor_b] = co_await when_all(read_a(), read_b());             │
│   // Identical C++20 application code across all 3 environments!                  │
└───────────────────────────────────────────────────────────────────────────────────┘
                                          │
                        Lock-Free SPSC TLP Message Rings
                                          │
         ┌────────────────────────────────┼────────────────────────────────┐
         ▼                                ▼                                ▼
┌─────────────────┐              ┌─────────────────┐              ┌─────────────────┐
│  ENVIRONMENT 1  │              │  ENVIRONMENT 2  │              │  ENVIRONMENT 3  │
│  Linux / Host   │              │  Multi-Core MCU │              │  FPGA Hardware  │
│  Threaded I/O   │              │  Remote Proc /  │              │  Offloader &    │
│  Workers (SITL) │              │  Interrupt/DMA  │              │  On-Board Fabric│
└─────────────────┘              └─────────────────┘              └─────────────────┘
```

---

## 2. The 3 Execution Environments

### Environment 1: Linux / Host Multi-Threaded I/O Workers (SITL & Companion Computers)
* **Target Platforms**: Raspberry Pi 5, NVIDIA Jetson, x86 Linux SITL Simulation, ROS2 / Micro-ROS Nodes.
* **Architecture**:
  - **Top Level**: A single high-priority real-time thread running the C++20 Coroutine Scheduler (`CoroutineIoEngine`).
  - **I/O Subsystem**: Background POSIX worker threads (one thread per physical blocking device: `/dev/spidev`, `/dev/i2c-dev`, `/dev/ttyUSB`, or simulated physics/sensor models in SITL).
  - **Interconnect**: Lock-free SPSC rings (`SpscTlpRing`) with `eventfd` doorbells and zero-copy pointer descriptors (`TlpDescriptor`).
* **Why It Solves the Linux Dilemma**:
  - Linux kernel drivers for SPI and I2C use synchronous blocking `ioctl()` calls without `epoll` support.
  - Dedicated background worker threads absorb these blocking calls.
  - The main application loop never blocks, never suffers from mutex lock contention (`AP_HAL::Semaphore`), and never incurs thread context-switch jitter ($20\text{--}50\ \mu\text{s}$).

---

### Environment 2: Multi-Core MCU / Asymmetric Multiprocessors (Interrupt & DMA Driven)
* **Target Platforms**: Raspberry Pi RP2350 (Pico 2 / Pico 2W), Espressif ESP32-P4 (Dual RISC-V 400MHz), Allwinner A55 + RISC-V E907, STM32 H7 / G4 / AT32.
* **Architecture**:
  - **Top Level (Core 1 / Application Core)**: Single thread running the C++20 coroutine state machine.
  - **I/O Subsystem (Core 0 / Coprocessor Core)**: Dedicated I/O processor running interrupt-driven, DMA-driven, and PIO state-machine drivers with ISR callbacks.
  - **Interconnect**: Shared SRAM / PSRAM lock-free SPSC rings passing compact 24-byte (`TlpShort`) or variable (`TlpVar<N>`) messages, triggered by hardware SIO FIFO / mailbox IRQs.
* **Why It Solves the Microcontroller Dilemma**:
  - Completely isolates high-rate ISR interrupt thrashing and DMA buffer recycling to Core 0.
  - Core 1 executes pure sequential coroutine logic with zero interrupt latency overhead.

---

### Environment 3: FPGA Hardware Offloader & On-Board Fabric (Hardware Synthesized)
* **Target Platforms**: Gowin Tang Nano 9K / Primer 20K, QMTECH Xilinx Zynq-7020 PL.
* **Architecture**:
  - **Top Level (Host CPU)**: Single thread running C++20 coroutines communicating over Dual-SPI or PCIe/AXI.
  - **I/O Subsystem (FPGA Fabric)**: Pure synthesizable hardware state machines (`asp_imu_auto_dma.sv`, `asp_router.sv`, `asp_spi_frontend.sv`, Wishbone masters).
  - **Interconnect**: Fixed 64-byte 512-bit vector TLPs (`Tlp64`) streamed over 50 MHz Dual-SPI (5.12 $\mu\text{s}$ per transfer) or AXI DMA.
* **Why It Solves the Hard Real-Time Dilemma**:
  - Maximum performance: **0% CPU load for sensor burst acquisition**, cycle-accurate hardware timestamping ($< 20\text{ ns}$), and parallel hardware execution across all buses simultaneously.

---

## 3. Comparison Matrix Across Environments

| Feature | Environment 1 (Linux SITL / SBC) | Environment 2 (Dual-Core MCU / AMP) | Environment 3 (FPGA Fabric) |
|---|---|---|---|
| **Top-Level Threading** | Single Thread (C++20 Coroutines) | Single Thread (C++20 Coroutines) | Single Thread (C++20 Coroutines) |
| **I/O Execution Engine** | Background POSIX worker threads | Core 0 ISR / DMA / PIO engines | Synthesizable RTL state machines |
| **Message Container** | Zero-copy `TlpDescriptor` (48B) | Compact `TlpShort` (24B) / `TlpVar` | Fixed 512-bit `Tlp64` (64B) |
| **Transport Medium** | Shared RAM / `eventfd` | Shared SRAM / Hardware Mailbox | Dual-SPI / AXI-Stream / PCIe |
| **Primary Advantage** | Rapid PC testing & Linux AI nodes | Ultra-low power MCU sensor hub | Sub-20ns jitter & max hardware speed |
| **Application Code Portability**| **100% Identical C++20 Code** | **100% Identical C++20 Code** | **100% Identical C++20 Code** |

---

## 4. The Unified Developer Workflow

AbstractX allows developers to write their core application once and seamlessly transition across lifecycle stages:

1. **Step 1: Rapid Development & Simulation on PC (Environment 1)**:
   - Develop, test, and debug flight control, robotics kinematics, or industrial logic on Linux / macOS using the SITL simulator benchmark ([`sim/sitl_coro_sim.cpp`](../sim/sitl_coro_sim.cpp)).
2. **Step 2: Low-Cost Edge Deployment (Environment 2)**:
   - Flash the exact same C++20 coroutine application onto a dual-core microcontroller (RP2350 Pico 2W or ESP32-P4), leveraging Core 0 for interrupt/PIO offloading.
3. **Step 3: High-Performance Hardware Acceleration (Environment 3)**:
   - Deploy onto an FPGA-accelerated platform (Gowin / Zynq-7020) for sub-20ns hardware timestamping and microsecond Dual-SPI transfers.

---

## 5. Universal Code Example

```cpp
#include "asp_coro.hpp"
#include "asp_tlp_msg.hpp"

using namespace abstractx;
using namespace abstractx::coro;

// 1. High-Rate 8 kHz Telemetry Coroutine
Task<void> telemetry_loop(CoroutineIoEngine& io, SystemStats& stats) {
    while (true) {
        // Awaits autonomous DMA stream (Channel 2)
        Tlp64 packet = co_await io.async_await_stream(Channel::Telemetry);
        process_telemetry(packet.payload());
    }
}

// 2. Multi-Bus Synchronized Sensor Acquisition using when_all (&&)
Task<SensorFusionData> read_multi_sensor_sync(CoroutineIoEngine& io) {
    // Concurrently dispatches requests across separate physical buses:
    auto [adc_data, temp_data, press_data] = co_await when_all(
        io.async_read(REG_ADC_CURRENT),
        io.async_read(REG_I2C_TEMP),
        io.async_read(REG_SPI_PRESSURE)
    );

    co_return compute_fusion(adc_data, temp_data, press_data);
}

// 3. Safety Watchdog Timeout using when_any (||)
Task<void> monitored_actuator_command(CoroutineIoEngine& io, uint32_t command) {
    auto result = co_await when_any(
        io.async_write(REG_ACTUATOR_CTRL, command),
        hardware_timeout(500us)
    );

    if (result.index() == 1) {
        trigger_emergency_failsafe(); // Timed out before hardware ACK
    }
}
```

---

## 6. Zero-Allocation Execution & HALO (Heap Allocation eLision)

For safety-critical embedded systems (flight controllers, automotive ECUs, robotics), **dynamic heap allocation (`malloc` / `new`) is strictly forbidden** during runtime.

AbstractX achieves **deterministic zero-allocation** through two complementary mechanisms:

### 6.1 Compiler-Driven HALO (Heap Allocation eLision Optimization)
When a coroutine's lifetime is anchored inside the caller's execution scope (such as the main flight/control loop), modern C++20 compilers (GCC with `-O2/-O3`, Clang) completely elide dynamic allocation of the coroutine frame.
- **Embedded Result Storage**: Result structures (e.g. `IOResult out_data`) are embedded directly inside the `DeviceAwaiter` object on the coroutine frame stack.
- In `await_suspend()`, the awaiter passes `&out_data` directly to the background I/O worker or hardware DMA channel.
- When hardware completes, data is written directly to `*target_storage` and the coroutine handle is resumed with **zero data copies and zero heap allocations**.

### 6.2 Embedded Static Frame Pools
For embedded bare-metal microcontrollers where HALO may not be guaranteed across complex non-inlined function boundaries, AbstractX provides a **static frame pool allocator** in `promise_type`:

```cpp
// Static pre-allocated buffer for coroutine frames
alignas(64) static uint8_t g_coro_frame_pool[4096];
static std::atomic<size_t> g_frame_pool_offset{0};

struct Task {
    struct promise_type {
        // Allocates strictly from pre-allocated SRAM pool - 0 dynamic heap calls!
        void* operator new(std::size_t size) noexcept {
            size_t offset = g_frame_pool_offset.fetch_add((size + 63) & ~63);
            if (offset + size > sizeof(g_coro_frame_pool)) return nullptr;
            return &g_coro_frame_pool[offset];
        }
        void operator delete(void*, std::size_t) noexcept {}
    };
};
```

---

## 7. The 4-Thread Background I/O Worker Pool vs. Bare-Metal ISR/DMA

```
┌───────────────────────────────────────────────────────────────────────────────────┐
│                       MAIN CONTROL THREAD (REACTOR EVENT LOOP)                    │
│   - Drains completed events from StaticRingQueue (Wait-free, zero-allocation)     │
│   - Resumes coroutine handles: event.handle.resume()                              │
└────────────────────────────────────────┬──────────────────────────────────────────┘
                                         │
                 ┌───────────────────────┴───────────────────────┐
                 │                                               │
                 ▼ (On Linux / SITL)                             ▼ (On Bare-Metal MCU / FPGA)
┌─────────────────────────────────────────────────┐ ┌─────────────────────────────────────────────────┐
│  4-THREAD BACKGROUND I/O WORKER POOL            │ │  HARDWARE ISR & DMA ENGINES                     │
│  - Worker Thread 0: SPI Master (/dev/spidev)    │ │  - Direct Hardware DMA Bus Burst Controller     │
│  - Worker Thread 1: I2C Master (/dev/i2c-dev)   │ │  - Raw Hardware Interrupt Service Routine (ISR) │
│  - Worker Thread 2: UART Serial (/dev/ttyUSB)   │ │  - FPGA Autonomous Auto-DMA Engine              │
│  - Worker Thread 3: PCIe TLP Transaction Engine │ │  - Zero OS threads: ISR pushes handle to queue  │
└─────────────────────────────────────────────────┘ └─────────────────────────────────────────────────┘
```

1. **On Linux (Environment 1)**: The 4-thread I/O worker pool absorbs the blocking `ioctl()` calls for `/dev/spidev0.0`, `/dev/i2c-1`, and serial ports.
2. **On Bare-Metal Microcontrollers (Environment 2)**: The thread pool is removed. `await_suspend()` triggers a hardware DMA transaction, and a raw hardware ISR pushes the handle to the event queue upon DMA completion.
3. **On FPGA Systems (Environment 3)**: The FPGA handles the transactions autonomously and notifies the host over the `o_int_req` doorbell interrupt line.

---

## 8. Hardware Fault Tolerance & Timeout Handling

AbstractX provides native status reporting in every I/O transaction to handle disconnected sensors, bus NACKs, and hardware stalls without crashing the main reactor loop:

```cpp
enum class IOStatus : uint8_t {
    Success    = 0,
    Timeout    = 1,
    BusError   = 2,
    DeviceNack = 3
};

struct IOResult {
    DeviceId   device;
    IOStatus   status;
    float      payload_value;
    uint64_t   timestamp_ns;
};
```

### Clean Coroutine Error Recovery:
```cpp
DeviceAwaiter baro_call{DeviceId::BARO, 2000us};
IOResult baro_res = co_await baro_call;

if (baro_res.status == IOStatus::Success) {
    update_altitude(baro_res.payload_value);
} else if (baro_res.status == IOStatus::DeviceNack || baro_res.status == IOStatus::Timeout) {
    log_sensor_warning("Barometer unreachable; falling back to GPS/INS altitude");
}
```

---

## 9. Dedicated Lock-Free SPSC Channel Array Architecture (Zero Mutexes, Zero Locks)

To completely eliminate mutex lock contention on bare-metal targets (Pico 2W, ESP32-P4) and ultra-low-latency Linux nodes, AbstractX deploys an **array of dedicated lock-free SPSC ring buffers** (`SpscChannelArray<T, NumChannels>`):

```
BACKGROUND I/O WORKER THREADS       DEDICATED SPSC RING ARRAY          MAIN FLIGHT LOOP THREAD
─────────────────────────────       ─────────────────────────          ───────────────────────
[ Worker 0: PCIe TLP Thread ] ────► [ Queue [0] : PCIe SPSC ] ──pop()──┐
[ Worker 1: SPI IMU Thread  ] ────► [ Queue [1] : IMU SPSC  ] ──pop()──┼─► [ Main Core Loop ]
[ Worker 2: I2C Baro Thread ] ────► [ Queue [2] : Baro SPSC ] ──pop()──┤   (Non-blocking round-robin)
[ Worker 3: ADC / Mag Thread] ────► [ Queue [3] : ADC SPSC  ] ──pop()──┘   (Safely calls .resume())
```

### Why SPSC Arrays Eliminate All Synchronization Bottlenecks:
1. **1-to-1 Producer-Consumer Pair**: Each channel has exactly **one dedicated producer** (Worker thread index $N$, or raw hardware DMA channel $N$) and **one single consumer** (the Main Flight Loop).
2. **Lock-Free Atomic Fences**: Uses `std::memory_order_release` on push and `std::memory_order_acquire` on pop without a single OS mutex or syscall.
3. **Zero False Sharing**: Ring head and tail atomic counters are cache-line aligned (`alignas(64)`), guaranteeing zero cache line bouncing across CPU cores.
4. **Resumption Safety**: Background workers NEVER execute `.resume()` directly (which would cause severe thread-hopping race conditions and corrupt flight matrix math). Workers push `{handle, data}` across the lock-free boundary, and the main loop calls `.resume()` strictly on the single main thread.

---

## 10. Bare-Metal & Freestanding Microcontroller Support (No OS, No Mutex)

Microcontrollers do **not** have `<mutex>`, `<thread>`, `<condition_variable>`, or an operating system. The AbstractX core headers ([`include/`](../include/)) are **100% Freestanding C++20**, requiring only standard atomic and coroutine compiler support.

```
┌───────────────────────────────────────────────────────────────────────────────────┐
│                    MICROCONTROLLER HARDWARE TOPOLOGY MATRIX                       │
└───────────────────────────────────────────────────────────────────────────────────┘
                                          │
                  ┌───────────────────────┴───────────────────────┐
                  ▼                                               ▼
┌───────────────────────────────────────────────┐ ┌───────────────────────────────────────────────┐
│ CONFIGURATION A: SINGLE-CORE MCU (STM32G4/S3) │ │ CONFIGURATION B: DUAL-CORE AMP (RP2350/P4)    │
│                                               │ │                                               │
│ [ Hardware DMA / Peripheral ISR (Producer) ]  │ │ [ Core 0: Dedicated I/O Coprocessor Engine ]  │
│                       │                       │ │                       │                       │
│             g_spsc_channel.push()             │ │         g_spsc_channel_sram.push()            │
│                       ▼                       │ │                       ▼                       │
│ [ Single Lock-Free SPSC Ring Buffer (RAM) ]   │ │ [ Shared SRAM Lock-Free SPSC Array ]          │
│                       │                       │ │                       │                       │
│             g_spsc_channel.pop()              │ │         g_spsc_channel_sram.pop()             │
│                       ▼                       │ │                       ▼                       │
│ [ Main Loop C++20 Coroutine Task (Consumer) ] │ │ [ Core 1: Main C++20 Flight Loop Task ]       │
└───────────────────────────────────────────────┘ └───────────────────────────────────────────────┘
```

### 10.1 Is SPSC Safe on a Single-Core Microcontroller with ISRs?
**YES, 100% SAFE and Re-Entrant.**
- **Producer**: The hardware Interrupt Service Routine (e.g. `SPI_DMA_IRQHandler()`).
- **Consumer**: The Main Loop event drainer.
- Because SPSC strictly enforces a single producer and single consumer:
  - The ISR *only* writes to `tail_` and reads `head_`.
  - The Main Loop *only* writes to `head_` and reads `tail_`.
  - **No critical sections or `__disable_irq()` calls are needed!** An ISR interrupting the main loop will never corrupt the queue state.

### 10.2 Dual-Core Microcontrollers (RP2350 Pico 2/2W & ESP32-P4)
- **Core 0 (I/O Engine)**: Handles PIO DShot generation, fast UART telemetry, and SPI DMA interrupts.
- **Core 1 (Application Engine)**: Runs the C++20 Coroutine flight loop.
- **Inter-Core SPSC in Shared SRAM**: The atomic `memory_order_release` and `memory_order_acquire` operations emit hardware Data Memory Barriers (`DMB` on ARM Cortex-M33; `FENCE` on RISC-V), guaranteeing atomic handoffs without hardware spinlocks or OS mailboxes.

---

## 11. Hybrid Linux Host + FPGA Co-Processing Architecture

On advanced robotic systems, companion computers (Raspberry Pi 5, NVIDIA Jetson), or hybrid SoCs (Zynq-7020 / Allwinner A55+FPGA), AbstractX seamlessly integrates **Linux native I/O thread pools** and **high-speed SPI-to-FPGA offloading** simultaneously:

```
┌───────────────────────────────────────────────────────────────────────────────────┐
│               HYBRID LINUX HOST REAL-TIME FLIGHT / ROBOTICS CORE                  │
│                                                                                   │
│  auto [fpga_imu, linux_baro, linux_gps] = co_await when_all(                      │
│      fpga_bridge.async_await_stream(Channel::Telemetry),                          │
│      linux_i2c.async_read(REG_BARO_PRESSURE),                                     │
│      linux_uart.async_read(REG_GPS_UBX)                                           │
│  );                                                                               │
└────────────────────────────────────────┬──────────────────────────────────────────┘
                                         │
             Lock-Free SPSC Channel Array (`SpscChannelArray<T, 4>`)
                                         │
       ┌─────────────────────────────────┼─────────────────────────────────┐
       ▼                                 ▼                                 ▼
┌───────────────────────────────┐ ┌───────────────────────────────┐ ┌───────────────────────────────┐
│  Worker 0: FPGA SPI Bridge    │ │  Worker 1: Linux Native I2C   │ │  Worker 2: Linux Native UART  │
│  /dev/spidev0.0 (50 MHz)      │ │  /dev/i2c-1 (400 kHz)         │ │  /dev/ttyUSB0 (2 Mbaud)       │
│  - 8 kHz IMU Auto-DMA Stream  │ │  - Barometer / Mag Sensor     │ │  - GPS / Companion Telemetry  │
│  - DShot Motor Timers         │ │  - Power Monitoring IC        │ │  - MAVLink / ROS2 Micro-XRCE  │
│  - Sub-20ns Latched TS        │ │  - EEPROM Calibration         │ │  - Blackbox Flash Tunnel      │
└──────────────┬────────────────┘ └───────────────────────────────┘ └───────────────────────────────┘
               │ Dual-SPI 50MHz
               ▼
┌───────────────────────────────────────────────────────────────────────────────────┐
│                     ABSTRACTX FPGA HARDWARE COPROCESSOR                           │
│        (Gowin Tang Nano 9K / Tang Primer 20K / Xilinx Zynq-7020 PL)               │
│                                                                                   │
│  - Autonomous SPI Master Engine (asp_imu_auto_dma.sv)                             │
│  - 512-bit Vector Switch Fabric & Skid Buffers (asp_router.sv)                    │
│  - Hardware DShot600 / DShot300 Motor Engine & WS2812B RGB Driver                 │
│  - Cycle-Accurate 64-bit Hardware Timestamp Latching Counter                      │
└───────────────────────────────────────────────────────────────────────────────────┘
```

### Key Architectural Advantages of the Hybrid Topology:
1. **Zero Compromise Hardware Allocation**:
   - Time-critical, ultra-low-jitter functions (8 kHz IMU, DShot motor generation, sub-20ns timestamping) run in dedicated **FPGA fabric**.
   - Standard peripherals already wired to Linux headers (I2C barometer, USB GPS, serial radio) run through the **Linux Thread Pool**.
2. **Unified C++20 Coroutine API**:
   - The main flight/robotics task does not distinguish between an FPGA Dual-SPI transaction and a native Linux `/dev/i2c` read.
   - Both produce standard `Tlp64` / `TlpDescriptor` packets delivered through the lock-free SPSC array.
3. **True Bus Concurrency**:
   - The FPGA Dual-SPI link (50 MHz, 5.12 $\mu\text{s}$ per 64B packet), Linux I2C bus (400 kHz), and UART serial link (2 Mbaud) all run **simultaneously in parallel**, joined seamlessly by `co_await when_all(...)`!
