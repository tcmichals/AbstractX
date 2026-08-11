# AbstractX FCProtocol Specification (`FCPROTOCOL_SPECIFICATION.md`)

This document defines the normative **FCProtocol (Flight Controller Protocol)** specification for **AbstractX**. FCProtocol defines how flight software (Betaflight, ArduPilot, iNav, RTOS, Linux) communicates with the Gowin FPGA offloader over the **`asp-tlp-64b`** 64-byte PCIe-like transport protocol.

---

## 1. Architectural Philosophy

Unlike legacy serial packet protocols with variable-length headers and byte-stuffing, **AbstractX FCProtocol** operates as a **PCIe Memory-Mapped BAR Bus**:

1. **Fixed 64-Byte TLPs**: Every command, register access, and telemetry packet is a fixed 64-byte unit with CRC32 integrity.
2. **Zero-CPU Overhead IMU Telemetry**: Hardware automatically streams timestamped 64-byte sensor TLPs on IMU `DRDY` interrupts.
3. **Master Nanosecond Timebase**: Every sensor sample is lensed with a monotonic 64-bit nanosecond hardware timestamp.
4. **Decoupled Flight Stack**: Betaflight / ArduPilot control loops invoke simple register functions (`pcie_set_motor_throttle()`, `pcie_get_rc_channel_us()`) without managing microcontroller timers or interrupts.

---

## 2. Complete Memory BAR Register Map (`0x40000000..0x40000600`)

```
+-----------------------------------------------------------------------------------+
|                           ABSTRACTX WISHBONE BAR MAP                              |
+------------+----------------------+------+----------------------------------------+
| Base Addr  | Region Name          | Access| Function                              |
+------------+----------------------+------+----------------------------------------+
| 0x40000000 | System & Timestamp   | R/W  | Identity, Scratch, LEDs, 64-bit Time   |
| 0x40000100 | IMU Auto-DMA Engine  | R/W  | ICM-42688-P SPI & Telemetry Stream     |
| 0x40000200 | DShot / Motor Core   | R/W  | 4-Ch DShot150/300/600 & Servo PWM      |
| 0x40000300 | PWM Receiver Decoder | RO   | 4-Ch RC Pulse Capture (1000us - 2000us)|
| 0x40000600 | NeoPixel Status RGB  | R/W  | WS2812B 800kHz Single-Wire Driver      |
+------------+----------------------+------+----------------------------------------+
```

### 2.1 System Identity & Central Timestamp (`0x40000000..0x40000014`)

| Address | Register | Type | Reset | Description |
|---|---|---|---|---|
| `0x40000000` | `REG_SYS_ID_REV` | RO | `0xABF10164` | **Device ID** (`0xABF1`), **Rev** (`0x01`), **Arch** (`0x64`) |
| `0x40000004` | `REG_SYS_VENDOR_ID` | RO | `0x19981ACC` | **Subsystem Vendor** (`0x1998`), **Vendor ID** (`0x1ACC`) |
| `0x40000008` | `REG_SYS_SCRATCH` | R/W | `0xCAFEBABE` | **Host R/W Loopback Scratchpad** |
| `0x4000000C` | `REG_SYS_LED_CTRL` | R/W | `0x0000003E` | **Linux-Controllable Onboard LEDs 2..6** (Bits 1..5) |
| `0x40000010` | `REG_SYS_TIME_LOW` | RO | Counter | **Master Nanosecond Timestamp [31:0]** (latches high bits) |
| `0x40000014` | `REG_SYS_TIME_HIGH` | RO | Counter | **Atomic Shadow Timestamp High [63:32]** |

### 2.2 Motor Control Core (`0x400000200..0x40000210`)

| Address | Register | Type | Description |
|---|---|---|---|
| `0x40000200` | `REG_MOTOR_CTRL` | R/W | Protocol Select: `00`=DShot600, `01`=DShot300, `10`=DShot150, `11`=Servo PWM |
| `0x40000204` | `REG_MOTOR_CH1`  | R/W | Motor Channel 1 Throttle Value (`0..2047` DShot / `1000..2000` PWM) |
| `0x40000208` | `REG_MOTOR_CH2`  | R/W | Motor Channel 2 Throttle Value |
| `0x4000020C` | `REG_MOTOR_CH3`  | R/W | Motor Channel 3 Throttle Value |
| `0x40000210` | `REG_MOTOR_CH4`  | R/W | Motor Channel 4 Throttle Value |

### 2.3 PWM Receiver Decoder Core (`0x40000300..0x40000310`)

| Address | Register | Type | Description |
|---|---|---|---|
| `0x40000300` | `REG_PWM_DEC_CTRL` | RO | `[31:16]`=ID (`0x0001`), `[15:8]`=`NUM_CH` (4), `[7:0]`=Ready Flags |
| `0x40000304` | `REG_PWM_DEC_CH1`  | RO | Channel 1 Pulse Width `[15:0]` in $\mu\text{s}$ (1000–2000) + Error Flags `[31:16]` |
| `0x40000308` | `REG_PWM_DEC_CH2`  | RO | Channel 2 Pulse Width `[15:0]` in $\mu\text{s}$ (1000–2000) + Error Flags `[31:16]` |
| `0x4000030C` | `REG_PWM_DEC_CH3`  | RO | Channel 3 Pulse Width `[15:0]` in $\mu\text{s}$ (1000–2000) + Error Flags `[31:16]` |
| `0x40000310` | `REG_PWM_DEC_CH4`  | RO | Channel 4 Pulse Width `[15:0]` in $\mu\text{s}$ (1000–2000) + Error Flags `[31:16]` |

### 2.4 NeoPixel WS2812B Status RGB LED Core (`0x40000600..0x40000604`)

| Address | Register | Type | Description |
|---|---|---|---|
| `0x40000600` | `REG_NEO_CTRL` | R/W | Bit 0 = Enable, Bits `7..0` = Number of LEDs |
| `0x40000604` | `REG_NEO_LED0` | R/W | LED 0 Color `0x00RRGGBB` (24-bit RGB) |

---

## 3. Flight Controller C API Reference (`pcie_reg_api.h`)

Below is the clean C API used by flight controllers to interact with AbstractX:

```c
#ifndef ABSTRACTX_PCIE_REG_API_H
#define ABSTRACTX_PCIE_REG_API_H

#include <stdint.h>
#include <stdbool.h>

// Register addresses
#define REG_SYS_ID_REV    0x40000000
#define REG_SYS_TIME_LOW  0x40000010
#define REG_SYS_TIME_HIGH 0x40000014
#define REG_MOTOR_CTRL    0x40000200
#define REG_MOTOR_CH1     0x40000204
#define REG_PWM_DEC_CH1   0x40000304
#define REG_NEO_CTRL      0x40000600
#define REG_NEO_LED0      0x40000604

// Hardware Access Primitives
extern uint32_t pcie_reg_read32(uint32_t addr);
extern void     pcie_reg_write32(uint32_t addr, uint32_t val);

// High-Level Flight Controller API
static inline uint64_t pcie_get_timestamp_ns(void) {
    uint32_t low  = pcie_reg_read32(REG_SYS_TIME_LOW);
    uint32_t high = pcie_reg_read32(REG_SYS_TIME_HIGH);
    return ((uint64_t)high << 32) | low;
}

static inline void pcie_set_motor_throttle(uint8_t ch, uint16_t val) {
    pcie_reg_write32(REG_MOTOR_CH1 + ((ch - 1) * 4), val);
}

static inline uint16_t pcie_get_rc_channel_us(uint8_t ch) {
    uint32_t raw = pcie_reg_read32(REG_PWM_DEC_CH1 + ((ch - 1) * 4));
    return (uint16_t)(raw & 0xFFFF);
}

static inline void pcie_set_status_rgb(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    pcie_reg_write32(REG_NEO_CTRL, 0x00000101);
    pcie_reg_write32(REG_NEO_LED0, rgb);
}

#endif // ABSTRACTX_PCIE_REG_API_H
```
