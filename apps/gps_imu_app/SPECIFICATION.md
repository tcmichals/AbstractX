# GPS & IMU Application Specification

This document is the **authoritative design specification** for the **GPS & IMU Application (`apps/gps_imu_app/`)**. It details the dual-domain architecture, the I/O processing loop peripheral setup, the 8 kHz IMU auto-sample pipeline, the C++20 coroutine state machines, and the 64-byte TLP wire contract.

---

## 1. Application Overview & Objectives

The `gps_imu_app` is the flagship reference application for AbstractX. It coordinates high-rate inertial sensing and precision satellite navigation:
1. **ICM-42688-P 6-Axis IMU**: 8,000 Hz accelerometer and gyroscope burst telemetry with sub-microsecond hardware timestamping.
2. **U-Blox UBX GPS Receiver**: 10 Hz / 25 Hz 3D position, velocity, and time-of-week navigation stream.
3. **100% Portability**: The same application code runs unmodified across **Pico 2 W (RP2350)**, **ESP32-P4**, **Linux (Allwinner Cubie A5E)**, and **Desktop SITL**.

```mermaid
graph TD
    subgraph CORO_DOMAIN["1. Coroutine Domain (coro_app.cpp)"]
        IMU_TASK["<b>imu_task()</b><br/>co_await imu.next_sample_async()<br/>Runs AHRS & Rate PID at 8 kHz"]
        GPS_TASK["<b>gps_task()</b><br/>co_await gps.next_fix_async()<br/>Updates EKF position at 10-25 Hz"]
        HEARTBEAT["<b>heartbeat_task()</b><br/>co_await timer.sleep_ms_async(1000)"]
    end

    subgraph RINGS["2. Lock-Free SPSC TLP Rings (Shared Memory)"]
        SensorRing["<b>g_sensor_ring</b><br/>IO Proc -> Coroutines (64B DMA_Stream TLPs)"]
        TelemRing["<b>g_telemetry_ring</b><br/>Coroutines -> IO Proc (Outbound Telemetry)"]
    end

    subgraph IOPROC_DOMAIN["3. App I/O Processing Loop (io_processor.cpp)"]
        PIN3_IRQ["<b>Pin 3 (DRDY) GPIO Interrupt</b><br/>Latches nanosecond timestamp"]
        SPI1_DMA["<b>SPI1 DMA Burst Engine</b><br/>Autonomously clocks 15B from ICM-42688-P"]
        UART_GPS["<b>UART Streaming Parser</b><br/>UBX-NAV-PVT zero-allocation parser"]
        TLP_GEN["<b>64B TLP Packetizer</b><br/>Encapsulates CTF 1.8 binary payloads"]
        NET_EGRESS["<b>Telemetry Egress</b><br/>CYW43 Wi-Fi / Linux UDP socket (:9870)"]
    end

    subgraph HAL["4. Abstract HAL Interfaces"]
        SPI_IF["ISpi (SPI1)"]
        GPIO_IF["IGpio (Pin 3)"]
        UART_IF["IUart (GPS UART)"]
        TIMER_IF["ITimer (Timestamp)"]
    end

    IMU_TASK <-->|Pops Sensor TLPs| SensorRing
    GPS_TASK <-->|Pops Sensor TLPs| SensorRing
    IMU_TASK -->|Pushes Telemetry TLPs| TelemRing

    PIN3_IRQ --> SPI1_DMA
    SPI1_DMA --> TLP_GEN
    UART_GPS --> TLP_GEN
    TLP_GEN -->|Pushes Sensor TLPs| SensorRing
    TelemRing -->|Pops Outbound TLPs| NET_EGRESS

    IOPROC_DOMAIN -->|Drives| HAL

    classDef coroStyle fill:#1e3a8a,stroke:#3b82f6,stroke-width:2px,color:#ffffff;
    classDef ringStyle fill:#14532d,stroke:#22c55e,stroke-width:2px,color:#ffffff;
    classDef ioStyle fill:#4c1d95,stroke:#8b5cf6,stroke-width:2px,color:#ffffff;
    classDef halStyle fill:#0f172a,stroke:#475569,stroke-width:1px,color:#e2e8f0;

    class IMU_TASK,GPS_TASK,HEARTBEAT coroStyle;
    class SensorRing,TelemRing ringStyle;
    class PIN3_IRQ,SPI1_DMA,UART_GPS,TLP_GEN,NET_EGRESS ioStyle;
    class SPI_IF,GPIO_IF,UART_IF,TIMER_IF halStyle;
```

---

## 2. App I/O Processing Loop (`src/io_processor.cpp`)

The application I/O processing loop manages physical peripheral sequencing using **only generic HAL interfaces**:

### 2.1 Hardware Configuration Sequence
1. **SPI1 Setup**:
   - Calls `hal_spi.init(SpiConfig{ .frequency_hz = 12'000'000, .mode = SpiMode::Mode3, .use_dma = true })`.
   - Initializes ICM-42688-P registers (`PWR_MGMT0 = 0x0F` full gyro/accel power, `GYRO_CONFIG0 = 0x06` ±2000 dps @ 8 kHz, `ACCEL_CONFIG0 = 0x06` ±16g @ 8 kHz, `INT_CONFIG = 0x03` push-pull active-high).
2. **Pin 3 Interrupt Setup (Non-TLP Generic API)**:
   - Calls `hal_gpio.configure_pin(PIN_IMU_DRDY, PinMode::Input, PinPull::PullDown)`.
   - Calls `hal_gpio.configure_interrupt(PIN_IMU_DRDY, EdgeTrigger::Rising, &on_imu_drdy_interrupt, this)`.
   - Calls `hal_gpio.enable_interrupt(PIN_IMU_DRDY, true)`.
3. **GPS UART Setup**:
   - Calls `hal_uart.init(115200)` (or 921600 baud).

### 2.2 8 kHz Auto-Sample Pipeline
```mermaid
sequenceDiagram
    participant HW as ICM-42688-P
    participant IRQ as Pin 3 (DRDY) ISR
    participant DMA as SPI1 DMA Engine
    participant RING as g_sensor_ring
    participant CORO as Coroutine (Core 1)

    HW->>IRQ: Assert DRDY line (Pin 3 rising edge)
    activate IRQ
    IRQ->>IRQ: Latch timestamp_ns = timer.get_time_ns()
    IRQ->>DMA: Trigger 15-byte SPI DMA Burst (0x1D..0x2B)
    deactivate IRQ
    
    activate DMA
    DMA->>HW: Transfer 15 bytes @ 12-24 MHz
    HW-->>DMA: Return Temp[2], Accel[6], Gyro[6]
    DMA->>DMA: Form 64-Byte DMA_Stream TLP
    DMA->>RING: push_from_isr(tlp)
    DMA->>CORO: Signal Doorbell (SIO FIFO / IPC)
    deactivate DMA

    activate CORO
    CORO->>RING: pop(tlp)
    CORO->>CORO: imu_task resumes in 2-5 ns!
    deactivate CORO
```

---

## 3. C++20 Coroutine Application Engine (`src/coro_app.cpp`)

The Coroutine Domain runs the flight and navigation algorithms. It is completely isolated from physical interrupts and bus stalls:

```cpp
// 1. High-Rate 8 kHz IMU Control Task
Task<void> imu_task(Icm42688p& imu) {
    while (true) {
        // Suspends task until the next 64B DMA_Stream TLP arrives
        ImuSample sample = co_await imu.next_sample_async();
        if (sample.valid) {
            run_attitude_filter(sample.gyro, sample.accel, sample.timestamp_ns);
            run_rate_pid();
            
            // Format telemetry TLP and queue for outbound transmission
            Tlp64 telem = Icm42688p::to_tlp(sample);
            g_telemetry_ring.push(telem);
        }
    }
}

// 2. Navigation GPS Task
Task<void> gps_task(UbloxGps& gps) {
    while (true) {
        // Suspends task until verified 3D UBX-NAV-PVT fix arrives
        GpsFix fix = co_await gps.next_fix_async();
        if (fix.valid) {
            update_ekf_navigation(fix.lat, fix.lon, fix.alt_mm, fix.speed_mm_s);
            
            Tlp64 telem = UbloxGps::to_tlp(fix);
            g_telemetry_ring.push(telem);
        }
    }
}

// 3. Heartbeat & Diagnostics Task
Task<void> heartbeat_task(hal::ITimer& timer) {
    uint32_t count = 0;
    while (true) {
        co_await timer.sleep_ms_async(1000);
        count++;
        log_diagnostics(count, g_sensor_ring.size(), g_telemetry_ring.size());
    }
}
```

---

## 4. TLP Wire Format & Message Classification (TX, RX, IOCTL)

The `gps_imu_app` uses 64-byte `Tlp64` messages across the lock-free SPSC rings categorized into three message types:
1. **RX Messages (Sensor Telemetry Stream)**: `DMA_Stream` (`Type = 0x10`) packets emitted by `io_processor.cpp` on `Channel::Telemetry` carrying 8 kHz IMU samples and GPS fixes.
2. **TX Messages (Outbound Telemetry Stream)**: Telemetry TLPs produced by `coro_app.cpp` pushed into `g_telemetry_ring` for external network/serial transmission.
3. **IOCTL Messages (Runtime Reconfiguration)**: `MemWr` and `MemRd` packets for dynamic sensor scaling, rate changes, or calibration queries, correlated via `Tag`.

| Field | Value | Meaning |
| :--- | :--- | :--- |
| **`type`** | `0x10` (`DMA_Stream`) | Autonomous sensor stream packet |
| **`channel`** | `0x02` (`Channel::Telemetry`) | Telemetry plane |
| **`tag`** | `0x01` (IMU) / `0x02` (GPS) | Split-transaction correlation tag |
| **`timestamp_ns`** | 64-bit nanosecond timer | Cycle-accurate hardware timestamp latched at Pin 3 edge |
| **`payload[40]`** | CTF 1.8 binary payload | Packed `ImuSamplePayload` (23B) or `GpsFixPayload` (35B) |
| **`crc32`** | IEEE 802.3 CRC32 | Hardware/software link integrity check |

---

## 5. Supported Target Platforms

| Platform | Location | Execution Strategy |
| :--- | :--- | :--- |
| **Raspberry Pi Pico 2 W** | `platforms/pico2w/` | Core 0 runs `io_processor` + Wi-Fi; Core 1 runs `coro_app`. |
| **Espressif ESP32-P4** | `platforms/esp32p4/` | Core 0 runs `io_processor` (GDMA); Core 1 runs `coro_app`. |
| **Desktop SITL** | `platforms/sitl/` | Main thread runs `coro_app`; worker threads simulate 8 kHz IMU. |
| **Allwinner Cubie A5E** | `platforms/linux/` | PREEMPT_RT thread runs `coro_app`; POSIX workers service `/dev/spidev1.0`. |
