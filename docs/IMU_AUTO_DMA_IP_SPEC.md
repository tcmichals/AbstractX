# AbstractX IMU SPI Master & Auto-DMA IP Core Specification

This document details the hardware architecture, register map, operational workflow, simulation model, and iNav/Betaflight driver alignment for the **AbstractX IMU SPI Master & Auto-DMA IP Core**.

---

## 1. Overview & Key Objectives

Modern flight controllers and robotic systems require sub-millisecond, low-jitter access to Inertial Measurement Unit (IMU) telemetry (accelerometers, gyroscopes, and temperature sensors).

The AbstractX IMU IP core integrates:
1. **Dedicated SPI Master Controller**: Hardware SPI master interfacing directly with external IMU sensor ICs (e.g. InvenSense ICM-42688-P, Bosch BMI088, ST LSM6DSOX).
2. **Hardware Interrupt Trigger Engine (`IMU_INT`)**: Dedicated FPGA input pin connected to the IMU's data-ready interrupt pin (`DRDY`/`INT1`).
3. **Autonomous SPI Burst Reader**: Executes a pre-configured SPI burst read sequence automatically upon receiving an `IMU_INT` trigger, bypassing host CPU overhead entirely.
4. **Sub-Microsecond Timestamping**: Captures a 64-bit hardware timestamp at the exact clock cycle `IMU_INT` is asserted.
5. **64-Byte TLP Stream Packetizer**: Formats raw sensor bytes into a 64-byte `DMA_Stream` TLP (`Type = 0x10`, `Channel = 0x02 TELEMETRY`) and enqueues it to the transport FIFO for host egress.

---

## 2. Hardware Signal Interfaces & Block Architecture

```
+-----------------------------------------------------------------------------------+
|                            IMU SPI Master & Auto-DMA IP                           |
|                                                                                   |
|  External Pins                Hardware State Machine                 Fabric Output|
|  +---------+   IMU_INT Pulse  +-------------------+   Latch 64b Timer +------------+
|  | IMU_INT | ---------------> | Latch Timestamp   | ----------------> | 64-Byte    |
|  +---------+                  | Execute SPI Burst |                   | TLP Stream |
|                               +-------------------+                   | Generator  |
|                                         |                             +------------+
|                                         v                                    |
|                               +-------------------+                          v
|  External SPI Pins            | SPI Master Core   |                    Transport FIFO
|  +------------------------+   | (SCLK, CS_N,      |                    (Dual-SPI Link)
|  | SCLK, CS_N, MOSI, MISO | <---|  MOSI, MISO)    |                                 
|  +------------------------+   +-------------------+                                 
+-----------------------------------------------------------------------------------+
```

### Pin Definitions:
- `o_imu_sclk` (FPGA Output): Serial Clock to external IMU.
- `o_imu_cs_n` (FPGA Output): Active-Low Chip Select to external IMU.
- `o_imu_mosi` (FPGA Output): Master Output Slave Input (transmits read register commands).
- `i_imu_miso` (FPGA Input): Master Input Slave Output (receives raw sensor bytes).
- `i_imu_int` (FPGA Input GPIO): External Data-Ready Interrupt pin connected to IMU `DRDY` / `INT1`.

---

## 3. Register Interface (Wishbone Address Map)

Host applications configure and query the IMU IP core by issuing `MemRd` and `MemWr` TLPs to the core's Wishbone address space (Base Address: `0x40000100`).

| Address Offset | Register Name | R/W | Bit Description |
|---|---|---|---|
| `0x40000100` | `IMU_CTRL` | R/W | Bit 0: `AUTO_DMA_EN` (1 = Enable Auto-DMA on `IMU_INT` trigger)<br>Bit 1: `MANUAL_START` (1 = Trigger manual SPI transaction)<br>Bit 2: `INT_POLARITY` (0 = Active Low, 1 = Active High)<br>Bits [7:4]: `SPI_PRESCALER` (SCLK clock divider) |
| `0x40000104` | `IMU_BURST_ADDR`| R/W | Bits [7:0]: Read register start address on IMU (e.g. `0x1F` for ICM-42688 ACCEL_DATA_X1) |
| `0x40000108` | `IMU_BURST_LEN` | R/W | Bits [5:0]: Burst read length in bytes (e.g. 14 bytes for Accel[6] + Gyro[6] + Temp[2]) |
| `0x4000010C` | `IMU_MAN_DATA`  | R/W | Manual SPI Data Buffer (for sensor register setup / write configuration) |
| `0x40000110` | `IMU_STATUS`    | R   | Bit 0: `BUSY`, Bit 1: `SAMPLE_READY`, Bits [31:16]: Sample Count Counter |
| `0x40000114` | `IMU_LATCH_TS_H`| R   | Latched 64-bit Timestamp High 32 bits (from last auto-DMA trigger) |
| `0x40000118` | `IMU_LATCH_TS_L`| R   | Latched 64-bit Timestamp Low 32 bits |

---

## 4. Operational Modes & Hardware Flow

### 4.1 Manual Configuration Mode (`AUTO_DMA_EN = 0`)
1. Host sets `AUTO_DMA_EN = 0` in `IMU_CTRL`.
2. Host issues `MemWr` TLPs to write configuration registers on the external IMU via `IMU_MAN_DATA` (e.g., setting sample rates, full-scale ranges, power modes).
3. FPGA SPI Master executes standard SPI transactions for setup.

### 4.2 Auto-DMA Telemetry Mode (`AUTO_DMA_EN = 1`)
1. Host configures target register (`IMU_BURST_ADDR = 0x1D`) and length (`IMU_BURST_LEN = 14`).
2. Host enables Auto-DMA by writing `IMU_CTRL = 0x00000001`.
3. When external IMU asserts `IMU_INT`:
   - FPGA hardware latches current 64-bit hardware nanosecond clock.
   - SPI Master asserts `IMU_CS_N` low and clocks out `IMU_BURST_ADDR | 0x80` (read command) over `IMU_MOSI`.
   - SPI Master clocks in 14 bytes of raw sensor data (Accel X/Y/Z, Gyro X/Y/Z, Temp) from `IMU_MISO`.
   - Core builds a 64-byte TLP:
     * **DW0**: `Type = 0x10 (DMA_Stream)`, `Tag = 0x00`, `Channel = 0x02 (TELEMETRY)`
     * **DW1**: Target Address = `0x40000100` (Uses the **exact same Wishbone base address / device ID** as the IMU peripheral target on the Wishbone bus!)
     * **DW2**: Length = 4 DWs (14 bytes payload)
     * **DW3-DW4**: 64-bit Latched Hardware Timestamp
     * **DW5-DW8**: 14 bytes raw IMU sensor payload (Accel X/Y/Z, Gyro X/Y/Z, Temp) + 2 bytes zero-padding
     * **DW9-DW14**: Unused payload zero-padded (24 bytes) up to 64-byte TLP boundary
     * **DW15**: CRC32 Checksum
   - Enqueues TLP to transport FIFO and asserts `o_int_req` doorbell pin to host CPU.
4. Host CPU receives `o_int_req` interrupt, clocks out the 64B TLP over Dual-SPI (`0xA2`), and `o_int_req` deasserts.

---

## 5. iNav & Betaflight Flight Controller Driver Alignment Review

AbstractX's hardware Auto-DMA engine and Python Cocotb emulator ([`sim/cocotb/icm42688p_cocotb_vip.py`](file:///home/tcmichals/ssdData/projects/home/AbstractX/sim/cocotb/icm42688p_cocotb_vip.py)) are directly aligned with both the official **iNav Flight Controller Driver** (`accgyro_icm42605.c`) and **Betaflight Driver** (`accgyro_spi_icm426xx.c`):

| Driver Function / Register | iNav & Betaflight Value | AbstractX FPGA / Cocotb Alignment |
|---|---|---|
| `WHO_AM_I` Signature | `0x75 -> 0x47` | Verifies InvenSense ICM-42688-P device signature `0x47`. |
| `PWR_MGMT0` Mode | `0x4E -> 0x0F` | Low-Noise Accel Mode + Low-Noise Gyro Mode. |
| `GYRO_CONFIG0` Scale & ODR | `0x4F -> 0x06` | Configures Gyro Full-Scale `±2000 dps`, `1 kHz` ODR. |
| `ACCEL_CONFIG0` Scale & ODR | `0x50 -> 0x06` | Configures Accel Full-Scale `±16g`, `1 kHz` ODR. |
| `INT_CONFIG` Interrupt Pin | `0x14 -> 0x03` | `INT1`/`DRDY` pin: Active High, Push-Pull, Pulsed mode. |
| `INT_SOURCE0` DRDY Enable | `0x65 -> 0x08` | Enables `UI_DRDY_INT1_EN_ENABLED`. |
| `TEMP_DATA1` Burst Start | `0x1D` (14 Bytes) | Sets FPGA `IMU_BURST_ADDR = 0x1D` for continuous Temp[2] + Accel[6] + Gyro[6] acquisition. |

---

## 6. Simulation & Python Cocotb VIP Model

Verification is performed via Python Cocotb ([`sim/cocotb/test_asp_tlp_64b_cocotb.py`](file:///home/tcmichals/ssdData/projects/home/AbstractX/sim/cocotb/test_asp_tlp_64b_cocotb.py)):
- **Python Cocotb VIP ([`sim/cocotb/icm42688p_cocotb_vip.py`](file:///home/tcmichals/ssdData/projects/home/AbstractX/sim/cocotb/icm42688p_cocotb_vip.py))**: Pure Python emulator model acting as the ICM-42688-P IC attached to FPGA pins (`dut.imu_sclk`, `dut.imu_cs_n`, `dut.imu_mosi`, `dut.imu_miso`, `dut.imu_int_i`).
- **DRDY Interrupt Generator**: Pulses `imu_int_i` at the configured ODR rate.
- **Synthesizable FPGA Core**: The Verilog codebase remains 100% clean and synthesizable (`asp_top.sv`, `asp_spi_frontend.sv`, `asp_router.sv`, `asp_wishbone_master.sv`, `asp_imu_auto_dma.sv`).

---

## 7. Performance Metrics

- **Latency Jitter**: $< 20 \text{ ns}$ (hardware timestamp captured on exact clock cycle of `IMU_INT` rising/falling edge).
- **Transport Latency**: At 50 MHz Dual-SPI, the entire 64B telemetry TLP is delivered to host CPU memory in **5.12 microseconds**.
- **Host CPU Load**: 0% CPU utilization during SPI acquisition; host receives fully framed, timestamped packets via interrupt/DMA.
