# AbstractX System Invariants & Architecture Vision

**Document:** `docs/tier1_vision/SYSTEM_INVARIANTS.md`  
**Status:** Authoritative Foundation (Tier 1 SSOT) — All specifications and code must strictly comply.

---

## 1. What AbstractX Is (And Is NOT)

> **AbstractX is a universal, zero-allocation, hardware-software co-design framework for real-time aerospace, robotics, and embedded systems.**

It provides a single C++20 stackless coroutine application layer that executes symmetrically across:
1. **FPGA Hardware Switch Fabrics**: Synthesizable SystemVerilog auto-DMA cores on Gowin Tang 9K/20K, Zynq-7000.
2. **Dual-Core Microcontrollers**: Asymmetric Multiprocessing on RP2350 (Pico 2 W) and ESP32-P4.
3. **Heterogeneous Linux Hosts & Coprocessors**: PREEMPT_RT Linux host + Allwinner XuanTie E907 RISC-V coprocessor, and Desktop SITL.

### What AbstractX Is NOT
* **NOT a heavy multi-task RTOS**: AbstractX avoids thread proliferation. Instead of spawning 5+ preemptive OS threads with separate 4 KB stacks, it uses a single CPU stack and cooperative coroutine task frames.
* **NOT complex serialization middleware**: AbstractX uses fixed 64-byte binary PCIe-style Transaction Layer Packets (TLPs) with zero runtime serialization overhead.
* **NOT a blocking HAL**: AbstractX drivers never execute blocking `delay_ms()`, polling spinloops, or synchronous bus calls (`read_sync()`, `write_sync()`). All I/O yields execution cooperatively via `co_await`.

---

## 2. The Three-Layer Architecture

```
┌────────────────────────────────────────────────────────────────────────┐
│  LAYER 3: APPLICATION (C++20 Stackless Coroutines)                     │
│  100% portable code. Same source across MCU, Linux, and FPGA.          │
│  co_await io.async_read(REG_SENSOR)                                    │
│  co_await when_all(read_imu(), read_baro())                            │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │ Lock-free SPSC queues
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│  LAYER 2: MESSAGE (64-Byte Transaction Layer Packet - TLP)             │
│  Fixed-size PCIe-like packets. CRC32. Hardware nanosecond timestamps.  │
│  MemRd / MemWr / CplD / DMA_Stream / DMA_Cfg                           │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │ Platform-specific transport
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│  LAYER 1: TRANSPORT (Silicon & Bus Specific)                           │
│  FPGA:   Dual-SPI 50 MHz / AXI-Stream Crossbar (asp_router.sv)          │
│  MCU:    Core-to-Core Shared SRAM + Hardware Doorbells (SIO / IPC)     │
│  Linux:  POSIX epoll / Shared SRAM A3/C + msgbox mailboxes             │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Core Architectural Invariants (Non-Negotiable)

### 3.1 Freestanding C++20 Rules (Zero Dynamic Heap)
All code under `include/`, `apps/`, and `targets/` must compile freestanding without host OS runtime support:
* **Strictly Prohibited Headers**: `<mutex>`, `<thread>`, `<condition_variable>`, `<iostream>`, `<vector>`, `<list>`, `<map>`, `<queue>`, `<string>`.
* **Prohibited Calls**: `malloc()`, `free()`, `operator new`, `operator delete`.
* **Permitted Headers**: `<coroutine>`, `<atomic>`, `<array>`, `<optional>`, `<variant>`, `<tuple>`, `<cstdint>`, `<cstddef>`, `<utility>`, `<cmath>`.
* **Static Memory Pools**: All queues and rings must use statically sized structures (`SpscRingBuffer<T, N>`, `SpscTlpRing<N>`, `AsyncQueue<T, N>`) where $N$ is a power of 2.

### 3.2 Single Shared Stack vs Coroutine Frames
* **Single Stack**: The cooperative event loop runs on a single small CPU stack (< 2 KB).
* **Static Coroutine Frames**: Coroutine state frames reside in static memory or pre-allocated BSS pools (< 120 bytes per suspended task), completely eliminating thread stack bloat.

### 3.3 Execution Domain & Queue Safety Rules
* **Coroutine Domain vs ISR Domain**:
  * The coroutine domain (`abstractx::step()`) is cooperative and is the **sole context that calls `.resume()`**.
  * The ISR/DMA domain communicates exclusively by pushing completion tokens or packets into lock-free SPSC rings and ringing doorbells. **ISRs never resume coroutines directly.**
* **ETL Pattern Requirement**: Queues modified from ISR contexts must use interrupt-safe wrappers (`etl::queue_spsc_isr<T, N, abstractx::InterruptLock>`).
* **Ring Sizing**: Capacity MUST be a power of two with minimum capacity $\ge 8$ slots.

### 3.4 Wire Protocol & Endianness Convention
* **Wire Protocol (ASP TLP)**: Big-Endian for all multi-byte header fields.
* **C/C++ Structures in Memory**: Host native byte order.
* **Symmetry Mandate**: Serialization to wire or deserialization to memory must use explicit byte-swap functions (`bswap16`, `bswap32`, `bswap64`) or dynamic schema compilation.

---

## 4. The 4 Execution Environments

| Feature | Environment 1: Linux SITL / Host | Environment 2: Dual-Core MCU (Pico 2 W / ESP32-P4) | Environment 3: Heterogeneous Coprocessor (Radxa Cubie A5E Pure Silicon) | Environment 4: FPGA Hardware Offloader (A5E + FPGA / Tang / Zynq) |
| :--- | :--- | :--- | :--- | :--- |
| **I/O Engine** | POSIX Worker Threads (`epoll`) | Core 0 (Autonomous DMA / PIO / Wi-Fi) | XuanTie E907 (On-Chip SPI0/TWI/UART DMA) | SystemVerilog Auto-DMA (`asp_imu_auto_dma.sv` + AXI) |
| **Coroutine Engine** | Main Coroutine Loop | Core 1 (Isolated C++20 Coroutine Dispatcher) | ARM Cortex-A55 (PREEMPT_RT Thread) | Host SBC / MCU Coroutine Application |
| **Doorbell Bridge** | `eventfd` / Linux pipe | Hardware FIFO / IPC Mailbox Interrupt | Shared SRAM A3/C + `sun6i-msgbox` | Physical DIO Pin / Interrupt Line |
| **Memory Isolation** | Host RAM | Internal L2 SRAM (Zero PSRAM dependence) | Banked Internal SRAM (No PSRAM) | On-Chip Block RAM / FIFO Buffer |
| **FPGA Requirement** | None (0 LUTs) | None (0 LUTs) | **None (Pure Silicon, 0 LUTs)** | Gowin / AMD FPGA (~9k–85k LUTs) |
| **Typical Jitter** | `< 20 µs` (PREEMPT_RT) | `< 0.2 µs` (Dedicated Hardware Core) | `< 0.5 µs` (Dedicated RISC-V Core) | `< 0.01 µs` (Hardware Clock Cycle Deterministic) |

---

## 5. Primary-Paced Multi-Rate Channel Pattern

When ingesting sensors at different rates (e.g. 8 kHz IMU, 50 Hz Magnetometer, 10 Hz GPS):
* **Prohibited**: Modulus prescalers or fragmented tick counters (`if (++counter % 80 == 0)`).
* **Mandated**: The **Primary-Paced Coroutine Pattern**:
  1. The highest-frequency sensor (IMU DRDY @ 8 kHz) acts as the physical clock. The main fusion loop awaits it:
     ```cpp
     co_await g_imu_channel.pop();
     ```
  2. Auxiliary lower-rate sensors (Mag, GPS, Baro) are drained non-blockingly on each tick:
     ```cpp
     while (g_mag_channel.try_pop(mag_sample)) { ... }
     while (g_gps_channel.try_pop(gps_fix)) { ... }
     ```
  3. Control flow is 100% linear, sequential, and free of race conditions.
