# ASP SPI & Dual-SPI Register Map and Wire Contract (`asp-tlp-64b`)

This document defines the physical command, status byte map, and register interface for the **PCIe-like 64-Byte TLP Profile (`asp-tlp-64b`)**.

---

## 1. Command Byte Map (Dual-SPI & SPI Mode)

Every SPI transfer begins with a 1-byte command phase issued by the host while asserting `CS_N` low:

| Command Byte | Symbol | Direction | Function |
|---|---|---|---|
| `0xA1` | `TLP_WRITE_BURST` | Host $\rightarrow$ FPGA | Clock in 64-byte TLP (`MemRd`, `MemWr`, or `DMA_Cfg`) to FPGA ingress |
| `0xA2` | `TLP_READ_BURST` | Host $\leftarrow$ FPGA | Clock out 64-byte TLP (`CplD`, `Cpl`, or `DMA_Stream`) from FPGA egress buffer |
| `0xA0` | `TLP_READ_STATUS` | Host $\leftarrow$ FPGA | Query 4-byte status vector (FIFO levels & diagnostic flags) |

---

## 2. `TLP_READ_STATUS` Response Vector (4 Bytes)

When the host issues `0xA0`, the FPGA returns a 4-byte status payload:

| Response Byte | Field Name | Description |
|---:|---|---|
| Byte 0 | `VERSION` | Protocol revision (`0x64` for 64B TLP profile) |
| Byte 1 | `STATUS_FLAGS` | Bit 0: `EGRESS_READY` ($\ge 1$ 64B TLP ready)<br>Bit 1: `INGRESS_ACCEPT` (Ingress FIFO can accept write)<br>Bit 2: `CRC_ERROR` (Sticky CRC failure flag)<br>Bit 3: `IMU_AUTO_DMA_ACTIVE` (IMU telemetry active) |
| Byte 2 | `EGRESS_COUNT` | Number of 64-byte TLPs currently queued in egress FIFO |
| Byte 3 | `INGRESS_SPACE` | Free 64-byte TLP slot count in ingress FIFO |

---

## 3. Wishbone Base Address Map (Target Address Field in TLP)

All Wishbone registers are targeted by `MemRd` (Type `0x01`) and `MemWr` (Type `0x02`) TLPs using the 32-bit `Target Address` field in the TLP header:

```
+------------------+-----------------------------------------------+
| Address Range    | Peripheral Target                             |
+------------------+-----------------------------------------------+
| 0x40000000       | WHO_AM_I System ID & Capability Signature     |
| 0x40000100       | IMU SPI Master & Auto-DMA IP Core Registers   |
| 0x40000300       | DShot Motor Controller                        |
| 0x40000500       | UART ESC Engine & 2-Char Timeout Register     |
| 0x40000900       | UART ESC Raw Serial Tunnel FIFO               |
| 0x40000C00       | GPIO & LED Controller                         |
+------------------+-----------------------------------------------+
```

### Detailed Peripheral Registers

#### A. System Identification (`0x40000000`)
- `0x40000000`: `WHO_AM_I` (Read-Only: returns `0xAB1X6401` - AbstractX 64B TLP Core v1)

#### B. IMU Auto-DMA Core (`0x40000100` - `0x40000118`)
- `0x40000100`: `IMU_CTRL` (Control register: `AUTO_DMA_EN`, `MANUAL_START`, `SPI_MODE`)
- `0x40000104`: `IMU_BURST_ADDR` (Target IMU SPI register address, e.g. `0x1F`)
- `0x40000108`: `IMU_BURST_LEN` (Burst byte count, e.g. 14 bytes)
- `0x4000010C`: `IMU_MAN_DATA` (Manual SPI data transmit/receive buffer)
- `0x40000110`: `IMU_STATUS` (Status register: sample counter, busy flag)
- `0x40000114`: `IMU_LATCH_TS_H` (High 32 bits of latched 64-bit timestamp)
- `0x40000118`: `IMU_LATCH_TS_L` (Low 32 bits of latched 64-bit timestamp)

#### C. UART ESC Engine (`0x40000500` - `0x40000508`)
- `0x40000500`: `UART_ESC_CTRL` (Bit 0: Enable, Bit 1: 2-Char Idle Timer Enable)
- `0x40000504`: `UART_BAUD_DIV` (Baud rate clock prescaler divisor)
- `0x40000508`: `UART_IDLE_CYCLES` (Bit-time idle counter limit for flush trigger, default 20)

---

## 4. Doorbell Interrupt Signalling (`INT_REQ`)

The FPGA asserts `INT_REQ` high whenever `EGRESS_COUNT > 0`. Host IRQ handler responds by reading 64-byte `CplD` or `DMA_Stream` TLPs using `0xA2 (TLP_READ_BURST)`.
