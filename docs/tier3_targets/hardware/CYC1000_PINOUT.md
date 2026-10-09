# AbstractX Trenz CYC1000 FPGA Pinout & Target Reference (`CYC1000_PINOUT.md`)

<!-- @impl [SPEC-TARGET-01] docs/tier3_targets/hardware/CYC1000_PINOUT.md -->

This document defines the hardware pin assignments, architectural resource budgets, and flashing procedures for the **Trenz Electronic CYC1000 FPGA board** (`TEI0003` with Intel/Altera Cyclone 10 LP `10CL025YU256C8G`).

---

## 1. Board Overview & Silicon Capabilities

The CYC1000 is an ultra-compact, thumb-drive form-factor FPGA development board:
* **FPGA Silicon**: Intel Cyclone 10 LP `10CL025YU256C8G` (24,624 Logic Elements, 66 M9K Block RAMs / 594 Kbits, 66 18x18 multipliers).
* **Onboard Memory**: 64 MBit (8 MB) SDRAM (`W9864G6JT-6`), 16 MBit Flash (`W25Q16DV`).
* **Onboard Clock**: 12.0 MHz MEMS oscillator directly connected to global clock pin `M2`.
* **Programmer**: Onboard FTDI USB-Blaster II compatible interface (supported natively by `openFPGALoader`).
* **Pin Headers**: Arduino MKR-compatible dual-inline headers (3.3V LVCMOS standard).

---

## 2. Top-Level Control & System Pins

| Physical Signal | FPGA Package Pin | Direction | I/O Standard | Description |
| :--- | :--- | :--- | :--- | :--- |
| **`i_clk`** | **Pin M2** | Input | 3.3-V LVCMOS | 12.0 MHz Global Clock Input (CLK2) |
| **`i_reset_n`** | **Pin N6** | Input | 3.3-V LVCMOS | Active-Low Onboard User Pushbutton (S1) |
| **`o_int_req`** | **Pin L2** | Output | 3.3-V LVCMOS | Doorbell Interrupt Pin to Host MCU / CPU |

---

## 3. Host SPI / Dual-SPI Slave Bus (Arduino MKR Header)

Connects to host microcontrollers (Raspberry Pi Pico 2 W, ESP32-P4, STM32) or Linux SBC:

| Signal | Package Pin | Direction | Standard SPI Mode | Dual-SPI Mode | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`i_spi_sclk`** | **Pin M1** (D8) | Input | **SCLK** | **SCLK** | SPI Clock Input (Up to 50 MHz) |
| **`i_spi_cs_n`** | **Pin P1** (D11) | Input | **CS_N** | **CS_N** | Active-Low Chip Select |
| **`io_spi_io0`** | **Pin N2** (D10) | Inout | **MOSI** | **SDIO0** | Data Line 0 (Host OUT / FPGA IN) |
| **`io_spi_io1`** | **Pin N1** (D9) | Inout | **MISO** | **SDIO1** | Data Line 1 (Host IN / FPGA OUT) |

---

## 4. External IMU Master SPI Bus (ICM-42688-P Sensor)

| Signal | Package Pin | Direction | Description |
| :--- | :--- | :--- | :--- |
| **`o_imu_sclk`** | **Pin B1** (D0) | Output | Hardware IMU SPI Master Clock |
| **`o_imu_cs_n`** | **Pin C2** (D1) | Output | Active-Low IMU Chip Select |
| **`o_imu_mosi`** | **Pin J1** (D2) | Output | Master-Out Slave-In Data |
| **`i_imu_miso`** | **Pin J2** (D3) | Input | Master-In Slave-Out Data |
| **`i_imu_int`**  | **Pin K1** (D4) | Input | Active-High Data Ready (`DRDY`) Hardware Interrupt |

---

## 5. Motor DShot & Actuator Outputs

| Signal | Package Pin | Direction | Description |
| :--- | :--- | :--- | :--- |
| **`o_motor_pins[0]`** | **Pin K2** (D5) | Output | DShot150/300/600 Motor Ch 1 |
| **`o_motor_pins[1]`** | **Pin L1** (D6) | Output | DShot150/300/600 Motor Ch 2 |
| **`o_motor_pins[2]`** | **Pin P2** (D12)| Output | DShot150/300/600 Motor Ch 3 |
| **`o_motor_pins[3]`** | **Pin R1** (A0) | Output | DShot150/300/600 Motor Ch 4 |
| **`o_neopixel_pin`**  | **Pin T1** (A1) | Output | WS2812B NeoPixel RGB LED Strip Output |

---

## 6. Onboard Status LEDs

| Signal | Package Pin | Direction | Description |
| :--- | :--- | :--- | :--- |
| **`o_led[0]`** | **Pin M6** | Output | User LED 0 (Heartbeat / PLL Lock) |
| **`o_led[1]`** | **Pin T4** | Output | User LED 1 (SPI Slave Activity) |
| **`o_led[2]`** | **Pin T3** | Output | User LED 2 (IMU DRDY Active) |
| **`o_led[3]`** | **Pin R3** | Output | User LED 3 (Hardware ILA Trace Active) |

---

## 7. Resource Budget & Utilization Envelope

With the parameterized AbstractX X-Fabric (`DATA_WIDTH = 32`), the entire system compiles cleanly into the CYC1000 with immense headroom:

| Component | Target Allocation | Cyclone 10 LP Capacity | Utilization % |
| :--- | :--- | :--- | :--- |
| **Logic Elements (LEs)** | **~1,450 LEs** | 24,624 LEs | **5.9%** |
| **Registers (Flip-Flops)**| **~820 FFs** | 24,624 FFs | **3.3%** |
| **M9K Block RAMs** | **8 M9K blocks (8 KB)** | 66 M9K blocks | **12.1%** |
| **User Logic Headroom** | **> 23,000 LEs** | - | **94.1% FREE** |

---

## 8. Build & Deployment Instructions

### 1. Direct Synthesis with Intel Quartus Prime Lite
Generate the programming bitstream using Intel Quartus Lite:
```bash
quartus_sh --flow compile cyc1000_abstractx
```

### 2. Flashing using `openFPGALoader`
Flashing the CYC1000 is 100% automated using the standardized `openFPGALoader` utility in `~/.tools/openFPGALoader`:
```bash
# Load directly into Cyclone 10 LP SRAM:
openFPGALoader -b cyc1000 build/cyc1000_abstractx.svf

# Or write permanently to onboard SPI flash:
openFPGALoader -b cyc1000 -f build/cyc1000_abstractx.rbf
```
