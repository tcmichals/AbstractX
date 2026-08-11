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

## 4. DShot 150/300/600 & ESC Serial Passthrough Pins (4 Motor Channels)

| Signal | Package Pin | Direction | Normal Flight Mode | ESC Passthrough Mode (`serial_4way.c`) |
|---|---|---|---|---|
| **`o_motor_pins[0]`** | **Pin 38** | Bidirectional | DShot150/300/600 Output Ch 1 | 1-Wire Software UART (BLHeli / AM32 ESC 1) |
| **`o_motor_pins[1]`** | **Pin 39** | Bidirectional | DShot150/300/600 Output Ch 2 | 1-Wire Software UART (BLHeli / AM32 ESC 2) |
| **`o_motor_pins[2]`** | **Pin 40** | Bidirectional | DShot150/300/600 Output Ch 3 | 1-Wire Software UART (BLHeli / AM32 ESC 3) |
| **`o_motor_pins[3]`** | **Pin 41** | Bidirectional | DShot150/300/600 Output Ch 4 | 1-Wire Software UART (BLHeli / AM32 ESC 4) |

> **Note on Serial Passthrough**: When BLHeliSuite or AM32 Configurator connects over USB to flash or configure ESCs, the FPGA reconfigures motor pins 38..41 as 1-wire half-duplex Software UART pins. Serial bytes are tunneled back and forth over **64-byte TLP Stream Channel 0x05**.

---

## 5. NeoPixel WS2812B RGB Status LED Output Pin

| Signal | Package Pin | Direction | Voltage Standard | Description |
|---|---|---|---|---|
| **`o_neopixel_pin`** | **Pin 37** | Output | LVCMOS33 | Single-wire 800 kHz WS2812B NeoPixel RGB Signal Output (TLP Base `0x40000600`) |

---

## 6. Dedicated Hardware Logic Analyzer Debug Pins (`o_debug_pins[3:0]`)

| Signal | Package Pin | Direction | Logic Analyzer Signal Assignment | Description |
|---|---|---|---|---|
| **`o_debug_pins[0]`** | **Pin 42** | Output | **Host SPI CS Active (`~spi_cs_n`)** | Pulses High when Host SPI bus is active |
| **`o_debug_pins[1]`** | **Pin 48** | Output | **IMU Stream Valid (`imu_stream_tvalid`)** | Pulses High when IMU Auto-DMA emits 64B TLP |
| **`o_debug_pins[2]`** | **Pin 49** | Output | **FPGA TLP Egress Valid (`tlp_tx_valid`)** | Pulses High when FPGA outputs 64B TLP to Host |
| **`o_debug_pins[3]`** | **Pin 50** | Output | **Doorbell IRQ Pulse (`o_int_req`)** | Pulses High on Host Doorbell Interrupt Trigger |

---

## 7. Onboard LED Diagnostics & Linux Dynamic Control

| Signal | Package Pin | Drive Strength | Control Source | Description |
|---|---|---|---|---|
| **`o_led[0]`** | Pin 10 | 8 mA | FPGA Hardware | **1 Hz Hardware Heartbeat Blinker** |
| **`o_led[1]`** | Pin 11 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 1) |
| **`o_led[2]`** | Pin 13 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 2) |
| **`o_led[3]`** | Pin 14 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 3) |
| **`o_led[4]`** | Pin 15 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 4) |
| **`o_led[5]`** | Pin 16 | 8 mA | **Linux Host** | **Free / Controllable** via TLP Reg `0x40000008` (Bit 5) |
