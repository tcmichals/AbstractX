# Radxa Cubie A5E Hardware Reference Documents

This directory contains the official hardware design schematics and component placement maps for the **Radxa Cubie A5E (Allwinner A527 / T527)**:

* [radxa_cubie_a5e_schematic_v1.2.pdf](file:///home/tcmichals/projects/AbstractX/docs/tier3_targets/hardware/schematics/radxa_cubie_a5e_schematic_v1.2.pdf) (Rev 1.2, 2025-01-13)
* [radxa_cubie_a5e_placement_v1.2.pdf](file:///home/tcmichals/projects/AbstractX/docs/tier3_targets/hardware/schematics/radxa_cubie_a5e_placement_v1.2.pdf) (Component Placement Map)

---

## Key Hardware Findings & Verification

### 1. Header `CON1` (40-Pin GPIO Expansion) — Sheet 19
The physical 40-pin header (`CON1`) is mapped on Sheet 19 of the schematic:

* **Pins 11 & 13**:
  * **Pin 11** connects to net `PWM0-10-PI` $\rightarrow$ SoC ball **`PI9`** (Functions: `PWM0-10`, `DMIC-DATA2`, `TWI5-SDA`, `PI-EINT9`).
  * **Pin 13** connects to net `PWM0-11-PI` $\rightarrow$ SoC ball **`PI10`** (Functions: `PWM0-11`, `DMIC-DATA1`, `OWA-OUT`, `I2S2-MCLK`, `PI-EINT10`).
  * ⚠️ **Pins 11 and 13 have NO hardware UART multiplexed onto them.**

### 2. SoC Bank U1B (Port B & UART2) — Sheet 5
* **Ball N28 (`PB0` / UART2-TX)**: **No Connect (NC)**. Not routed on PCB.
* **Ball N29 (`PB1` / UART2-RX)**: **No Connect (NC)**. Not routed on PCB.
* **Ball M31 (`PB2` / UART2-RTS)**: **No Connect (NC)**.
* **Ball M32 (`PB3` / UART2-CTS)**: **No Connect (NC)**.
* ⚠️ **UART2 (`0x02500800` / `/dev/ttyS2`) is NOT routed to the 40-pin header or anywhere else on the Cubie A5E board.**

### 3. Available UARTs on the 40-Pin Header
* **UART3** (`/dev/ttyAS3`):
  * **Pin 16** (`PI11` / Net `PWM0-12-PI`): `UART3-TX` $\rightarrow$ Connect to GPS RX
  * **Pin 31** (`PI12` / Net `SPI2-MISO-PI`): `UART3-RX` $\rightarrow$ Connect to GPS TX
* **UART4** (`/dev/ttyAS4`):
  * **Pin 26** (`PI0` / Net `I2S2-DIN3-PI`): `UART4-TX`
  * **Pin 32** (`PI1` / Net `I2S2-DIN2-PI`): `UART4-RX`
* **UART7** (`/dev/ttyAS7`):
  * **Pin 19** (`PB13` / Net `SPI1-MOSI-PB`): `UART7-TX`
  * **Pin 21** (`PB14` / Net `SPI1-MISO-PB`): `UART7-RX`
* **UART0** (`/dev/ttyS0` Debug Serial Console):
  * **Pin 8** (`PB9` / Net `CPUX-TX-PB` via R44 1k): `UART0-TX`
  * **Pin 10** (`PB10` / Net `CPUX-RX-PB` via R45 1k): `UART0-RX`

### 4. High-Speed SPI & Dual-SPI on the 40-Pin Header
* **SPI1 Controller** (`0x04026000`, supports Dual-SPI / DBI 2-bit IO0/IO1 streaming):
  * **Pin 24**: `PB11` $\rightarrow$ `SPI1-CS0` (FPGA Dual-SPI Chip Select)
  * **Pin 23**: `PB12` $\rightarrow$ `SPI1-CLK` (High-Speed Bus Clock)
  * **Pin 19**: `PB13` $\rightarrow$ `SPI1-MOSI` (Dual-SPI IO0)
  * **Pin 21**: `PB14` $\rightarrow$ `SPI1-MISO` (Dual-SPI IO1)
* **SPI2 Controller** (`0x04027000`, independent second SPI bus):
  * **Pin 33**: `PI6` $\rightarrow$ `SPI2-CS0`
  * **Pin 7**: `PI7` $\rightarrow$ `SPI2-CLK`
  * **Pin 29**: `PI8` $\rightarrow$ `SPI2-MOSI`
  * **Pin 31**: `PI12` $\rightarrow$ `SPI2-MISO`
