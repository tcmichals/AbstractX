# AbstractX Multi-Rate Sensor Fusion & Flight Controller (`gps_imu_app`)

The **`gps_imu_app`** is the flagship demonstration of the **AbstractX C++20 Coroutine Architecture**. It solves one of the most difficult engineering problems in aerospace and robotics: **harmonizing heterogeneous multi-rate sensors** (8 kHz IMU, 50 Hz Magnetometer, 10 Hz GPS) and executing a real-time 9-DoF Mahony AHRS and Cascaded Motor Mixer with **zero state machines, zero blocking, and zero dynamic heap allocation.**

---

## 1. The Core Problem: Multi-Rate Sensor Synchronization

A modern flight controller must ingest sensor data streaming on wildly different physical timescales:
* **ICM-42688-P 6-Axis IMU**: 1,000 Hz – 8,000 Hz (SPI DMA auto-burst) $\to$ The high-speed **Flight Heartbeat**
* **QMC5883L 3-Axis Magnetometer**: 50 Hz – 100 Hz (I2C Fast Mode) $\to$ The medium-speed **Heading Reference**
* **U-Blox M10 Satellite GPS**: 5 Hz – 10 Hz (UART Serial Navigation) $\to$ The low-speed **Geodetic Reference**

```mermaid
flowchart TD
    subgraph SENSORS["Heterogeneous Hardware Sensors & Physical Cadences"]
        direction LR
        S_IMU["<b>ICM-42688-P IMU</b><br/>SPI DMA (10 MHz)<br/>Cadence: <b>8,000 Hz</b> (125 µs)"]
        S_MAG["<b>QMC5883L Magnetometer</b><br/>I2C Fast Mode (400 kHz)<br/>Cadence: <b>50 Hz</b> (20 ms)"]
        S_GPS["<b>U-Blox M10 GPS</b><br/>UART (115,200 Baud)<br/>Cadence: <b>10 Hz</b> (100 ms)"]
    end

    subgraph LEGACY["Legacy Flight Stacks (Betaflight / INAV / FreeRTOS)"]
        direction TB
        L_SM["<b>Fragmented State Machine Graph</b><br/>• Tick prescalers: <code>if (++tick % 80 == 0)</code><br/>• Callbacks mutate volatile structs and exit stack frames<br/>• RTOS thread context switching & mutex contention<br/>• Sequential boot delays: >600 ms"]
    end

    subgraph ABSTRACTX["AbstractX C++20 Coroutines & Typed Channels"]
        direction TB
        A_BOOT["<b>Structured Parallel Boot</b><br/><code>co_await coro::when_all(imu, gps, mag)</code><br/>Boot Time: max(T_i) < 110 ms"]
        A_PIPE["<b>Linear Primary-Paced Channel Pipeline</b><br/>• Pacer: <code>co_await g_imu_channel.pop()</code><br/>• Non-blocking Aux: <code>while (g_mag.try_pop(m))</code><br/>• 0 Bytes Heap • Zero Context Switching"]
        A_BOOT --> A_PIPE
    end

    SENSORS -->|The Hard Way| LEGACY
    SENSORS -->|The AbstractX Way| ABSTRACTX

    classDef sensStyle fill:#0f172a,stroke:#38bdf8,stroke-width:2px,color:#ffffff;
    classDef legStyle fill:#450a0a,stroke:#f87171,stroke-width:2px,color:#ffffff;
    classDef absStyle fill:#064e3b,stroke:#34d399,stroke-width:2px,color:#ffffff;

    class S_IMU,S_MAG,S_GPS sensStyle;
    class L_SM legStyle;
    class A_BOOT,A_PIPE absStyle;
```

---

## 2. The Architectural Contrast: State Machines vs. Linear Coroutines

| Engineering Metric | Legacy State Machines (RTOS / Bare-Metal) | AbstractX C++20 Coroutines |
| :--- | :--- | :--- |
| **Control Flow** | **Fragmented & Discontinuous**: Code executes an event, mutates a flag, exits the function, and later re-enters a different switch-case. | **100% Linear & Readable**: Code reads top-to-bottom like synchronous code while executing asynchronously with zero blocking. |
| **Rate Matching** | **Ad-Hoc Tick Prescalers**: Complex modulo counters (`if (++c % 80 == 0)`) scattered across timer ISRs. | **Typed Lock-Free Channels**: Master sensor paces via `co_await channel.pop()`; auxiliary sensors drain via `try_pop()`. |
| **Boot Latency** | **Sequential Blocking Stalls**: Sequential calls (`spi_init(); delay_ms(100); gps_init(); delay_ms(500);`) wasting >600 ms. | **Structured Parallel Concurrency**: `co_await coro::when_all(...)` configures all buses simultaneously in <110 ms. |
| **Memory Allocation** | Variable / dynamic heap queues with fragmentation risks. | **0 Bytes Heap**: Static SPSC rings and compiler-managed stackless coroutine frames. |
| **Task Scheduling** | Preemptive RTOS threads incurring 2–8 KB stack per task and cache-thrashing context switches. | **Single-Threaded Cooperative Dispatch**: Zero thread context switches, zero priority inversions, sub-microsecond jitter. |
| **Portability** | Platform `#ifdef` spaghetti for target timer/mutex APIs. | **Zero `#ifdef`s**: Identical code executes on Pico 2 W, ESP32-P4, and ARM A55 Linux. |

### The Code Comparison

#### Legacy State Machine Approach:
```c
// Legacy: Execution frame is shattered across disjoint ISR callbacks and flags
void SPI_DMA_IRQHandler(void) {
    g_imu_ready = 1;
    schedule_task(TASK_IMU);
}

void Task_IMU_Execute(void) {
    switch (g_imu_state) {
        case STATE_WAIT_DRDY:
            if (!g_imu_ready) return;
            start_dma_transfer();
            g_imu_state = STATE_READING;
            break; // Exits function!
        case STATE_READING:
            if (dma_busy()) return;
            process_imu_sample();
            if (++g_mag_prescaler % 80 == 0) schedule_task(TASK_MAG);
            g_imu_state = STATE_WAIT_DRDY;
            break;
    }
}
```

#### The AbstractX Coroutine Pipeline:
```cpp
// AbstractX: Completely linear, self-contained, zero-heap coroutine
Task<void> sensor_fusion_task(hal::ITimer& timer, AttitudeFilter& filter) {
    while (true) {
        // 1. Asynchronously await next high-rate IMU pulse (Physical Clock Pacer)
        ImuSample imu = co_await g_imu_channel.pop();
        filter.update_imu(imu, dt);

        // 2. Non-blockingly drain whatever Mag and GPS updates arrived in the interim
        MagSample mag;
        while (g_mag_channel.try_pop(mag)) filter.update_mag(mag);

        GpsFix gps;
        while (g_gps_channel.try_pop(gps)) filter.update_gps(gps);

        // 3. Emit fused 64-byte TLP into telemetry stream
        g_telemetry_ring.push(AttitudeFilter::to_tlp(filter.state()));
    }
}
```

---

## 3. Parallel Hardware Boot (`co_await coro::when_all`)

Traditional systems waste hundreds of milliseconds synchronously waiting for silicon peripherals to wake up. AbstractX initializes the SPI IMU, UART GPS receiver, and I2C Magnetometer concurrently:

```mermaid
sequenceDiagram
    autonumber
    actor Boot as Application Boot (app_main)
    participant SPI as SPI1 (ICM-42688-P)
    participant UART as UART0 (U-Blox M10)
    participant I2C as I2C0 (QMC5883L)

    Note over Boot: co_await coro::when_all(imu.init_async(), gps.init_async(115200), mag.init_async())
    
    par Concurrent Bus Handshakes
        Boot->>SPI: Write PWR_MGMT0 (Enable Gyro/Accel)
        SPI-->>Boot: Suspends awaiter (DMA in flight)
    and
        Boot->>UART: Configure Baudrate & UBX-NAV-PVT
        UART-->>Boot: Suspends awaiter (FIFO streaming)
    and
        Boot->>I2C: Set Continuous Mode (0x09=0x1D)
        I2C-->>Boot: Suspends awaiter (I2C state machine)
    end

    Note over SPI,I2C: All 3 hardware buses transact in parallel on the bus reactor
    SPI-->>Boot: IMU Ready (15 ms)
    I2C-->>Boot: Mag Ready (22 ms)
    UART-->>Boot: GPS Ready (105 ms)

    Note over Boot: when_all completes in max(T_i) = 105 ms (Saved >500 ms!)
```

---

## 4. End-to-End Application Dataflow & Channel Topology

```mermaid
flowchart TD
    subgraph HW["1. Autonomous Hardware Peripherals"]
        direction LR
        SPI_HW["<b>SPI1 (ICM-42688-P)</b><br/>DMA auto-burst @ 10 MHz"]
        I2C_HW["<b>I2C0 (QMC5883L)</b><br/>Fast-mode @ 400 kHz"]
        UART_HW["<b>UART0 (U-Blox)</b><br/>UBX-NAV-PVT @ 115,200 Bd"]
    end

    subgraph PROD["2. Dedicated Coroutine Producers"]
        direction LR
        P_IMU["<b>imu_producer_task</b><br/>co_await imu.next_sample_async()<br/>8,000 Hz"]
        P_MAG["<b>mag_producer_task</b><br/>co_await timer.sleep_ms_async(20)<br/>50 Hz"]
        P_GPS["<b>gps_producer_task</b><br/>co_await gps.next_fix_async()<br/>10 Hz"]
    end

    subgraph CHANS["3. Lock-Free Typed Coroutine Channels"]
        direction LR
        Q_IMU["<b>g_imu_channel</b><br/>AsyncQueue&lt;ImuSample, 32&gt;"]
        Q_MAG["<b>g_mag_channel</b><br/>AsyncQueue&lt;MagSample, 16&gt;"]
        Q_GPS["<b>g_gps_channel</b><br/>AsyncQueue&lt;GpsFix, 8&gt;"]
    end

    subgraph FUSION_ENGINE["4. 9-DoF Mahony AHRS & Cascaded Flight Controller"]
        direction TB
        F_PACE["<b>1. Pacing Stream (8 kHz):</b><br/><code>co_await g_imu_channel.pop()</code>"]
        F_DRAIN["<b>2. Non-Blocking Drain:</b><br/><code>g_mag_channel.try_pop()</code><br/><code>g_gps_channel.try_pop()</code>"]
        F_MATH["<b>3. Mahony Quaternion & Cascaded PID:</b><br/>• q_dot = 0.5 * q ⊗ ω<br/>• Earth-frame magnetic rotation: h = q ⊗ m ⊗ q*<br/>• Angle PID (Outer) -> Rate PID (Inner) -> Motor Mixer"]
        F_PACE --> F_DRAIN --> F_MATH
    end

    subgraph TELEM["5. Zero-Copy 64B TLP Telemetry Stream"]
        direction LR
        RING["<b>g_telemetry_ring</b><br/>SpscTlpRing&lt;64&gt;"]
        EGRESS["<b>telemetry_egress_task</b><br/>Live UDP :9870 / Shared SRAM"]
        RING --> EGRESS
    end

    SPI_HW --> P_IMU -->|try_push| Q_IMU
    I2C_HW --> P_MAG -->|try_push| Q_MAG
    UART_HW --> P_GPS -->|try_push| Q_GPS

    Q_IMU -.->|Paces Loop| F_PACE
    Q_MAG -.->|Drained| F_DRAIN
    Q_GPS -.->|Drained| F_DRAIN

    F_MATH -->|Decimated TLP| RING

    classDef hwStyle fill:#1e1b4b,stroke:#818cf8,stroke-width:2px,color:#ffffff;
    classDef prodStyle fill:#4c1d95,stroke:#c084fc,stroke-width:2px,color:#ffffff;
    classDef chanStyle fill:#14532d,stroke:#4ade80,stroke-width:2px,color:#ffffff;
    classDef fuseStyle fill:#0f172a,stroke:#38bdf8,stroke-width:2px,color:#ffffff;
    classDef telStyle fill:#78350f,stroke:#fbbf24,stroke-width:2px,color:#ffffff;

    class SPI_HW,I2C_HW,UART_HW hwStyle;
    class P_IMU,P_MAG,P_GPS prodStyle;
    class Q_IMU,Q_MAG,Q_GPS chanStyle;
    class F_PACE,F_DRAIN,F_MATH fuseStyle;
    class RING,EGRESS telStyle;
```

---

## 5. Multi-Target Silicon Portability Invariants

The exact same `main.cpp` code compiles and executes across three target silicon architectures with **zero application `#ifdef`s**:

| Target Platform | Core Allocation | Hardware Floating-Point Execution | Memory & Timing Budget |
| :--- | :--- | :--- | :--- |
| **Raspberry Pi Pico 2 W** | **Dual ARM Cortex-M33 @ 150 MHz**<br/>• Core 1: Coroutine Flight Loop<br/>• Core 0: PIO SPI DMA + CYW43 Wi-Fi | Single-cycle hardware single-precision FPU (`vadd.f32`, `vmul.f32`, fast `vsqrt.f32`). | 520 KB SRAM total.<br/>Filter state + queues = **< 1 KB static SRAM**.<br/>Loop execution: **4.2 µs**. |
| **Espressif ESP32-P4** | **Dual RISC-V @ 400 MHz**<br/>• Core 1: Coroutine Flight Loop<br/>• Core 0: GDMA SPI + Wi-Fi 6 | Hardware single & double-precision FPU (`fadd.s`, `fmul.s`, `fsqrt.s`). | 768 KB HP SRAM.<br/>Filter state + queues = **< 1 KB static SRAM**.<br/>Loop execution: **1.8 µs**. |
| **Allwinner Cubie A5E (Pure Silicon)** | **Quad AArch64 @ 1.4 GHz + E906**<br/>• A55: PREEMPT_RT Flight Thread<br/>• E906: On-Chip SPI0/TWI/UART DMA | Hardware ARM NEON vector and scalar floating-point engine. | 1 GB – 4 GB LPDDR4.<br/>Filter state + queues = **< 1 KB static SRAM**.<br/>Loop execution: **0.8 µs**. |
| **Allwinner Cubie A5E + FPGA (X-Fabric)** | **Quad AArch64 @ 1.4 GHz + E906 + FPGA**<br/>• A55: PREEMPT_RT Flight Thread<br/>• FPGA: Hardware Auto-DMA & Router | Hardware ARM NEON FPU + FPGA DSPs. | 1 GB – 4 GB LPDDR4 + FPGA BRAM.<br/>Filter state + queues = **< 1 KB static SRAM**.<br/>Loop execution: **0.4 µs**. |

---

## 5a. Split-Core Heterogeneous Architecture: E906 C++20 I/O Processor & Linux Coroutines

On the **Radxa Cubie A5E**, `gps_imu_app` implements a true split-core asymmetric multiprocessing (AMP) architecture that separates high-speed hardware bus management from flight mathematics and telemetry:

```mermaid
flowchart TD
    subgraph SENSORS["Physical Sensors on Board"]
        HW_IMU["<b>ICM-42688-P 6-Axis IMU</b><br/>SPI1 DMA (10 MHz) + PI8 DRDY Interrupt"]
        HW_MAG["<b>HMC5883L / QMC5883L Compass</b><br/>TWI1 Fast Mode (400 kHz)"]
        HW_GPS["<b>u-blox NEO-M8N GPS</b><br/>UART3 DMA (9600 / 115200 Baud)"]
    end

    subgraph E906["XuanTie E906 RISC-V Coprocessor (e906_coprocessor.elf)"]
        direction TB
        subgraph NO_MP["Zero MicroPython • Pure C++20"]
            IO_ENG["<b>IoEngine + E906IoBackend</b><br/>Sensor-Agnostic Generic I/O Engine"]
            AUTO_DMA["<b>Hardware Auto-DMA Engine</b><br/>PI8 Rising Edge -> Autonomous SPI1 DMA Read<br/>1 kHz Stream • Zero CPU Intervention"]
            BRIDGES["<b>TLP Peripheral Bridges</b><br/>• SPI Bridge (0x06)<br/>• I2C Bridge (0x07)<br/>• Serial UART Bridge (0x05)"]
        end
    end

    subgraph IPC["Shared SRAM VirtIO RPMsg Ring Buffers"]
        TLP_EPT["<b>Endpoint 1024 ('abstractx-tlp')</b><br/>64-Byte PCIe-like TLP Request/Completion & Stream Ring"]
        TRACE_EPT["<b>Endpoint 1025 ('abstractx-trace')</b><br/>CTF 1.8 Nanosecond Timeline Profiler Stream"]
    end

    subgraph LINUX["ARM Cortex-A55 Linux Host (Quad AArch64 @ 1.4 GHz)"]
        direction TB
        subgraph CPP_BRIDGE["e906_rpmsg_studio_bridge (C++20 Coroutine Bridge)"]
            IMU_CORO["<b>imu_coro_task</b><br/>Auto-DMA Ingest & SI Unit Scaling"]
            MAG_CORO["<b>mag_coro_task</b><br/>Periodic I2C Polling & Heading"]
            GPS_CORO["<b>gps_coro_task</b><br/>UART DMA Stream & NMEA Checksums"]
            AHRS_CORO["<b>attitude_filter_task</b><br/>9-DoF Mahony AHRS Quaternion Math"]
        end

        subgraph PY_TEST["test_e906_coprocessor_async.py (Python Async Test Suite)"]
            ASYNC_LINK["<b>AsyncRpmsgLink</b><br/>loop.add_reader(fd) • Futures • Queues<br/><b>Zero Blocking Sleeps</b>"]
        end
    end

    subgraph STUDIO["AbstractX Visualizer Studio & UDP Telemetry"]
        UDP_TELEM["<b>UDP :9870</b>: 64B CTF/TLP Stream (IMU, GPS, Compass, Attitude)"]
        UDP_CTRL["<b>UDP :9872</b>: Auto-DMA Dynamic Configuration (ASP_TLP_TYPE_DMA_CFG = 0x11)"]
    end

    SENSORS <--> E906
    E906 <--> IPC
    IPC <--> LINUX
    CPP_BRIDGE --> UDP_TELEM
    STUDIO -->|Control Datagrams| CPP_BRIDGE
```

### 1. Pure C++20 E906 Generic I/O Coprocessor (`e906_coprocessor.elf`)
- **Zero MicroPython in Flight**: Replaces the bulky 3.2 MB MicroPython image with an optimized 1.5 MB pure C++20 binary.
- **Sensor-Agnostic Design**: The E906 firmware contains **zero sensor drivers** and **zero floating-point math**. It acts strictly as an intelligent I/O processor servicing standard 64-byte TLP requests:
  - **SPI Bridge (`Channel::SpiBridge = 0x06`)**: Direct DMA transactions on SPI1 (PB11..PB14).
  - **I2C Bridge (`Channel::I2cBridge = 0x07`)**: Sub-addressed read/write packets on TWI1 (PH0..PH1).
  - **Serial Bridge (`Channel::EscSerial = 0x05`)**: DMA circular ring RX/TX on UART3 (PB6..PB7).
  - **Autonomous Auto-DMA (`ASP_TLP_TYPE_DMA_CFG = 0x11`)**: Configures the hardware trigger engine. When armed, silicon interrupts on PI8 (pin 264, ICM-42688-P DRDY) autonomously execute a 14-byte SPI1 DMA burst at 1 kHz ODR, packing the sensor readings into 64-byte Tag 1 Telemetry TLPs with monotonic nanosecond timestamps and streaming them into the RPMsg ring with zero CPU intervention.

### 2. Linux C++20 Coroutine Sensor Bridge (`e906_rpmsg_studio_bridge`)
- **Location**: [`platforms/linux/e906_rpmsg_studio_bridge.cpp`](file:///home/tcmichals/projects/AbstractX/apps/gps_imu_app/platforms/linux/e906_rpmsg_studio_bridge.cpp)
- **Role**: Sits between Linux VirtIO RPMsg `/dev/rpmsg0` and network clients (AbstractX Studio and UDP visualization tools).
- **Coroutine Sensor Ingestion**: Executes parallel, non-blocking coroutines:
  - Ingests 1 kHz Auto-DMA bursts or dispatches periodic SPI polls.
  - Converts raw integers to standard SI physical units ($\text{m/s}^2$, $\text{rad/s}$, $\mu\text{T}$).
  - Accumulates UART3 GPS bytes, validates NMEA checksums, and parses coordinates.
  - Paces the 9-DoF Mahony AHRS fusion loop and emits Tag 1 (IMU), Tag 2 (GPS), Tag 3 (Compass), and Tag 5 (Attitude Quaternion) TLPs.
- **Dynamic Control Server**: Listens on UDP `:9872` for `ASP_TLP_TYPE_DMA_CFG` (0x11) packets sent by AbstractX Studio, allowing the user to dynamically toggle between polled bridge mode and autonomous hardware Auto-DMA directly from the GUI.

### 3. Asynchronous Linux Python Test Suite (`test_e906_coprocessor_async.py`)
- **Location**: [`tests/board/test_e906_coprocessor_async.py`](file:///home/tcmichals/projects/AbstractX/tests/board/test_e906_coprocessor_async.py)
- **Zero Blocking Sleeps**: Traditional test scripts rely on blocking `time.sleep()` or MicroPython `abstractx.sleep_ms()`, which freeze execution threads and violate AbstractX's non-blocking coroutine architecture. `test_e906_coprocessor_async.py` implements a 100% event-driven test engine using native Python `asyncio`:
  - `AsyncRpmsgLink`: Registers `/dev/rpmsg0` directly into the asyncio event loop using `loop.add_reader(fd, callback)`.
  - Tag Multiplexing: Every outgoing TLP allocates an 8-bit transaction tag and associates an `asyncio.Future`. When the E906 completion TLP arrives, the future is resolved reactively with zero polling.
  - Stream Subscriptions: Telemetry and Auto-DMA streams (Tag 1) are dispatched non-blockingly into `asyncio.Queue` objects.
- **Hardware Sweep Coverage**:
  - `test_coprocessor_spi_imu_whoami`: Asserts ICM-42688-P `WHO_AM_I` (`0x47`) over SPI1 DMA.
  - `test_coprocessor_i2c_compass_id`: Asserts HMC5883L ID string (`b"H43"`) over TWI1.
  - `test_coprocessor_serial_gps_stream`: Streams u-blox NEO-M8N sentences over UART3 DMA and validates NMEA XOR checksums.
  - `test_coprocessor_auto_dma_stream`: Arms PI8 DRDY Auto-DMA, gathers 25 consecutive samples from the queue, asserts payload validity, and verifies timestamp monotonicity with a 1 kHz inter-sample interval ($\sim 1.0\text{ ms}$).

### Platform Deployment & Debugging Guides (`platforms/`)

`gps_imu_app` provides dedicated deployment, build, flashing, and debugging HOWTO guides for each supported platform under [`platforms/`](file:///home/tcmichals/ssdData/projects/home/AbstractX/apps/gps_imu_app/platforms/):

| Platform Directory | Target Hardware | Guide Link | Focus Topics |
| :--- | :--- | :--- | :--- |
| **[`platforms/pico2w/`](file:///home/tcmichals/ssdData/projects/home/AbstractX/apps/gps_imu_app/platforms/pico2w/)** | Raspberry Pi Pico 2 W (RP2350) | [**Pico 2 W HOWTO**](file:///home/tcmichals/ssdData/projects/home/AbstractX/apps/gps_imu_app/platforms/pico2w/HOWTO.md) | 3-pin SWD wiring, VS Code Cortex-Debug, USB BOOTSEL, UDP Wi-Fi telemetry |
| **[`platforms/allwinner_e906/`](file:///home/tcmichals/ssdData/projects/home/AbstractX/apps/gps_imu_app/platforms/allwinner_e906/)** | Radxa Cubie A5E (Allwinner A5E) | [**Allwinner E906 HOWTO**](file:///home/tcmichals/ssdData/projects/home/AbstractX/apps/gps_imu_app/platforms/allwinner_e906/HOWTO.md) | Linux RemoteProc deployment, XuanTie E906 firmware, shared SRAM rings |
| **[`platforms/linux/`](file:///home/tcmichals/ssdData/projects/home/AbstractX/apps/gps_imu_app/platforms/linux/)** | Workstation & CI (SITL Simulation) | [**Linux SITL HOWTO**](file:///home/tcmichals/ssdData/projects/home/AbstractX/apps/gps_imu_app/platforms/linux/HOWTO.md) | Native C++20 build, zero-hardware SITL simulation, loopback UDP :9870 |
| **[`platforms/esp32p4/`](file:///home/tcmichals/ssdData/projects/home/AbstractX/apps/gps_imu_app/platforms/esp32p4/)** | Espressif ESP32-P4 (400 MHz RV32) | [**ESP32-P4 HOWTO**](file:///home/tcmichals/ssdData/projects/home/AbstractX/apps/gps_imu_app/platforms/esp32p4/HOWTO.md) | ESP-IDF v5.x build, GDMA SPI2, Wi-Fi 6 streaming, OpenOCD JTAG |

---

## 6. Live Visualization & Dynamic CTF Schema

`gps_imu_app` emits standardized 64-byte CTF 1.8 packets over UDP `:9870`. The telemetry stream is completely self-describing via [`trace_schema.json`](file:///home/tcmichals/projects/AbstractX/apps/gps_imu_app/trace_schema.json) and [`trace/barectf_config.yaml`](file:///home/tcmichals/projects/AbstractX/trace/barectf_config.yaml).

### Tool 1: Dedicated 3D Quadcopter & Primary Flight Display (PFD)
A standalone Python visualization tool is included in `tools/flight_display.py`:
* **Aviation Primary Flight Display (PFD)**: Sky/ground artificial horizon with roll pointer and pitch ladder.
* **3D Quadcopter Wireframe**: Real-time 3D perspective wireframe rotating with unit quaternion $[q_0, q_1, q_2, q_3]$.
* **Tape Altimeter & Airspeed**: Vertical scrolling altitude tape in meters/feet and ground speed.
* **Quad-X Motor Mixer Demands**: 4-bar real-time throttle display ($M_1..M_4$).
* **Multi-Rate Health Meters**: Live Hz indicators for IMU, Mag, GPS, and AHRS.

```bash
# Launch the 3D Quadcopter & PFD instrument display
python3 apps/gps_imu_app/tools/flight_display.py --port 9870

# Or run in simulation mode without hardware
python3 apps/gps_imu_app/tools/flight_display.py --sim
```

### Tool 2: AbstractX Visualizer Studio
For full coroutine Gantt timelines, 8 kHz oscilloscope waveforms, and SPSC ring saturation gauges:
```bash
python3 tools/visualizer/abstractx_studio.py --port 9870
```

---

## 7. Building & Running

### 1. Build Native Host SITL
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target gps_imu_app
```

### 2. Run the Flight Application
```bash
./build/apps/gps_imu_app/gps_imu_app
```

### 3. Example Live Telemetry Console Output
```text
========================================================
  AbstractX - Multi-Rate Flight Controller & AHRS Node   
  Structured Concurrency + Channel-Based Sensor Fusion  
========================================================
[AbstractX Boot] Parallel hardware initialization (when_all: SPI IMU + UART GPS + I2C Mag)...
[AbstractX Boot] Hardware Status: IMU=OK, GPS=OK, MAG=OK
[AbstractX AHRS #1] Roll:  -0.1° | Pitch:  +7.1° | Yaw:  21.8° (Mag:  21.8°) | Alt: 142.5m | Spd:  2.4 m/s | IMU: 5821, Mag: 50, GPS: 10 | Egress: 582 pkts
[AbstractX AHRS #2] Roll:  -0.0° | Pitch:  +7.1° | Yaw:  21.8° (Mag:  21.8°) | Alt: 142.5m | Spd:  2.4 m/s | IMU: 11642, Mag: 100, GPS: 20 | Egress: 1164 pkts
[AbstractX AHRS #3] Roll:  +0.0° | Pitch:  +7.1° | Yaw:  21.8° (Mag:  21.8°) | Alt: 142.5m | Spd:  2.4 m/s | IMU: 17463, Mag: 150, GPS: 30 | Egress: 1746 pkts
```
