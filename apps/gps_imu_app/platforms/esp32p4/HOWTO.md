# Espressif ESP32-P4 - `gps_imu_app` Deployment & Debugging Guide

This guide details how to build, flash, debug via JTAG, and visualize live flight telemetry from **`gps_imu_app`** on the **Espressif ESP32-P4** dual-core 400 MHz RISC-V SoC.

---

## 1. Platform Architecture: Dual 400 MHz RISC-V Cores

The ESP32-P4 features dual high-performance RISC-V cores running at 400 MHz with single/double-precision hardware FPUs:

```mermaid
graph TD
    subgraph Core0["Core 0: I/O & Networking Domain (400 MHz RISC-V)"]
        C0_Init["ESP-IDF BSP & GDMA Init"]
        C0_GDMA["SPI2 GDMA Engine (ICM-42688-P 8 kHz Auto-Burst)"]
        C0_I2C["I2C0 Hardware Master (QMC5883L Compass)"]
        C0_UART["UART1 Ring Buffer (U-Blox GPS)"]
        C0_Net["Wi-Fi 6 / 100M Ethernet LwIP Stack (:9870)"]
        C0_IPC["Hardware IPC Mailbox ISR"]
        C0_Init --> C0_GDMA
        C0_Init --> C0_I2C
        C0_Init --> C0_UART
        C0_Init --> C0_Net
        C0_Init --> C0_IPC
    end

    subgraph Core1["Core 1: Coroutine Flight Domain (400 MHz RISC-V)"]
        C1_Sched["C++20 Coroutine Dispatcher (abstractx::step())"]
        C1_IMU["imu_producer_task (Pacing Clock)"]
        C1_FUSION["sensor_fusion_task (Mahony AHRS & Mixer)"]
        C1_Sched --> C1_IMU
        C1_Sched --> C1_FUSION
    end

    subgraph SRAM["Internal L2 SRAM (768 KB)"]
        RingRx["g_sensor_ring (SpscTlpRing&lt;64&gt;)<br/>Core 0 -> Core 1 (Sensor Frames)"]
        RingTx["g_telemetry_ring (SpscTlpRing&lt;64&gt;)<br/>Core 1 -> Core 0 (Egress Frames)"]
    end

    C0_GDMA -->|Pushes 64B TLPs| RingRx
    RingRx -->|Pops TLPs @ 8 kHz| C1_IMU
    C1_FUSION -->|Pushes Telemetry TLPs| RingTx
    RingTx -->|Pops & Transmits UDP| C0_Net

    classDef c0 fill:#0f172a,stroke:#3b82f6,stroke-width:2px,color:#ffffff;
    classDef c1 fill:#1e1b4b,stroke:#818cf8,stroke-width:2px,color:#ffffff;
    classDef sram fill:#14532d,stroke:#22c55e,stroke-width:2px,color:#ffffff;

    class C0_Init,C0_GDMA,C0_I2C,C0_UART,C0_Net,C0_IPC c0;
    class C1_Sched,C1_IMU,C1_FUSION c1;
    class RingRx,RingTx sram;
```

---

## 2. Hardware Pinout & Wiring

| Peripheral | Signal | ESP32-P4 GPIO | Description |
| :--- | :--- | :--- | :--- |
| **SPI2 (IMU Bus)** | `SCK` | **GPIO 7** | Serial Clock to ICM-42688-P (up to 24 MHz) |
| | `MOSI` | **GPIO 8** | Master Out Slave In |
| | `MISO` | **GPIO 9** | Master In Slave Out |
| | `CS_N` | **GPIO 10** | Active-Low Hardware Chip Select |
| **IMU Interrupt** | `DRDY` | **GPIO 6** / **Pin 3** | Data-Ready Rising Edge Interrupt |
| **I2C0 (Compass)** | `SDA` | **GPIO 4** | 400 kHz Fast Mode Data |
| | `SCL` | **GPIO 5** | 400 kHz Fast Mode Clock |
| **UART1 (GPS)** | `TX` | **GPIO 11** | ESP TX to GPS RX |
| | `RX` | **GPIO 12** | ESP RX from GPS TX |
| **Console UART** | `TX` / `RX` | **USB-Serial-JTAG** | Built-in USB Type-C Port |

---

## 3. Toolchain Setup (ESP-IDF v5.2+)

Install and export ESP-IDF with RISC-V support:
```bash
# Source the ESP-IDF environment
. $HOME/esp/esp-idf/export.sh

# Verify RISC-V compiler
riscv32-esp-elf-gcc --version
```

---

## 4. How to Compile & Flash

### 4.1 Set Target to ESP32-P4
```bash
idf.py set-target esp32p4
```

### 4.2 Configure Wi-Fi Credentials
Configure the network SSID and password for live UDP telemetry:
```bash
idf.py menuconfig
# Navigate to: Component config -> AbstractX Platform -> Wi-Fi Configuration
```

### 4.3 Build the Firmware
```bash
idf.py build
```

### 4.4 Flash and Monitor
Connect the ESP32-P4 via USB-C:
```bash
idf.py -p /dev/ttyACM0 flash monitor
```

---

## 5. On-Chip JTAG Debugging

The ESP32-P4 includes integrated USB-Serial-JTAG on the main USB port:

### 5.1 Start OpenOCD
```bash
openocd-esp32 -f board/esp32p4-builtin.cfg
```

### 5.2 Launch GDB
In a separate terminal:
```bash
riscv32-esp-elf-gdb -ex "target remote localhost:3333" build/gps_imu_app.elf
```

Or select **"ESP32-P4: JTAG Debug (GDB / OpenOCD)"** in VS Code and press **`F5`**.

---

## 6. Real-Time Telemetry Visualization

The ESP32-P4 broadcasts 64-byte CTF 1.8 frames over Wi-Fi 6 or Ethernet to UDP port **9870**:

```bash
# Connect visualizers across the Wi-Fi network (replace with ESP32-P4 IP)
python3 apps/gps_imu_app/tools/flight_display.py --host 192.168.1.150 --port 9870
python3 tools/visualizer/abstractx_studio.py --host 192.168.1.150 --port 9870
```
