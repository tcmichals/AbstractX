# AbstractX Tang Primer 20K FPGA Pinout Reference (`PRIMER20K_PINOUT.md`)

This document defines the physical hardware pin assignments for the **Tang Primer 20K FPGA board** (`GW2A-LV18PG256C8/I7`).

---

## 1. Top-Level System Control & Interrupt Pins

| Physical Signal | FPGA Package Pin | Direction | Voltage Standard | Description |
|---|---|---|---|---|
| **`i_clk`** | Pin H11 | Input | LVCMOS33 | 27.0 MHz Onboard Crystal Oscillator |
| **`i_reset_n`** | Pin T10 | Input | LVCMOS33 | Active-Low Reset Button (S1) |
| **`o_int_req`** | Pin D10 | Output | LVCMOS33 | Doorbell IRQ Output to Host CPU |

---

## 2. Host SPI / Dual-SPI Slave Bus (Connection to Allwinner A55 / E907 / Host MCU)

| Signal | Package Pin | Direction | Pull Mode | Standard 4-Wire SPI | Dual-SPI Mode | Description |
|---|---|---|---|---|---|---|
| **`i_spi_sclk`** | Pin C13 | Input | PULL_DOWN | **SCLK** | **SCLK** | SPI Clock Input (Up to 50 MHz) |
| **`i_spi_cs_n`** | Pin B13 | Input | PULL_UP | **CS_N** | **CS_N** | Active-Low Chip Select |
| **`io_spi_io0`** | Pin B14 | Inout | PULL_DOWN | **MOSI** | **SDIO0** | SPI Data Line 0 (Host OUT / FPGA IN) |
| **`io_spi_io1`** | Pin A14 | Inout | PULL_DOWN | **MISO** | **SDIO1** | SPI Data Line 1 (Host IN / FPGA OUT) |

> **Universal SPI Compatibility**: Standard 4-wire SPI (SCLK, CS_N, MOSI, MISO) and Dual-SPI use the exact same physical header pins! Any standard SPI master (RP2350, STM32, ESP32, Linux `/dev/spidev`) communicates without hardware wiring changes.

---

## 3. External IMU Master SPI Bus (Connection to ICM-42688-P Sensor)

| Signal | Package Pin | Direction | Pull Mode | Description |
|---|---|---|---|---|
| **`o_imu_sclk`** | Pin D11 | Output | - | Hardware IMU SPI Master Clock |
| **`o_imu_cs_n`** | Pin N7 | Output | PULL_UP | Active-Low IMU Chip Select |
| **`o_imu_mosi`** | Pin N8 | Output | - | Master-Out Slave-In Data |
| **`i_imu_miso`** | Pin N9 | Input | PULL_UP | Master-In Slave-Out Data |
| **`i_imu_int`**  | Pin L9 | Input | PULL_UP | Active-High Data Ready (`DRDY`) Hardware Interrupt |

---

## 4. DShot 150/300/600 & PWM Motor Outputs (Quadcopter 4 Channels)

| Signal | Package Pin | Direction | Target ESC / Motor Channel |
|---|---|---|---|
| **`o_motor_pins[0]`** | Pin T6 | Output | Motor Channel 1 (DShot / Servo PWM) |
| **`o_motor_pins[1]`** | Pin T7 | Output | Motor Channel 2 (DShot / Servo PWM) |
| **`o_motor_pins[2]`** | Pin P6 | Output | Motor Channel 3 (DShot / Servo PWM) |
| **`o_motor_pins[3]`** | Pin R7 | Output | Motor Channel 4 (DShot / Servo PWM) |

---

## 5. NeoPixel WS2812B RGB Status LED Output Pin

| Signal | Package Pin | Direction | Voltage Standard | Description |
|---|---|---|---|---|
| **`o_neopixel_pin`** | **Pin A15** | Output | LVCMOS33 | Single-wire 800 kHz WS2812B NeoPixel RGB Signal Output (TLP Base `0x40000600`) |

---

## 6. Dedicated Hardware Logic Analyzer Debug Pins (`o_debug_pins[3:0]`)

| Signal | Package Pin | Direction | Logic Analyzer Signal Assignment | Description |
|---|---|---|---|---|
| **`o_debug_pins[0]`** | **Pin P8** | Output | **Host SPI CS Active (`~spi_cs_n`)** | Pulses High when Host SPI bus is active |
| **`o_debug_pins[1]`** | **Pin R8** | Output | **IMU Stream Valid (`imu_stream_tvalid`)** | Pulses High when IMU Auto-DMA emits 64B TLP |
| **`o_debug_pins[2]`** | **Pin T8** | Output | **FPGA TLP Egress Valid (`tlp_tx_valid`)** | Pulses High when FPGA outputs 64B TLP to Host |
| **`o_debug_pins[3]`** | **Pin T9** | Output | **Doorbell IRQ Pulse (`o_int_req`)** | Pulses High on Host Doorbell Interrupt Trigger |

---

## 7. Onboard LED Diagnostics & Linux Dynamic Control

| Signal | Package Pin | Drive Strength | Control Source | Description |
|---|---|---|---|---|
| **`o_led[0]`** | Pin L14 | 8 mA | FPGA Hardware | **1 Hz Hardware Heartbeat Blinker** |
| **`o_led[1]`** | Pin L16 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 1) |
| **`o_led[2]`** | Pin N14 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 2) |
| **`o_led[3]`** | Pin N16 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 3) |
| **`o_led[4]`** | Pin M14 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 4) |
| **`o_led[5]`** | Pin M15 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 5) |
