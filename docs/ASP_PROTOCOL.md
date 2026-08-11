# ASP Protocol Specification (ASP/1 - 64-Byte TLP Profile)

**ASP** = **AbstractX Switch Protocol**

This document is the **normative** protocol specification for `ASP/1` operating under the **PCIe-like 64-Byte Transaction Layer Packet (`asp-tlp-64b`)** profile in the AbstractX repository.

---

## 1. Frame Format (Normative Wire Specification)

All multi-byte integers in headers are **Big-Endian** on the wire.

Every AbstractX frame is **exactly 64 bytes (16 DWORDs / 512 bits)**.

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Type      |     Flags     |      Tag      | Channel/AXID  | DW0 (Header)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Target Address (32-bit)                   | DW1 (Header)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       Length (in DW)          |       Sequence Number         | DW2 (Header)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                Hardware Timestamp High (32-bit)               | DW3 (Header)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                Hardware Timestamp Low (32-bit)                | DW4 (Header)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               | DW5..DW14
|                  Data Payload (40 Bytes)                      | (Payload)
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Packet CRC32 / Checksum                    | DW15 (Footer)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### 1.1 Field Definitions

| Field | Size (Bytes) | Description |
|---|---:|---|
| `Type` | 1 | TLP operation type (`MemRd`, `MemWr`, `CplD`, `Cpl`, `DMA_Stream`, `DMA_Cfg`) |
| `Flags` | 1 | Operational flags (`0x01` = Completion Requested, `0x02` = Error Flag) |
| `Tag` | 1 | Split-transaction correlation ID matching `MemRd` requests to `CplD` responses |
| `Channel/AXID` | 1 | Logical destination/source routing plane ID |
| `Target Address` | 4 | 32-bit Wishbone target register or memory address |
| `Length` | 2 | Payload length in 32-bit DWORDs (1 to 10 DWs, maximum 40 bytes) |
| `Sequence Number` | 2 | Per-channel packet sequence counter |
| `Hardware Timestamp` | 8 | 64-bit nanosecond timer captured at ingress/egress |
| `Data Payload` | 40 | Data bytes (zero-padded if valid data $< 40$ bytes) |
| `CRC32` | 4 | IEEE 802.3 CRC32 calculated over DW0 through DW14 (bytes 0 to 59) |

---

## 2. Transaction Semantics (PCIe TLP Analogy)

### 2.1 `MemRd` (Memory Read Operation - Type `0x01`)
- Issued by Host to read Wishbone registers or memory.
- `Target Address` specifies 32-bit Wishbone start address.
- `Length` specifies number of 32-bit DWORDs to read (1 to 10).
- FPGA Wishbone Master executes read cycle(s) and returns a `CplD` TLP carrying the matching `Tag`.

### 2.2 `MemWr` (Memory Write Operation - Type `0x02`)
- Issued by Host to perform posted writes to Wishbone target registers.
- Payload (DW5..DW14) carries write data.
- If `Flags & 0x01` is set, FPGA returns a `Cpl` TLP acknowledging write completion.

### 2.3 `CplD` (Completion with Data - Type `0x03`)
- Emitted by FPGA in response to a `MemRd` request.
- Copies `Tag`, `Channel`, and `Sequence Number` from the request TLP.
- Payload (DW5..DW14) contains fetched 32-bit register data.

### 2.4 `DMA_Stream` (Autonomous Egress - Type `0x10`)
- Emitted autonomously by FPGA peripheral engines (IMU Auto-DMA, UART ESC, telemetry).
- **Target Address (DW1)**: Populated with the **exact Wishbone base address / device ID** of the originating peripheral (e.g. `0x40000100` for IMU, `0x40000500` for UART ESC).
- Header contains 64-bit hardware timestamp latched at event time (`IMU_INT` trigger or UART RX).
- `Length` indicates valid payload byte count divided by 4 (rounded up).

### 2.5 Payload Padding & Length Extraction Rules
To maintain static 512-bit shift register mechanics while allowing variable valid payload lengths:
- **Transmitter (FPGA/Host)**: Calculates the valid byte count, sets `Length = ceil(valid_bytes / 4)` in DW2, and automatically **zero-pads** the unused trailing payload bytes (DW5..DW14) up to the 64-byte TLP boundary.
- **Receiver (Host/FPGA)**: Reads `Length` from DW2, extracts exactly `valid_bytes` from DW5..DW14, and safely discards the zero-padding.
- **Payload Capacity**: Up to 10 DWORDs (40 bytes) per 64-byte TLP container.

---


## 3. Peripheral DMA Engines & Flush Rules

### 3.1 UART ESC Serial Tunnel Engine (`Channel = 0x05`)
To optimize transmission efficiency over Dual-SPI, the UART ESC DMA engine implements a **dual-trigger flush policy**:

1. **Full Buffer Trigger**: Flushes immediately when 40 bytes of RX serial data accumulate in the engine FIFO.
2. **2-Character Idle Timeout Trigger**: When UART RX line remains idle for **2 character periods** (~20 bit-times, e.g. 173 µs at 115,200 baud), the engine immediately flushes accumulated bytes (zero-padded to 40 bytes) in a `DMA_Stream` TLP.

> **Rationale**: Full-buffer flush maximizes transport throughput for continuous telemetry, while the 2-character idle timeout provides ultra-low latency for short interactive serial commands without waiting for buffer completion.

---

## 4. IMU SPI Master & Auto-DMA IP Specification

The AbstractX core includes a dedicated hardware **IMU SPI Master & Auto-DMA Controller**:

```
+-----------------------------------------------------------------------+
|                           IMU Auto-DMA IP                             |
|                                                                       |
|  +-------------------+     +------------------+     +--------------+  |
|  | IMU_INT Hardware  | --> | SPI Master Burst | --> | 64B TLP      |  |
|  | Trigger Pin       |     | Engine (Accel/   |     | Packetizer & |  |
|  +-------------------+     |  Gyro Regs)      |     | Timestamping |  |
|                            +------------------+     +--------------+  |
|                                                                |      |
+----------------------------------------------------------------|------+
                                                                 v
                                                        Host Transport FIFO
```

### 4.1 Theory of Operation
1. External IMU asserts hardware interrupt line (`IMU_INT`).
2. IMU Auto-DMA engine immediately latches the 64-bit system timer (`Timestamp`).
3. Core executes pre-configured SPI Master read burst (e.g., 14-byte Accel + Gyro + Temp burst) over dedicated SPI pins (`IMU_SCLK`, `IMU_CS_N`, `IMU_MOSI`, `IMU_MISO`).
4. Captured sensor bytes are placed in DW5..DW8 of a 64B TLP container.
5. TLP header is formatted (`Type = 0x10`, `Channel = 0x02 (TELEMETRY)`), CRC32 calculated, and pushed to transport egress FIFO.
6. FPGA asserts `INT_REQ` pin to notify host CPU that fresh telemetry is ready.

### 4.2 Manual vs Auto-DMA Modes
- **Manual Mode**: Host can issue `MemWr`/`MemRd` TLPs to control IMU SPI Master registers directly for sensor configuration and setup.
- **Auto-DMA Mode**: Armed via Wishbone control register (`IMU_CTRL.AUTO_DMA_EN = 1`). All subsequent hardware interrupts automatically trigger hardware-driven burst reads and stream generation.

---

## 5. Production Memory Map (AbstractX Wishbone Space)

Wishbone targets map into 32-bit address space accessed via `MemRd` and `MemWr` TLPs:

| Absolute Address | Peripheral Target | Description |
|---|---|---|
| `0x40000000` | `WHO_AM_I` | Read-only System ID & Capability signature |
| `0x40000100` | `IMU_CTRL` | IMU SPI Master control, status, and Auto-DMA config |
| `0x40000104` | `IMU_BURST_ADDR` | Target IMU SPI register start address |
| `0x40000108` | `IMU_BURST_LEN` | Burst read length (in bytes) |
| `0x4000010C` | `IMU_DATA_REG` | Manual SPI TX/RX data register |
| `0x40000300` | `DShot Controller` | Motor command/control registers |
| `0x40000500` | `UART_ESC_CTRL` | UART ESC baud rate & 2-char timeout config |
| `0x40000900` | `ESC_UART_FIFO` | Raw serial tunnel FIFO |
| `0x40000C00` | `LED Controller` | System status LEDs |

---

## 6. Verification & Quality Requirements

- Decoder **MUST** enforce exact 64-byte frame length.
- Decoder **MUST** verify IEEE 802.3 CRC32 before executing `MemWr` or `MemRd`.
- Decoder **MUST** correlate `MemRd` `Tag` into matching `CplD` header.
