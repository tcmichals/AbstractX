# AbstractX Protocol, Hardware & Pinout Documentation

Welcome to the AbstractX documentation hub. This repository defines the PCIe-like register BAR architecture (`asp-tlp-64b`), SystemVerilog RTL cores, FPGA pinout maps, and Python test tooling.

---

## Architecture Specifications & Protocol Stack

1. **[`PORTABLE_FLIGHT_STACK_ARCHITECTURE.md`](PORTABLE_FLIGHT_STACK_ARCHITECTURE.md)**  
   *Why*: Specification for porting Betaflight, iNav, and ArduPilot to a decoupled PCIe-like Register BAR API (`pcie_reg_api.h`). Eliminates microcontroller HAL driver complexity and enables chip-specific hardware offload engines (RP2350 PIO, STM32 MDMA, Gowin FPGA fabric).

2. **[`ASP_SPEC_DIRECTION.md`](ASP_SPEC_DIRECTION.md)**  
   *Why*: Protocol direction specifying the 64-byte TLP (`asp-tlp-64b`) frame architecture.

3. **[`ASP_PROTOCOL.md`](ASP_PROTOCOL.md)**  
   *Why*: Normative specifications for 64-byte TLP headers, PCIe operations (`MemRd`, `MemWr`, `CplD`, `DMA_Stream`), and packet structures.

4. **[`ASP_SPI_TRANSPORT.md`](ASP_SPI_TRANSPORT.md)**  
   *Why*: Physical layer transport specifications for Dual-SPI and Single-SPI modes up to 50 MHz.

5. **[`ASP_SPI_REGISTER_MAP.md`](ASP_SPI_REGISTER_MAP.md)**  
   *Why*: Command byte mapping (`0xA1 TLP_WRITE_BURST`, `0xA2 TLP_READ_BURST`) and Wishbone register space.

6. **[`ABSTRACTX_SWITCH_FABRIC_ARCHITECTURE.md`](ABSTRACTX_SWITCH_FABRIC_ARCHITECTURE.md)**  
   *Why*: Parallel vector router fabric and Wishbone master gateway architecture.

7. **[`IMU_AUTO_DMA_IP_SPEC.md`](IMU_AUTO_DMA_IP_SPEC.md)**  
   *Why*: Hardware IMU SPI Master & Auto-DMA IP core for zero-CPU-overhead timestamped sensor telemetry streams.

---

## FPGA Pinout Maps & Hardware Rationale

8. **[`TANG9K_PINOUT.md`](TANG9K_PINOUT.md)**  
   *Why*: Physical pinout map and header layout for **Tang Nano 9K FPGA** (`GW1NR-9`).
   - **4 Motor Channels (`o_motor_pins[3:0]`)**: Configured for Quadcopter DShot150/300/600 & 1-wire ESC Serial Passthrough (`serial_4way.c`), saving FPGA logic resources.
   - **NeoPixel WS2812B RGB Pin**: Mapped to **Pin 37**.
   - **4 Dedicated Hardware Debug Pins (`o_debug_pins[3:0]`)**: Mapped to **Pins 42, 48, 49, 50** for logic analyzer / oscilloscope scoping.
   - **1 Hz FPGA Heartbeat Blinker**: Mapped to **LED 1 (Pin 10)** for instant visual confirmation of hardware clock & reset state.
   - **Linux Dynamic LED Control**: **LEDs 2..6 (Pins 11, 13, 14, 15, 16)** kept unassigned to internal fast signals so Linux or FreeRTOS can toggle them dynamically over PCIe TLP register `0x4000000C` (`REG_SYS_LED_CTRL`).

9. **[`PRIMER20K_PINOUT.md`](PRIMER20K_PINOUT.md)**  
   *Why*: Physical pinout map and header layout for **Tang Primer 20K FPGA** (`GW2A-18`).
   - **4 Motor Channels**: Pins T6, T7, P6, R7.
   - **NeoPixel WS2812B RGB Pin**: Pin A15.
   - **4 Hardware Debug Pins**: Pins P8, R8, T8, T9.
   - **1 Hz FPGA Heartbeat Blinker**: LED 1 (Pin L14).
   - **Linux Dynamic LED Control**: LEDs 2..6 (Pins L16, N14, N16, M14, M15).

---

## Hardware Identity & Central Timestamp Registers (`0x40000000..0x40000014`)

| Register Address | Name | Type | Reset / Expected | Description / Architectural Rationale |
|---|---|---|---|---|
| **`0x40000000`** | `REG_SYS_ID_REV` | RO | `0xABF10164` | **PCIe Device ID (0xABF1), Rev (0x01), Arch (0x64)** for driver link verification. |
| **`0x40000004`** | `REG_SYS_VENDOR_ID` | RO | `0x19981ACC` | **PCIe Subsystem Vendor (0x1998) & Vendor ID (0x1ACC)**. |
| **`0x40000008`** | `REG_SYS_SCRATCH` | R/W | `0xCAFEBABE` | **Host Loopback Scratchpad** for R/W bus verification. |
| **`0x4000000C`** | `REG_SYS_LED_CTRL` | R/W | `0x0000003E` | **Linux-Controllable Onboard LEDs 2..6** (Bits 1..5). |
| **`0x40000010`** | `REG_SYS_TIME_LOW` | RO | Monotonic Counter | **Master Timestamp Nanoseconds [31:0]**; latches high 32 bits into shadow. |
| **`0x40000014`** | `REG_SYS_TIME_HIGH` | RO | Monotonic Counter | **Atomic Shadow Master Timestamp Nanoseconds [63:32]** for tear-free 64-bit reads. |

---

## Verification & Interactive Test Tooling

10. **[`tools/test_asp_pcie.py`](../tools/test_asp_pcie.py)**  
    *Why*: Executable Python test utility supporting hardware `/dev/spidevX.Y` and `--mock` PC simulation modes.
    - `python3 tools/test_asp_pcie.py --mode rainbow`: WS2812B NeoPixel RGB animated rainbow wave.
    - `python3 tools/test_asp_pcie.py --mode pwm_sweep`: 1000 µs to 2000 µs Servo PWM / DShot motor sweep.
    - `python3 tools/test_asp_pcie.py --mode led_chase`: Knight-rider chaser pattern across onboard LEDs.
    - `python3 tools/test_asp_pcie.py --mode cli`: Interactive shell to read/write any register, control motors, and drive RGB colors.

---

## Governance, Verification & Engineering Logs

11. **[`ENGINEERING_LOG.md`](ENGINEERING_LOG.md)**: Chronological history of engineering milestones, commit hashes, bitstream results, and git archive branches.
12. **[`ASP_REQUIREMENTS.md`](ASP_REQUIREMENTS.md)**: Requirements, quality gates, and definition-of-done.
13. **[`ASP_VALIDATION_MATRIX.md`](ASP_VALIDATION_MATRIX.md)**: Verification test matrix across synthesis, timing closure, and TLP protocol tests.
