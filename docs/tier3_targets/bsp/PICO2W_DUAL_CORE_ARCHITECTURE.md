# Raspberry Pi Pico 2 W Dual-Core Architecture Specification

This document defines the **Asymmetric Multiprocessing (AMP)** architecture for the **Raspberry Pi Pico 2 W (RP2350 SoC)** in AbstractX and the INAV flight stack.

---

## 1. Core Allocation & Separation of Concerns

The RP2350 features **dual ARM Cortex-M33 cores** running at 150 MHz paired with an onboard **CYW43439** Wi-Fi 4 (802.11n) and Bluetooth 5.2 radio. To achieve hard real-time flight determinism while supporting wireless telemetry and high-speed sensors, the cores are strictly partitioned:

```mermaid
graph TD
    subgraph Core0["Core 0: I/O, DMA & Wireless Network Master"]
        Core0_Net["CYW43439 Wi-Fi / Bluetooth Stack<br/>(pico_cyw43_arch + lwIP)"]
        Core0_UDP["UDP / TCP / MAVLink Telemetry Server"]
        Core0_DMA["Hardware DMA Controllers<br/>(SPI0 8 kHz IMU + UART0 GPS)"]
        Core0_Bridge["Inter-Core SIO Mailbox & Doorbell Handler"]
    end

    subgraph Core1["Core 1: Dedicated Real-Time Coroutine Flight Engine"]
        Core1_Sched["Domain Bridge Coroutine Work Queue<br/>(IsrSafeCoroutineQueue)"]
        Core1_IMU["8 kHz IMU Coroutine Task<br/>co_await imu.next_sample_async()"]
        Core1_GPS["GPS Navigation Coroutine Task<br/>co_await gps.next_fix_async()"]
        Core1_Flight["Attitude Estimation, EKF & PID Flight Control"]
    end

    subgraph InterCore["RP2350 Inter-Core Transport"]
        Ring_Sensor["Core 0 -> Core 1 Sensor SPSC TLP Ring<br/>(SpscTlpRing<64>)"]
        Ring_Actuator["Core 1 -> Core 0 Telemetry/Actuator Ring<br/>(SpscTlpRing<64>)"]
        SIO_Doorbell["RP2350 Hardware SIO FIFOs & Doorbell IRQs"]
    end

    subgraph Peripherals["Physical Hardware & Sensors"]
        CYW43["CYW43439 Wi-Fi Radio (SPI)"]
        IMU_HW["ICM-42688-P 6-Axis IMU (SPI0)"]
        GPS_HW["U-Blox UBX GPS (UART0)"]
        WiFi_Client["GCS / Mobile Phone / Telemetry Receiver"]
    end

    %% Peripherals to Core 0
    CYW43 <--> Core0_Net
    IMU_HW -->|8 kHz SPI DMA| Core0_DMA
    GPS_HW -->|Streaming UART DMA| Core0_DMA
    Core0_Net <-->|Wireless Packets| WiFi_Client

    %% Core 0 to Inter-Core
    Core0_DMA -->|Push 64B TLPs| Ring_Sensor
    Ring_Actuator -->|Pop Telemetry TLPs| Core0_UDP
    Core0_Bridge <--> SIO_Doorbell

    %% Inter-Core to Core 1
    Ring_Sensor -->|Pop Sensor TLPs| Core1_Sched
    Core1_Flight -->|Push Telemetry TLPs| Ring_Actuator
    SIO_Doorbell <--> Core1_Sched

    %% Core 1 internal
    Core1_Sched --> Core1_IMU
    Core1_Sched --> Core1_GPS
    Core1_IMU --> Core1_Flight
    Core1_GPS --> Core1_Flight

    classDef c0 fill:#0f172a,stroke:#3b82f6,stroke-width:2px,color:#ffffff;
    classDef c1 fill:#1e1b4b,stroke:#818cf8,stroke-width:2px,color:#ffffff;
    classDef ipc fill:#14532d,stroke:#22c55e,stroke-width:2px,color:#ffffff;
    classDef hw fill:#7c2d12,stroke:#f97316,stroke-width:2px,color:#ffffff;

    class Core0_Net,Core0_UDP,Core0_DMA,Core0_Bridge c0;
    class Core1_Sched,Core1_IMU,Core1_GPS,Core1_Flight c1;
    class Ring_Sensor,Ring_Actuator,SIO_Doorbell ipc;
    class CYW43,IMU_HW,GPS_HW,WiFi_Client hw;
```

---

## 2. Detailed Responsibility Matrix

| Feature / Responsibility | **Core 0 (I/O & Network Master)** | **Core 1 (Coroutine Flight Engine)** |
| :--- | :--- | :--- |
| **Primary Role** | Hardware I/O, DMA, Wi-Fi networking, GCS telemetry | Real-time flight control, sensor fusion, coroutines |
| **Wi-Fi / Network Stack** | **Runs `pico_cyw43_arch`**, lwIP, DHCP, UDP broadcasts | **Zero network overhead** (100% isolated from Wi-Fi jitter) |
| **DMA & Peripheral ISRs** | Manages SPI0 DMA, UART0 DMA, PIO Dual-SPI, Timer Alarms | Consumes parsed events via lock-free SPSC rings |
| **Execution Paradigm** | Event-driven polling / ISR completion pipeline | C++20 Cooperative Coroutine Work Queue (`Dispatcher`) |
| **Flight Control & EKF** | None | **Runs 8 kHz PID loop, AHRS, Navigation state machine** |
| **Idle Behavior** | Polls network queues & services DMA interrupts | Sleeps in `__asm__ volatile("wfe")` when work queue is empty |

---

## 3. Inter-Core Data Flow (SIO + SPSC Ring)

### A. Sensor Ingestion Path (Core 0 $\rightarrow$ Core 1)
1. **SPI DMA** on Core 0 reads 15-byte burst from ICM-42688-P upon `DRDY` interrupt.
2. Core 0 formats a 64-byte completion packet (`Tlp64::make_cpl_d(0x01, payload)`) and pushes it into `g_sensor_ring`.
3. Core 0 writes a token to the RP2350 hardware SIO FIFO (`sio_hw->fifo_wr = 0x01`), triggering an SIO IRQ on Core 1.
4. Core 1 awakens, pops the TLP from `g_sensor_ring`, and resumes the waiting `imu_task` coroutine.

### B. Telemetry Broadcast Path (Core 1 $\rightarrow$ Core 0 $\rightarrow$ Wi-Fi)
1. Core 1 flight coroutines produce telemetry packets (attitude quaternion, GPS position, battery voltage).
2. Packets are pushed to `g_telemetry_ring`.
3. Core 0 pops packets during its network loop and broadcasts them via UDP / WebSocket to the ground station.

---

## 4. Why This Eliminates Flight Control Jitter
- Wireless stacks (Wi-Fi 802.11 beacons, ARP, DHCP, retransmissions) introduce non-deterministic latency spikes ranging from 2 ms to 50 ms.
- By pinning the wireless stack and DMA bus mastering entirely to **Core 0**, **Core 1** achieves **sub-microsecond jitter determinism** for flight attitude stabilization.
