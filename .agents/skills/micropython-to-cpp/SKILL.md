---
name: micropython-to-cpp
description: "Transforms MicroPython or Python asyncio peripheral drivers and multi-rate tasks into freestanding AbstractX C++20 coroutines with zero dynamic heap allocation."
---

# Skill: micropython-to-cpp

## Overview

In the AbstractX ecosystem, peripheral drivers and algorithmic data pipelines are frequently prototyped and validated in **MicroPython** (on the XuanTie E906 co-processor) or **Python `asyncio`** (on Linux). Once the hardware registers, bus timing, and protocol handshakes are confirmed in silicon, the logic is translated into **Freestanding C++20 Coroutines**.

Because Python's `asyncio` and AbstractX's C++20 execution engine share the identical structural concurrency model, this conversion is a systematic, 1-to-1 syntactic translation.

This skill governs the exact transformation rules, type mappings, zero-heap constraints, and verification steps.

---

## 1. Syntax & Primitive Mapping Table

| MicroPython / Python `asyncio` | AbstractX Freestanding C++20 | Invariant / Rule |
|---|---|---|
| `async def foo(...) -> T:` | `abstractx::coro::Task<T> foo(...) noexcept` | Return stackless coroutine task |
| `async def foo(...) -> None:` | `abstractx::coro::Task<void> foo(...) noexcept` | Void task specialization |
| `await expr` | `co_await expr` | Cooperative yield to dispatcher |
| `await asyncio.sleep(s)` | `co_await abstractx::coro::sleep_for(std::chrono::milliseconds(ms))` | Hardware timer awaiter |
| `await bus.read_reg_async(reg)` | `co_await m_bus.read_reg_async(reg)` | Event-driven bus transaction |
| `await bus.write_reg_async(reg, val)` | `co_await m_bus.write_reg_async(reg, val)` | Non-blocking write transaction |
| `q = asyncio.Queue(maxsize=N)` | `abstractx::AsyncQueue<T, N> q{};` | Fixed static ring buffer (0 B heap) |
| `await q.put(item)` | `co_await q.push(item);` | Asynchronous backpressured push |
| `item = await q.get()` | `T item = co_await q.pop();` | Primary-paced pacing await |
| `q.get_nowait()` | `q.try_pop(item)` | Non-blocking auxiliary drain |
| `struct.unpack(">hhhhhh", b)` | Native C++ `struct` with byte-swaps | Packed binary representation |
| `dict` / `list` | Fixed `struct` / `std::array<T, N>` / `etl::vector<T, N>` | **No dynamic heap (`std::vector`, `std::map`)** |
| `try: ... except Exception:` | `std::optional<T>` / `bool` status return | **No C++ exceptions (`-fno-exceptions`)** |
| `time.ticks_ms()` | `abstractx::hal::get_timestamp_ms()` | Hardware timer register |
| `time.ticks_us()` / `abstractx.cycles()` | `abstractx::hal::get_cycle_count()` | 64-bit hardware cycle count (`mcycle`) |

---

## 2. Mandatory Architectural Invariants

When translating Python code into AbstractX C++20:

1. **Zero Dynamic Allocation (0 B Heap)**:
   - Strictly ban: `<iostream>`, `<vector>`, `<string>`, `<map>`, `<queue>`, `malloc()`, `free()`, `new`, `delete`.
   - Use statically allocated members, member buffers, or `abstractx::AsyncQueue<T, N>`.
   - Coroutine frames are allocated from the 64-byte aligned static pool (`g_coro_static_frame_pool`).
2. **Zero Synchronous Bus I/O**:
   - Never introduce synchronous delays (`delay_ms()`, `usleep()`, `while(!ready);` spinloops).
   - Use asynchronous driver methods returning `coro::Task<bool>` or dedicated awaiters.
3. **Primary-Paced Channel Pattern**:
   - For multi-rate loops: the primary high-rate sensor (e.g. 8 kHz IMU DRDY) serves as the clock with `co_await g_imu_channel.pop()`.
   - Auxiliary lower-rate sensors (GPS, Mag, Baro) are drained non-blockingly using `while (g_aux_channel.try_pop(sample))`.
   - Never use modulo prescalers (`if (++counter % 80 == 0)`).
4. **Natural Alignment for Cross-Processor Structures**:
   - Any structure passed across RPMsg or shared SRAM must have natural alignment (`alignas(4)` or `alignas(8)`). Do NOT use `__attribute__((packed))` on 16/32-bit fields across boundaries.

---

## 3. Concrete Transformation Example

### Python / MicroPython Source
```python
# Prototype: Python asyncio sensor driver
import asyncio
import struct

class Bmp388Driver:
    REG_CHIP_ID = 0x00
    REG_CMD = 0x7E
    REG_PWR_CTRL = 0x1B
    REG_DATA = 0x04

    def __init__(self, bus):
        self.bus = bus

    async def init_async(self) -> bool:
        chip_id = await self.bus.read_reg_async(self.REG_CHIP_ID)
        if chip_id != 0x50:
            return False
        # Soft reset
        await self.bus.write_reg_async(self.REG_CMD, 0xB6)
        await asyncio.sleep(0.005)
        # Enable pressure and temp in normal mode
        await self.bus.write_reg_async(self.REG_PWR_CTRL, 0x33)
        return True

    async def read_sample_async(self):
        raw = await self.bus.read_bytes_async(self.REG_DATA, 6)
        p_raw, t_raw = struct.unpack("<II", raw + b"\x00\x00")
        p = p_raw & 0xFFFFFF
        t = (t_raw >> 8) & 0xFFFFFF
        return {"pressure_raw": p, "temp_raw": t}
```

### Translated AbstractX Freestanding C++20 Header & Implementation
```cpp
#pragma once

#include <cstdint>
#include <optional>
#include "asp_coro.hpp"
#include "abstractx/coro.hpp"
#include "hal/spi.hpp"

namespace abstractx {

struct Bmp388Sample {
    uint32_t pressure_raw{0};
    uint32_t temp_raw{0};
};

class Bmp388Driver {
public:
    static constexpr uint8_t REG_CHIP_ID  = 0x00;
    static constexpr uint8_t REG_CMD      = 0x7E;
    static constexpr uint8_t REG_PWR_CTRL = 0x1B;
    static constexpr uint8_t REG_DATA     = 0x04;
    static constexpr uint8_t EXPECTED_ID  = 0x50;

    explicit Bmp388Driver(hal::ISpi &bus) noexcept : m_bus(bus) {}

    // Asynchronous Initialization
    coro::Task<bool> init_async() noexcept {
        uint8_t chip_id = co_await m_bus.read_reg_async(REG_CHIP_ID);
        if (chip_id != EXPECTED_ID) {
            co_return false;
        }

        // Soft reset
        co_await m_bus.write_reg_async(REG_CMD, 0xB6);
        co_await coro::sleep_for(std::chrono::milliseconds(5));

        // Enable pressure and temp in normal mode
        co_await m_bus.write_reg_async(REG_PWR_CTRL, 0x33);
        co_return true;
    }

    // Asynchronous Sample Read
    coro::Task<std::optional<Bmp388Sample>> read_sample_async() noexcept {
        uint8_t buf[6]{};
        bool ok = co_await m_bus.read_bytes_async(REG_DATA, buf, sizeof(buf));
        if (!ok) {
            co_return std::nullopt;
        }

        uint32_t p_raw = static_cast<uint32_t>(buf[0]) |
                        (static_cast<uint32_t>(buf[1]) << 8) |
                        (static_cast<uint32_t>(buf[2]) << 16);

        uint32_t t_raw = static_cast<uint32_t>(buf[3]) |
                        (static_cast<uint32_t>(buf[4]) << 8) |
                        (static_cast<uint32_t>(buf[5]) << 16);

        co_return Bmp388Sample{p_raw, t_raw};
    }

private:
    hal::ISpi &m_bus;
};

} // namespace abstractx
```

---

## 4. Verification Procedure for Translated Drivers

Every translated C++20 driver must undergo standard verification:

1. **Freestanding Check**:
   Ensure zero forbidden headers (`<iostream>`, `<vector>`, `<string>`, `<mutex>`) are included.
2. **Host Unit & Sanitizer Suite**:
   Create a test harness in `tests/` using simulated/mock bus transactions pumped through `Dispatcher::process()`.
   ```bash
   cmake --build build_host -j && ctest --test-dir build_host
   cmake --build build_asan -j && ctest --test-dir build_asan
   ```
3. **Cross-Compilation & Layout Verification (E906)**:
   Verify the object compiles under `riscv-none-elf-gcc` and fits target SRAM windows:
   ```bash
   cmake --build build_e906 -j
   ```
