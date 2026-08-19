# AbstractX

**Universal Asynchronous Hardware Offloader & Heterogeneous Interconnect Framework**  
*Small. Deterministic. Zero-Blocking I/O for FPGAs, Microcontrollers, and Linux Hosts.*

---

## 1. Overview & Vision

Modern real-time systems—whether in **Robotics (ROS2)**, **Industrial Automation & DAQ**, **Power Electronics (BMS)**, or **Autonomous Flight (iNav / ArduPilot)**—struggle with a fundamental architecture problem: **The I/O Latency Mismatch**.

High-frequency control and telemetry algorithms cannot afford to block while slow physical peripheral buses (400 kHz I2C, 100 kHz Modbus, slow SPI ADC/DACs, UART serial) clock out data:
- In **bare-metal firmware**, developers are forced to write fragile, fragmented **callback state machines** across global tick timers.
- In **Linux systems**, `/dev/spidev` and `/dev/i2c-dev` use synchronous blocking `ioctl()` calls without `epoll` support, forcing multi-threading that causes **OS scheduling jitter ($20\text{--}50\ \mu\text{s}$), cache thrashing, and mutex lock contention**.

**AbstractX solves this permanently** by introducing a unified, multi-platform architecture:
1. **PCIe-like Split-Transaction TLPs (`asp-tlp`)**: Request operations (`MemRd`, `MemWr`) are tagged and dispatched asynchronously; completions (`CplD`, `DMA_Stream`) are posted into lock-free rings when hardware finishes.
2. **C++20 Stackless Coroutines (`asp_coro`)**: Application code is written sequentially using `co_await`, `when_all` (`&&`), and `when_any` (`||`) on a single real-time thread with **zero OS context switches, zero mutexes, and zero dynamic heap allocation**.
3. **Hardware Offloading Across Silicon**: FPGAs (Gowin / Zynq), Microcontrollers (RP2350 Pico 2W, ESP32-P4, STM32), and Linux hosts execute I/O autonomously with **sub-20ns hardware timestamping**.

```
+───────────────────────────────────────────────────────────────────────────────────+
|               APPLICATION LAYER (C++20 Stackless Coroutine Engine)                |
|                                                                                   |
|  [Robotics Joint Loop]       [Industrial DAQ Stream]      [Multi-Sensor Sync]     |
|  co_await next_telemetry()    co_await adc_burst()         co_await when_all(     |
|  (Executes, yields)           (Autonomous DMA stream)        read_sensor_a(),     |
|         │                            │                       read_sensor_b())     |
+─────────┼────────────────────────────┼────────────────────────────┼───────────────+
          │                            │                            │
          ▼                            ▼                            ▼
+───────────────────────────────────────────────────────────────────────────────────+
|               LOCK-FREE SPSC TLP RING BUFFERS (SpscTlpRing)                       |
|          - Host TX Ring (MemRd / MemWr)     - Host RX Ring (CplD / DMA_Stream)    |
+───────────────────────────────────────────────────────────────────────────────────+
                                       ▲
                                       │ Universal Split-Transaction Protocol
                                       ▼
+───────────────────────────────────────────────────────────────────────────────────+
|               HARDWARE OFFLOADER / HETEROGENEOUS COPROCESSOR LAYER                |
|  - FPGA Fabric: Autonomous SPI/I2C state machines & 512-bit vector router         |
|  - RP2350 PIO State Machines & SIO Ring / ESP32-P4 Dual RISC-V 400MHz Mailboxes   |
|  - Linux SITL / Host: Background I/O worker threads handling physical bus ioctl   |
+───────────────────────────────────────────────────────────────────────────────────+
```

---

## 2. Key Multi-Domain Application Profiles

AbstractX is domain-agnostic and provides tailored acceleration across multiple industries:

```
                 ┌──────────────────────────────────────────────┐
                 │       AbstractX Universal Core Engine        │
                 │   (C++20 Coroutines + Lock-Free 64B TLPs)    │
                 └──────────────────────┬───────────────────────┘
                                        │
      ┌──────────────────┬──────────────┴─────┬──────────────────┐
      ▼                  ▼                    ▼                  ▼
┌─────────────┐    ┌─────────────┐      ┌─────────────┐    ┌─────────────┐
│  Robotics & │    │ Industrial  │      │   Battery   │    │  Aviation & │
│  ROS2 Nodes │    │ DAQ & PLCs  │      │ Management  │    │  Motion     │
│             │    │             │      │ (BMS/Power) │    │             │
│• Multi-axis │    │• 1MSPS ADC  │      │• Multi-cell │    │• 8kHz IMU   │
│  Servo/CAN  │    │  vibration  │      │  voltage/T  │    │  Auto-DMA   │
│• Kinematics │    │• Isolated   │      │• <20ns fast │    │• DShot/ESC  │
│  in Coro    │    │  SPI/Modbus │      │  fault trip │    │• EKF3 Sync  │
└─────────────┘    └─────────────┘      └─────────────┘    └─────────────┘
```

1. **Robotics & Motion Control (ROS2 / Micro-ROS)**:
   - Synchronizes multi-axis servo loops, CAN/RS-485 telemetry, and joint kinematics in straight-line coroutines using `co_await when_all(joint1.write(), joint2.write(), ...)`.
2. **Industrial Data Acquisition & Automation**:
   - Streams 1MSPS ADC vibration data continuously over `DMA_Stream` TLPs while servicing slow Modbus/I2C environmental sensors without buffer overruns.
3. **Power Electronics & Battery Management Systems (BMS)**:
   - High-voltage multi-cell voltage and temperature monitoring over isolated SPI daisy chains with sub-20ns hardware fault-trip timestamping.
4. **Aviation & Autonomous Flight Control**:
   - Flagship reference implementation for iNav, Betaflight, and ArduPilot with 8 kHz IMU Auto-DMA streaming, DShot motor output, and nanosecond EKF3 state estimation.

---

## 3. Cross-Platform Silicon Matrix

AbstractX adapts its message framing to match the physical architecture of each target:

| Target Platform | Primary Role | Message Format | Transport Layer |
|---|---|---|---|
| **Gowin Tang 9K / 20K** | Ultra-low-latency FPGA offloader | **Fixed 64B (`Tlp64`)** | Dual-SPI (50 MHz) / AXI-Stream |
| **QMTECH Zynq-7020** | Linux Host (A9) + FPGA PL Fabric | **Fixed 64B / DMA Stream**| AXI DMA character dev / TUN |
| **RP2350 (Pico 2 / Pico 2W)** | Dual-Core Real-time Sensor Hub | **Compact 24B (`TlpShort`)** | Core 0 $\leftrightarrow$ Core 1 Shared SRAM Ring |
| **ESP32-P4 (Dual RISC-V 400MHz)**| High-Performance Edge Gateway | **Variable 24B–256B (`TlpVar<N>`)**| Hardware Mailboxes / PSRAM Streams |
| **Linux Host (RPi 5 / Jetson / SITL)**| High-Level AI / Supervisor / SITL | **Zero-Copy (`TlpDescriptor`)**| POSIX I/O Workers + SPSC Rings |

### Why FPGA Uses Fixed 64-Byte vs. Processors Using Compact/Variable:
- **FPGA (`Tlp64`)**: Fixed 512-bit vectors eliminate dynamic byte-counter state machines and variable FIFO alignment logic, saving precious LUTs and maximizing $f_{MAX}$.
- **Processors (`TlpShort`, `TlpVar`, `TlpDescriptor`)**: Passing compact 24-byte structs for register R/W eliminates copying 40 bytes of zero-padding, conserving MCU SRAM and keeping CPU L1/L2 cache lines hot.

### Bare-Metal Microcontroller Portability (Zero OS / Freestanding C++20):
- **100% Freestanding Core Headers**: Zero dependencies on `<mutex>`, `<thread>`, `<condition_variable>`, `<iostream>`, or dynamic memory allocation.
- **Single-Core MCU ISR Safety (STM32G4 / AT32 / S3)**: Lock-free SPSC queues are **100% ISR-safe and re-entrant without critical sections (`__disable_irq()`)**, because the ISR only writes `tail_` while the main coroutine loop only writes `head_`.
- **Dual-Core AMP (RP2350 Pico 2W / ESP32-P4)**: Core 1 runs the main coroutine loop, Core 0 runs low-level I/O engines, communicating across shared SRAM via lock-free SPSC arrays with hardware memory barriers (`DMB` / `FENCE`).

---

## 4. Software Architecture: C++20 Coroutines

The AbstractX C++20 header library ([`include/asp_coro.hpp`](include/asp_coro.hpp)) provides embedded-ready, zero-allocation asynchronous primitives:

### A. Non-Blocking Split Transactions
```cpp
// Straight-line async register read over I2C/SPI:
Tlp64 resp = co_await io.async_read(REG_SENSOR_DATA);
```

### B. Concurrent Join (`when_all` / `&&`)
```cpp
// Simultaneously dispatch requests across separate physical buses:
auto [sensor_a, sensor_b] = co_await when_all(
    io.async_read(REG_BUS0_SENSOR),
    io.async_read(REG_BUS1_SENSOR)
);
```

### C. Watchdog Timeout & Racing (`when_any` / `||`)
```cpp
// Race a slow I/O transaction against a hardware watchdog timer:
auto result = co_await when_any(
    io.async_read(REG_OPTICAL_FLOW),
    hardware_timeout(1000us)
);
if (result.index() == 1) {
    handle_sensor_timeout(); // Fall back to safe state
}
```

---

## 5. Repository Structure

```
AbstractX/
├── include/                      # Universal C/C++ Header Library
│   ├── asp_coro.hpp              # C++20 Coroutines, Tasks, when_all/when_any, IoEngine
│   ├── asp_tlp_msg.hpp           # Universal Header, Tlp64, TlpShort, TlpVar, TlpDescriptor
│   ├── asp_tlp64.hpp             # Strongly typed C++20 Tlp64 wrappers
│   ├── asp_tlp64.h               # Normative C 64-byte TLP struct & wire constants
│   ├── spsc_tlp_ring.hpp         # Lock-free, wait-free SPSC TLP ring buffer
│   └── pcie_bar_map.hpp          # Universal PCIe Register BAR address map
├── rtl/                          # Synthesizable SystemVerilog FPGA IP Cores
│   ├── top/asp_top.sv            # Top-level integration & skid buffer
│   ├── spi/asp_spi_frontend.sv   # Dual-SPI SDR 50MHz physical link layer
│   ├── router/asp_router.sv      # AXIS 512-bit vector switch fabric
│   ├── wishbone/asp_wishbone_master.sv # Wishbone gateway & register bank
│   └── imu/asp_imu_auto_dma.sv   # Autonomous SPI master, DRDY trigger & timestamping
├── sim/                          # Simulation, SITL & Verification
│   ├── sitl_coro_sim.cpp         # C++20 Coroutine SITL multi-bus simulator benchmark
│   ├── test_coro_combinators.cpp # when_all / when_any unit verification
│   ├── test_tlp_msg.cpp          # Multi-target TLP container verification
│   └── cocotb/                   # Synthesizable Verilog Cocotb testbenches
├── tools/                        # Python Verification & Interactive CLI
│   ├── test_asp_pcie.py          # Interactive register CLI & NeoPixel/PWM sweep test
│   └── example_imu_whoami.py     # End-to-end WHO_AM_I register read walkthrough
├── hw/                           # Board Constraints & Bitstream Flows
│   ├── tang9k/                   # Gowin GW1NR-9 (Tang Nano 9K)
│   ├── primer20k/                # Gowin GW2A-18 (Tang Primer 20K)
│   └── qmtech_zynq7020/          # Xilinx Zynq-7020 SoC Bring-up Notes & Buildroot
└── docs/                         # Specifications & Architecture Guides
    ├── COROUTINE_FLIGHT_CONTROLLER_ARCHITECTURE.md # Coroutine execution & SITL guide
    ├── PORTABLE_FLIGHT_STACK_ARCHITECTURE.md       # Target offloader architecture
    ├── ASP_PROTOCOL.md           # Normative TLP wire protocol specification
    └── IMU_AUTO_DMA_IP_SPEC.md   # Hardware Auto-DMA core specification
```

---

## 6. Quick Start & Verification Benchmarks

### 1. Run Multi-Target Architectural Benchmark (Linux, Pico 2, ESP32, FPGA):
```bash
g++ -std=c++20 -O2 -pthread -Iinclude examples/simple_proof_benchmark.cpp -o examples/simple_proof_benchmark && ./examples/simple_proof_benchmark
```
*Proves that the exact same C++20 coroutine driver (`universal_ms5611_baro_driver`) runs with 100% bit-exact mathematical parity across Linux POSIX workers, Pico 2 (RP2350) dual-core SRAM, ESP32-P4/S3 DMA ISRs, and FPGA Auto-DMA with 0 dynamic heap allocations and 0 mutexes.*

### 2. Run Flight SITL Multi-Sensor Coroutine Simulation (8 kHz IMU + Mag + Baro + EKF3):
```bash
g++ -std=c++20 -O3 -Iinclude sim/sitl_coro_sim.cpp -o sim/sitl_coro_sim && ./sim/sitl_coro_sim
```

### 3. Run Bare-Metal MCU Single-Core & Dual-Core SPSC Verification:
```bash
g++ -std=c++20 -O3 -Iinclude sim/test_baremetal_isr_spsc.cpp -o sim/test_baremetal_isr_spsc && ./sim/test_baremetal_isr_spsc
```

### 4. Run Dedicated Lock-Free SPSC Array Benchmark (Zero Mutexes, Zero Heap):
```bash
g++ -std=c++20 -O3 -Iinclude sim/test_spsc_channel_array.cpp -o sim/test_spsc_channel_array && ./sim/test_spsc_channel_array
```

### 5. Run Multi-Target Message Sizing Verification (FPGA vs CPU):
```bash
g++ -std=c++20 -O2 -Iinclude sim/test_tlp_msg.cpp -o sim/test_tlp_msg && ./sim/test_tlp_msg
```

### 6. Interactive Hardware CLI (via Python):
```bash
# Test register R/W in simulation mock mode:
python3 tools/test_asp_pcie.py --mock --mode cli

# Run interactive NeoPixel & Servo PWM sweep on physical hardware:
python3 tools/test_asp_pcie.py --spidev /dev/spidev0.0 --mode rainbow
```

---

## 7. Licensing Model

AbstractX is released under a **Dual-License Model**:
- **Open Source (GPLv3)**: Free to use for open-source projects, academic research, and community experimentation.
- **Commercial License**: Available for commercial products requiring closed-source firmware integration, dedicated hardware support, or proprietary extensions. Contact the maintainers for licensing inquiries.