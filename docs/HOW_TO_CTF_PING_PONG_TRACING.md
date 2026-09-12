# How-To: CTF 1.8 Ping-Pong Binary Tracing in AbstractX

## Overview & Mental Model

In high-rate embedded flight controllers and real-time robotic systems (such as an 8 kHz IMU sensor loop), **logging must never introduce timing jitter or blocking I/O**.

AbstractX provides a zero-allocation, high-throughput binary trace engine based on a **1 KB Ping-Pong Buffer Architecture**:
* **Buffer A (Active Fill Buffer)**: Fast producers (ISRs, coroutines, HAL drivers) write binary CTF 1.8 events into Buffer A with a direct memory copy and pointer increment ($< 50\text{ ns}$).
* **Buffer B (Transmit Buffer)**: When Buffer A fills to 1,024 bytes (or a periodic flush timer expires), the buffers **swap instantly**. Buffer B is transmitted asynchronously to the configured trace sink (UDP port 9870, local `.ctf` file, or coprocessor shared SRAM ring) by the cooperative `trace_dispatcher_task()` coroutine.

```
       Producer Domain (8 kHz IMU, GPS, HAL ISRs)
                           │
                           ▼
               ┌───────────────────────┐
               │ Buffer A [1024 Bytes] │ <── Active FILLING
               └───────────────────────┘
                           │
                 [Buffer Full / 10ms Swap]
                           │
                           ▼
               ┌───────────────────────┐
               │ Buffer B [1024 Bytes] │ ──► Active TRANSMITTING
               └───────────────────────┘
                           │
                           ▼
          trace_dispatcher_task() Coroutine
      ┌────────────────────┼────────────────────┐
      ▼                    ▼                    ▼
UdpTraceSink        FileTraceSink       SharedSramTraceSink
(:9870 Live)        ("trace.ctf")       (E907 -> Linux MSGBox)
```

---

## Why 1 KB is the Optimal Buffer Sizing

1. **Network Frame MTU Match (No IP Fragmentation)**:
   * Standard Ethernet and Wi-Fi MTU is **1,500 bytes**.
   * A 1,024-byte packet + 32-byte CTF packet header + 28-byte IP/UDP headers = **1,084 bytes**.
   * The packet fits entirely within a single unfragmented datagram, avoiding packet drops and IP reassembly on Linux and microcontroller network stacks (e.g. CYW43439 on Pico 2 W).
2. **Batches ~35 to 45 Events per Packet**:
   * IMU samples are 27 bytes, GPS fixes are 35 bytes, Coroutine events are 19 bytes.
   * A 1 KB packet batches dozens of high-rate events, cutting socket syscalls and hardware doorbell interrupts by **>80%**.
3. **Low SRAM Footprint**:
   * Two 1 KB buffers require only **2,048 bytes (2 KB)** of RAM, making it feasible on bare-metal RISC-V coprocessors (such as the Allwinner XuanTie E907 on the Cubie A5E) and RP2350 Core 0.

---

## 1. Selecting a Buffer Profile (`BufferProfile`)

AbstractX defines an `enum class BufferProfile` allowing applications to select the buffer topology that matches their silicon constraints:

| `BufferProfile` Enum | Packet Size | Buffers | Total RAM | Recommended Use Case |
| :--- | :---: | :---: | :---: | :--- |
| `BufferProfile::PingPong_1K_x2` | 1,024 B | 2 | 2 KB | **Default**: High-rate IMU/GPS flight loops over UDP & shared SRAM |
| `BufferProfile::Ring_1K_x4` | 1,024 B | 4 | 4 KB | High burst buffering (e.g. WiFi retries or bursts) |
| `BufferProfile::Compact_512B_x2` | 512 B | 2 | 1 KB | Ultra memory-constrained RISC-V / Cortex-M0+ microcontrollers |
| `BufferProfile::Large_2K_x4` | 2,048 B | 4 | 8 KB | Host Linux SITL simulation and massive multi-channel logging |

---

## 2. Configuring Startup Tracing via `abstractx::init()`

To configure the trace engine at boot, populate `TraceConfig` in your master `Config`:

```cpp
#include "abstractx/abstractx.hpp"

int main() {
    abstractx::Config config{};

    // 1. Configure Trace Destination
    config.trace.sink_type   = abstractx::TraceSinkType::Udp; // Or TraceSinkType::File
    config.trace.sink_target = "127.0.0.1:9870";              // Target IP/Port or filename
    config.trace.buffer_profile = abstractx::trace::BufferProfile::PingPong_1K_x2;
    config.trace.flush_period_ms = 10;                         // 10 ms periodic flush timer

    // 2. Initialize AbstractX Master Runtime
    abstractx::init(config);

    // 3. Launch Application Coroutine
    abstractx::run(app_main());
    return 0;
}
```

---

## 3. Emitting Binary CTF Events

In your real-time sensor coroutines or drivers, call `trace::g_tracer` methods directly. No string formatting or serialization takes place:

```cpp
// 1. High-Rate IMU Telemetry (8 kHz)
trace::g_tracer.trace_imu(
    sample.seq,
    accel_x_mg, accel_y_mg, accel_z_mg,
    gyro_x_dps, gyro_y_dps, gyro_z_dps,
    temp_c_1e2,
    timestamp_us
);

// 2. GPS Navigation Fix (10 Hz)
trace::g_tracer.trace_gps(
    fix.itow_ms,
    fix.lat_1e7, fix.lon_1e7, fix.alt_mm,
    fix.ground_speed_mm_s, fix.heading_1e5,
    fix.satellites, fix.fix_type,
    timestamp_us
);

// 3. Coroutine Lifecycle State Transition
trace::g_tracer.trace_coro(
    task_id,
    handle_address,
    trace::CoroState::Resume,
    reason,
    timestamp_us
);

// 4. Hardware Driver I/O Event
trace::g_tracer.trace_hal(
    peripheral_id,  // 1: UART, 2: SPI, 3: I2C, 4: GPIO
    req_size, bytes_transferred,
    duration_us, status,
    timestamp_us
);
```

---

## 4. How the Dispatcher Coroutine Transmits Non-Blockingly

When you call `abstractx::run()`, the runtime automatically schedules `trace_dispatcher_task()`:

```cpp
coro::Task<void> trace_dispatcher_task(uint16_t flush_period_ms) {
    auto& timer = hal::get_timer_driver();
    uint64_t next_flush_us = timer.get_time_us() + (flush_period_ms * 1000ULL);

    while (true) {
        uint64_t now_us = timer.get_time_us();
        if (now_us >= next_flush_us) {
            // Flushes active buffer if non-empty, triggering ping-pong swap
            g_tracer.flush(now_us);
            next_flush_us = now_us + (flush_period_ms * 1000ULL);
        }
        co_await yield_to_dispatcher();
    }
}
```

* If the active buffer reaches 1,024 bytes before the 10 ms timer expires, it triggers an immediate swap.
* If low event activity occurs, the 10 ms timer flushes whatever partial events are present so live visualizers maintain fluid display.

---

## 5. Heterogeneous XuanTie E907 Co-Processor to Linux

When deploying on the Allwinner XuanTie E907 (Cubie A5E / A7A):
1. The E907 initializes `g_tracer` with `BufferProfile::PingPong_1K_x2` mapped to shared SRAM (`0x40000000`).
2. When Buffer A swaps, the E907 rings the `sun6i-msgbox` hardware doorbell interrupt.
3. On Linux Cortex-A55, the `linux_trace_receiver_task` coroutine awakens from the doorbell `eventfd`, reads the completed 1 KB buffer, and streams it out via UDP port 9870 to the Visualizer Studio.

---

## 6. Visualizing and Decoding the Stream

1. **Live Oscilloscope (Visualizer Studio)**:
   ```bash
   python3 tools/visualizer/abstractx_studio.py --port 9870
   ```
   Renders real-time 8 kHz IMU graphs, GPS 3D flight paths, and coroutine Gantt bars at 60 FPS.

2. **Offline Analysis with Babeltrace 2**:
   If logging to `trace.ctf`:
   ```bash
   babeltrace2 ./trace/
   ```
   Outputs fully decoded, microsecond-accurate event streams conforming to `trace/barectf_config.yaml`.
