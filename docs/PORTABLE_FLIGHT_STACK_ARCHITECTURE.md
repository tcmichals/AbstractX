# AbstractX PCIe-like TLP Unified Flight Architecture: Chip-Specific Offloading & Decoupling

## 1. Core Vision & Architectural Breakthrough

Flight control software (**Betaflight**, **iNav**, **ArduPilot / ArduCopter**) has historically been tightly coupled to physical microcontroller hardware. On dual-core processors like the **Raspberry Pi RP2350 (Pico 2)**, asymmetric SoCs like the **Allwinner Cubie A5E (ARM Cortex-A55 + T-Head E907 RISC-V)**, or standard microcontrollers like **STM32 H7 / F7 / G4**, developers struggle with where to place driver logic, how to handle inter-core communication, and how to prevent interrupt jitter from stalling the primary flight loop.

The **AbstractX PCIe TLP Architecture** establishes **PCIe-like 64-Byte TLPs (`asp-tlp-64b`)** as the universal inter-core and inter-chip transport. Crucially, this abstraction allows **each chip architecture to deploy its unique hardware offload capabilities** (FPGA, PIO, or STM32 Timer/DMA) under the hood without changing a single line of flight code!

```
+-----------------------------------------------------------------------------------+
|               EXISTING BETAFLIGHT / INAV / ARDUPILOT AP_HAL APIs                  |
|               (Preserves 100% backward compatibility with Flight Code & MSP/MAVLink)|
+-----------------------------------------------------------------------------------+
                                         │
                                         ▼
+-----------------------------------------------------------------------------------+
|                   UNIFORM 64-BYTE PCIe TLP MESSAGE BUS (tlp64_ring)              |
|        - MemRd (0x01), MemWr (0x02), CplD (0x03), DMA_Stream (0x10)               |
+-----------------------------------------------------------------------------------+
                                         │
       ┌─────────────────────┬───────────┴───────────┬─────────────────────┐
       ▼                     ▼                       ▼                     ▼
+-------------------+ +-------------------+ +-------------------+ +-------------------+
| RP2350 (Pico 2)   | | Allwinner CubieA5E| | STM32 H7/F7/G4    | | Linux / Jetson    |
| - PIO State Mach. | | - FPGA Coprocessor| | - TIM Input Capt. | | - ArduPilot Linux |
| - Core 0 TLP Engine| | - E907 RISC-V Core| | - MDMA Streams    | | - AP_HAL_AbstractX|
| - SRAM Ring Buffer| | - Dual-SPI MAC    | | - HRTIM DShot     | | - Shared RAM / TLP|
+-------------------+ +-------------------+ +-------------------+ +-------------------+
       │                     │                       │                     │
       ▼                     ▼                       ▼                     ▼
+-----------------------------------------------------------------------------------+
|                  HARDWARE OFFLOAD EXECUTION & PHYSICAL IO LAYER                   |
+-----------------------------------------------------------------------------------+
```

---

## 2. Multi-Threaded Execution Model (ArduPilot ChibiOS / POSIX pthreads vs Lock-Free TLP Rings)

Unlike Betaflight/iNav (which use bare-metal non-blocking super-loops), **ArduPilot** uses multi-threaded execution (ChibiOS threads on RTOS, POSIX `pthreads` on Linux):

```
+-----------------------------------------------------------------------------------+
|                     ARDUPILOT MULTI-THREADED ARCHITECTURE                         |
|                                                                                   |
|  [Main Flight Thread]      [EKF3 Navigation Thread]    [MAVLink / Telemetry Thread]|
|  - 400 Hz Attitude PID     - 24-State EKF Matrix       - MAVLink 2 / ROS2 / GCS    |
|  - Motor Output Command    - Position/Velocity Calc    - Logging & Companion AI   |
+-----------------------------------------------------------------------------------+
                                         ▲
                                         │  Lock-Free 64B TLP Shared Memory Rings
                                         │  (Zero Mutex Locks, Zero Context Switch Stalls)
                                         ▼
+-----------------------------------------------------------------------------------+
|                     PHYSICAL HARDWARE COPROCESSOR LAYER                           |
|       (Gowin FPGA / RP2350 Core 0 + PIO / STM32 TIM IC + MDMA Streams)            |
|                                                                                   |
|  - Autonomous SPI Sensor Polling & 64-bit Nanosecond Hardware Timestamping        |
|  - Pushes ready 64-Byte DMA_Stream TLPs into Lock-Free Shared RAM Rings           |
+-----------------------------------------------------------------------------------+
```

### Why AbstractX Elevates Multi-Threaded ArduPilot Execution:
1. **Elimination of Mutex Locks & Thread Contention**:
   - In standard ArduPilot, the SPI Thread and EKF Thread compete for mutexes (`AP_HAL::Semaphore`). If SPI is stalled, the EKF Thread blocks.
   - Under AbstractX, hardware pushes 64-byte `DMA_Stream` TLPs into **lock-free single-producer single-consumer (SPSC) shared RAM ring buffers** (`tlp64_ring_pop()`). **Zero mutexes, zero thread blocking!**
2. **Elimination of Dedicated High-Rate SPI Threads**:
   - ArduPilot no longer wastes CPU clock cycles running a dedicated 1 kHz / 8 kHz SPI polling thread. The hardware offloader handles 100% of SPI clocking and register parsing in the background.
3. **Sub-Microsecond EKF3 Timestamp Accuracy**:
   - Latching `timestamp_ns` in hardware on the exact clock cycle `DRDY` fires eliminates OS thread scheduling jitter ($20\text{--}50\ \mu\text{s}$), preventing EKF3 velocity derivative errors.

---

## 3. ArduPilot / ArduCopter Integration (`AP_HAL_AbstractX` & EKF3 Alignment)

ArduPilot relies on **`AP_HAL` (ArduPilot Hardware Abstraction Layer)** to abstract physical hardware devices across ChibiOS, Linux, and SITL targets.

### 3.1 Universal `AP_HAL_AbstractX` Backend
Instead of writing complex board-specific Linux `/dev/spidev` or ChibiOS SPI drivers:
- ArduPilot implements a single universal **`AP_HAL_AbstractX`** implementation for `SPIDevice`, `UARTDriver`, `GPIO`, and `RCOutput`.
- Peripherals are accessed via 64-byte PCIe TLPs over `pcie_reg_read32()` / `pcie_reg_write32()` and `tlp64_ring_pop()`.

### 3.2 Sub-Microsecond Hardware Timestamping for EKF3
ArduPilot's **EKF3 (Extended Kalman Filter 3)** estimates 24+ vehicle state variables (position, velocity, orientation, gyro bias, accel bias, wind, mag bias). EKF3 accuracy depends critically on precise sensor measurement timestamps (`sensor_timestamp_us`).
- **Software Jitter Elimination**: Under legacy Linux/MCU drivers, software interrupt latency jitter ($20\text{--}50\ \mu\text{s}$) corrupts EKF3 velocity derivatives.
- **Hardware Precision**: AbstractX's hardware offloader latches the 64-bit nanosecond clock **on the exact clock cycle `DRDY` fires** and passes `timestamp_ns` directly into `AP_NavEKF3`. Jitter is reduced to **$< 20\text{ ns}$**, making EKF3 state estimation rock-solid.

---

## 4. Hardware Offload Execution Engines Across Targets

### 4.1 FPGA Offloader (Allwinner Cubie A5E + Gowin FPGA)
- Uses dedicated FPGA hardware state machines (`asp_imu_auto_dma.sv`, `asp_spi_frontend.sv`).
- Sub-microsecond timestamping latched on exact FPGA clock cycle.
- Dual-SPI transport to CPU over 64B TLPs.

### 4.2 PIO Offloader (Raspberry Pi RP2350 / Pico 2)
- Uses RP2350 PIO state machines (`dshot.pio`, `uart_pio.c`) and Core 0.
- Hardware timestamping latched via PIO timer counter.
- Inter-core SRAM ring buffer transport to Core 1 over 64B TLPs.

### 4.3 Internal Processor Timers & DMA Offloader (STM32 H7 / F7 / G4 / AT32)
- Uses **STM32 Timer Input Capture (TIM IC)** directly triggered by the IMU `DRDY` GPIO pin to latch nanosecond hardware timestamps.
- Uses **STM32 Hardware DMA (BDMA / MDMA)** to autonomously fetch 14-byte SPI sensor bursts directly into 64-byte TLP structs in STM32 SRAM.
- Uses **STM32 HRTIM / Advanced Timers** to output DShot motor signals.
- The flight software CPU core receives ready 64-byte TLPs with zero software polling stalls!

---

## 5. Architectural Summary

1. **100% Backward Compatibility**: Keeps Betaflight / iNav / ArduPilot `AP_HAL` APIs intact so all configurator tools, MAVLink telemetry, and ROS2 companion computer features work out of the box.
2. **Chip-Specific Acceleration Unlocked**: Each silicon target deploys its specialized hardware (Pico 2 PIO, STM32 Timer Input Capture + MDMA, Gowin FPGA) without changing flight software code.
3. **Zero Inter-Processor Friction**: All inter-core communication is standardized on 64-byte PCIe TLPs in lock-free ring buffers.
