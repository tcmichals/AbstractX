# AbstractX Design Goals

**Version:** 1.0  
**Branch:** `feature/universal-async-framework`  
**Status:** Authoritative — all other docs must agree with this document.

---

## 1. What AbstractX Is

> **AbstractX is a universal, zero-allocation, portable async I/O framework for real-time embedded systems.**

It provides a single C++20 coroutine application layer that runs identically across:
- FPGA hardware offloaders (Gowin Tang 9K/20K, Zynq-7020)
- Dual-core microcontrollers (RP2350, ESP32-P4, STM32H7)
- Linux hosts / SITL simulators (RPi 5, Jetson, x86)

The **Gowin FPGA flight controller offload board** is the canonical reference implementation of the framework.

---

## 2. What AbstractX Is NOT

- **Not a full RTOS.** AbstractX provides the async I/O layer. Scheduling and preemption are left to the application or hardware.
- **Not a complete flight stack.** It provides sensor transport and motor output. EKF, PID, and navigation algorithms live on top.
- **Not a Modbus / CAN / ROS2 framework.** Those are future integration targets, not current implementations.
- **Not a replacement for Linux kernel drivers.** On Linux it wraps existing `ioctl()` drivers in background workers.

---

## 3. Three-Layer Architecture

```
+----------------------------------------------------------------+
|  LAYER 3: APPLICATION (C++20 Coroutines)                       |
|  100% portable code. Same source across all platforms.         |
|  co_await io.async_read(REG_SENSOR)                            |
|  co_await when_all(read_imu(), read_baro())                    |
+----------------------------+-----------------------------------+
                             | Lock-free SPSC queues
+----------------------------v-----------------------------------+
|  LAYER 2: MESSAGE (64-byte TLP)                                |
|  Fixed-size PCIe-like packets. CRC32. Hardware timestamps.     |
|  MemRd / MemWr / CplD / DMA_Stream / DMA_Cfg                  |
|  Compact 24B on MCU, Full 64B on FPGA, Descriptor on Linux     |
+----------------------------+-----------------------------------+
                             | Platform-specific transport
+----------------------------v-----------------------------------+
|  LAYER 1: TRANSPORT (Platform-specific)                        |
|  FPGA:   Dual-SPI 50 MHz / AXI-Stream                         |
|  MCU:    Core-to-Core SRAM + ISR/DMA callbacks                 |
|  Linux:  POSIX worker threads + eventfd                        |
+----------------------------------------------------------------+
```

---

## 4. Design Invariants (Non-Negotiable)

These rules apply to ALL code in `include/`. Tests may relax rules 1-3 with explicit comments.

### 4.1 Freestanding C++ Headers
`include/` headers MUST NOT include `<mutex>`, `<thread>`, `<iostream>`, `<vector>`, `<list>`,
or call dynamic `operator new` / `malloc` without a static pool override.

### 4.2 No `.resume()` From ISR or Worker Thread
Background threads and ISRs MUST push events into a lock-free SPSC queue.
The **main coroutine thread** is the ONLY context that calls `.resume()`.

### 4.3 SPSC Queues Only for Cross-Context Communication
No `std::mutex` in the hot data path. SPSC is the primitive.

### 4.4 Static Memory Pools for Coroutine Frames
Pool `operator new` MUST use `std::atomic<size_t>` with CAS for dual-core safety.
Bounds check BEFORE committing the allocation.

### 4.5 Big-Endian on Wire, Native in Structs
ASP wire protocol is Big-Endian. C/C++ structs use native endianness with explicit byte-swaps.

### 4.6 Tag Exhaustion Must Be Detectable
`allocate_tag()` returning `0` is not a silent failure. Callers MUST handle it.

---

## 5. Naming Conventions

| Namespace | Meaning |
|---|---|
| `abstractx::` | Root namespace |
| `abstractx::coro::` | C++20 coroutine primitives |
| `ASP` | AbstractX Switch Protocol — the **wire protocol** |
| `AbstractX Runtime` | The C++20 coroutine engine — the **software layer** |
| `asp_` prefix | All SystemVerilog RTL modules |

---

## 6. Implemented vs. Roadmap Status

| Feature | Status |
|---|---|
| ASP 64B TLP wire protocol | ✅ Implemented |
| FPGA RTL (SPI, Router, IMU Auto-DMA, Wishbone) | ✅ Implemented |
| Lock-free SPSC ring buffer | ✅ Implemented |
| `Task<T>`, `when_all`, `when_any` | ✅ Implemented |
| Linux SITL worker thread I/O bridge | ✅ Implemented |
| Multi-core MCU SPSC ISR handoff | ✅ Implemented |
| Gowin Tang 9K / 20K bitstream | ✅ Implemented |
| Python verification CLI | ✅ Implemented |
| Free-list static frame allocator | 🗺️ Roadmap |
| Per-awaiter timeout watchdog | 🗺️ Roadmap |
| Linux `eventfd` + `epoll_wait` idle | 🗺️ Roadmap |
| `TlpShort` (24B MCU compact format) | 🗺️ Roadmap |
| RP2350 PIO / SIO FIFO mailbox driver | 🗺️ Roadmap |
| ESP32-P4 dual-core RISC-V driver | 🗺️ Roadmap |
| ArduPilot `AP_HAL_AbstractX` backend | 🗺️ Roadmap |
| ROS2 / Micro-ROS integration | 🗺️ Future (no timeline) |
| Modbus / RS-485 driver | 🗺️ Future (no timeline) |
| CAN / CANopen driver | 🗺️ Future (no timeline) |

---

## 7. Document Hierarchy

All docs must be consistent with this document. This document wins on conflict.

```
ABSTRACTX_DESIGN_GOALS.md   <- YOU ARE HERE (top authority)
├── ASP_PROTOCOL.md           <- Wire format (normative)
├── FCPROTOCOL_SPECIFICATION.md <- Register map & C API
├── DESIGN_RULES.md           <- Engineering rules
├── ABSTRACTX_SOFTWARE_ENVIRONMENTS.md <- 3-environment architecture
├── COROUTINE_FLIGHT_CONTROLLER_ARCHITECTURE.md <- C++20 layer design
├── ASP_REQUIREMENTS.md       <- Feature requirements
└── ASP_VALIDATION_MATRIX.md  <- Test gates & evidence
```
