# AbstractX Tang Nano 9K FPGA Pinout Reference (`TANG9K_PINOUT.md`)

This document defines the physical hardware pin assignments for the **Tang Nano 9K FPGA board** (`GW1NR-LV9QN88PC6/I5`).

---

## 1. Top-Level System Control & Interrupt Pins

| Physical Signal | FPGA Package Pin | Direction | Voltage Standard | Description |
|---|---|---|---|---|
| **`i_clk`** | Pin 52 | Input | LVCMOS33 | 27.0 MHz Onboard Crystal Oscillator |
| **`i_reset_n`** | Pin 4 | Input | LVCMOS33 | Active-Low Reset Button (S2) |
| **`o_int_req`** | Pin 36 | Output | LVCMOS33 | Doorbell IRQ Output to Host CPU |

---

## 2. Host Dual-SPI Slave Bus (Connection to Allwinner A55 / E907 / Host MCU)

| Signal | Package Pin | Direction | Pull Mode | Description |
|---|---|---|---|---|
| **`i_spi_sclk`** | Pin 25 | Input | PULL_DOWN | Dual-SPI Clock Input (Up to 50 MHz) |
| **`i_spi_cs_n`** | Pin 26 | Input | PULL_UP | Active-Low Chip Select |
| **`io_spi_io0`** | Pin 27 | Inout | PULL_DOWN | Dual-SPI Data Line 0 (MOSI / SDIO0) |
| **`io_spi_io1`** | Pin 28 | Inout | PULL_DOWN | Dual-SPI Data Line 1 (MISO / SDIO1) |

---

## 3. External IMU Master SPI Bus (Connection to ICM-42688-P Sensor)

| Signal | Package Pin | Direction | Pull Mode | Description |
|---|---|---|---|---|
| **`o_imu_sclk`** | Pin 31 | Output | - | Hardware IMU SPI Master Clock |
| **`o_imu_cs_n`** | Pin 32 | Output | PULL_UP | Active-Low IMU Chip Select |
| **`o_imu_mosi`** | Pin 33 | Output | - | Master-Out Slave-In Data |
| **`i_imu_miso`** | Pin 34 | Input | PULL_UP | Master-In Slave-Out Data |
| **`i_imu_int`**  | Pin 35 | Input | PULL_UP | Active-High Data Ready (`DRDY`) Hardware Interrupt |

---

## 4. DShot 150/300/600 & PWM Motor Outputs (Quadcopter 4 Channels)

| Signal | Package Pin | Direction | Target ESC / Motor Channel |
|---|---|---|---|
| **`o_motor_pins[0]`** | Pin 38 | Output | Motor Channel 1 (DShot / Servo PWM) |
| **`o_motor_pins[1]`** | Pin 39 | Output | Motor Channel 2 (DShot / Servo PWM) |
| **`o_motor_pins[2]`** | Pin 40 | Output | Motor Channel 3 (DShot / Servo PWM) |
| **`o_motor_pins[3]`** | Pin 41 | Output | Motor Channel 4 (DShot / Servo PWM) |

---

## 5. NeoPixel WS2812B RGB Status LED Output Pin

| Signal | Package Pin | Direction | Voltage Standard | Description |
|---|---|---|---|---|
| **`o_neopixel_pin`** | **Pin 37** | Output | LVCMOS33 | Single-wire 800 kHz WS2812B NeoPixel RGB Signal Output (TLP Base `0x40000600`) |

---

## 6. Onboard LED Diagnostics & Linux Dynamic Control

| Signal | Package Pin | Drive Strength | Control Source | Description |
|---|---|---|---|---|
| **`o_led[0]`** | Pin 10 | 8 mA | FPGA Hardware | **1 Hz Hardware Heartbeat Blinker** |
| **`o_led[1]`** | Pin 11 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 1) |
| **`o_led[2]`** | Pin 13 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 2) |
| **`o_led[3]`** | Pin 14 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 3) |
| **`o_led[4]`** | Pin 15 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 4) |
| **`o_led[5]`** | Pin 16 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 5) |
