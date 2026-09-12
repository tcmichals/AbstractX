# AbstractX

**Simple, High-Speed Asynchronous Embedded I/O Using C++20 Coroutines**

AbstractX applies the linear `async/await` pattern (familiar from Python `asyncio`, Boost.Asio, and C# `async`) to real-time embedded systems, microcontrollers, and Linux hosts.

Instead of writing fragmented callback state machines or spawning dozens of preemptive RTOS threads, you write asynchronous hardware routines in a clean, sequential flow using standard C++20 coroutines.

---

## 1. The Core Idea: Linear Async Code

In traditional embedded systems, non-blocking I/O forces developers to write manual state machines across timers or callbacks:

```
Traditional State Machine Approach:
  handle_tick() -> check_state() -> start_dma() -> return
  on_dma_isr()  -> set_state_flag() -> reschedule()
  handle_tick() -> read_buffer() -> parse() ... (split across files)
```

With AbstractX, the code is written linearly. The coroutine suspends at `co_await` until the hardware event completes, then resumes automatically:

```cpp
#include "abstractx/abstractx.hpp"

using namespace abstractx;

Task<void> imu_loop(Icm42688p& imu) {
    while (true) {
        // Suspend until next 8 kHz SPI DMA sample arrives
        ImuSample sample = co_await imu.next_sample_async();

        // Process data linearly without callbacks or state variables
        update_attitude(sample.gyro, sample.accel);
    }
}

Task<void> gps_loop(UbloxGps& gps) {
    while (true) {
        // Suspend until a complete UBX-NAV-PVT packet is received
        GpsFix fix = co_await gps.read_packet_async();

        update_navigation(fix.lat, fix.lon, fix.alt_mm);
    }
}
```

---

## 2. Key Architecture Principles

### 1. Single Prioritized Event Loop (No Task Proliferation)
AbstractX avoids spawning numerous RTOS tasks with duplicate stacks and preemption jitter. Instead:
- A single prioritized dispatcher processes coroutines cooperatively on the main thread.
- **Priority-Driven**: High-frequency real-time loops (e.g. 8 kHz IMU sampling) are dispatched first upon hardware completion. Lower-priority navigation and telemetry tasks run when real-time loops yield.

### 2. Zero Dynamic Heap Allocation (`0 B`)
Embedded systems cannot tolerate heap fragmentation or allocation failures:
- Coroutine frames are drawn from static atomic pools (`CoroutineStaticPool`).
- Communication queues use fixed-capacity ETL rings (`etl::queue_spsc_isr`).
- Dynamic memory (`malloc`, `new`) is never used in the execution path.

### 3. Background Hardware I/O Execution
The main thread never spins waiting on slow peripheral clocking (I2C, SPI, UART):
- **Bare-Metal MCU (RP2350 Pico 2 W)**: Core 0 handles peripheral ISRs, DMA, and CYW43 Wi-Fi; Core 1 runs the cooperative coroutine loop.
- **Heterogeneous SoC (Allwinner A5E / Cubie)**: The XuanTie E907 RISC-V coprocessor services real-time I/O and exchanges data with Linux over shared SRAM.
- **Linux Host / SITL**: POSIX background workers absorb synchronous `ioctl()` delays and wake the coroutine reactor over `eventfd`.

### 4. Universal 64-Byte Messages (TLP) & CTF 1.8 Tracing
- Data packets between cores, processes, and network streams use a fixed 64-byte container (`Tlp64`).
- Telemetry, driver metrics, and coroutine state changes encapsulate binary Common Trace Format (CTF 1.8) event payloads.
- Includes 1 KB ping-pong buffer trace logging with UDP streaming (`:9870`) and file sinks (`trace.ctf`).

---

## 3. Supported Target Platforms

The same application logic compiles across all targets without `#ifdef` directives:

| Target Platform | Environment | Primary Role | Interconnect |
| :--- | :--- | :--- | :--- |
| **Linux Host / SITL** | Desktop / SBC (ARM64 / x86_64) | Simulation, Companion Daemons | POSIX `eventfd` & Loopback UDP |
| **Raspberry Pi Pico 2 W** | Dual-Core ARM Cortex-M33 (RP2350) | Core 0: I/O & Wi-Fi<br/>Core 1: Coroutine Loop | Hardware SIO FIFO & SPSC Ring |
| **Allwinner XuanTie E907** | 32-bit RISC-V Coprocessor | Real-time I/O Reactor & DMA | Shared SRAM A3/C (`0x40000000`) + Mailbox |
| **ESP32-P4** | Dual RISC-V (400 MHz) + FreeRTOS | Sensor Processing & Networking | Hardware IPC Mailboxes / PSRAM |
| **FPGA (Gowin Tang 9K/20K)**| Synthesizable SystemVerilog | IMU Auto-DMA & DShot Hardware | Dual-SPI (50 MHz) / PCIe-style TLPs |

---

## 4. AbstractX Visualizer Studio

A real-time observability dashboard implemented in Python with **`imgui-bundle`** (`Dear ImGui` + `ImPlot`):

```bash
# 1. Install dependencies
pip install -r tools/visualizer/requirements.txt

# 2. Run live visualizer (listening on UDP port 9870)
python3 tools/visualizer/abstractx_studio.py --port 9870

# 3. Or run standalone simulation mode
python3 tools/visualizer/abstractx_studio.py --sim
```

### Dashboard Features:
* **Window 1 (Platform Topology)**: Displays detected platform architecture (Linux, Linux+E907, Pico 2, etc.), active cores, and ring buffer saturation.
* **Window 2 (Dual-Plane Timeline)**: Displays hardware I/O driver events on Plane 1 and coroutine tasks on Plane 2. Clicking any event opens the **Source Code Inspector** showing the exact source file and line number.
* **Window 3 (SPU/CPU Utilization)**: Reports per-core CPU usage, idle sleep duty cycles, and external background process interference.
* **8 kHz Sensor Oscilloscope**: Real-time ImPlot waveform display for accelerometer, gyroscope, and GPS data.

---

## 5. Quick Start & Build Instructions

### Build and Run Host Tests:
```bash
cmake --preset host
cmake --build build_host
ctest --test-dir build_host --output-on-failure
```

### Build Raspberry Pi Pico 2 W Target:
```bash
cmake --preset pico2w
cmake --build build_pico2w
# Produces build_pico2w/apps/gps_imu_app/gps_imu_app.uf2
```

### Build XuanTie E907 Target:
```bash
cmake --preset e907
cmake --build build_e907
# Produces build_e907/apps/e907_coprocessor/e907_coprocessor.elf
```

---

## 6. Repository Layout

```
AbstractX/
├── apps/
│   └── gps_imu_app/          # Unified portable sensor application (src/main.cpp)
├── docs/                     # Architecture specifications & pinout maps
│   ├── ABSTRACTX_PLATFORM_TOPOLOGY_AND_METRICS_SPEC.md
│   ├── ABSTRACTX_VISUALIZER_SPECIFICATION.md
│   ├── DESIGN_SPECIFICATION.md
│   └── HOW_TO_CTF_PING_PONG_TRACING.md
├── include/abstractx/        # Public C++20 headers
│   ├── abstractx.hpp         # Master unified runtime API (init, spawn, step, run)
│   ├── coro.hpp              # C++20 coroutine definitions & static frame pool
│   ├── platform_topology.hpp # Platform topology descriptor table
│   └── hal/                  # Generic HAL interfaces (SPI, I2C, UART, Timer, GPIO)
├── src/                      # Master runtime implementation (runtime.cpp)
├── targets/                  # Silicon target BSPs (linux, pico2w_rp2350, allwinner_e907)
├── sim/                      # CTest automated test suite (23 passing unit tests)
└── tools/
    └── visualizer/           # Python Dear ImGui studio (abstractx_studio.py)
```

---

## 7. License

AbstractX is licensed under the GNU General Public License v3.0 or later (GPL-3.0-or-later). Commercial licensing terms are available upon request.