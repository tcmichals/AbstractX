# ASP SPI & Dual-SPI Transport Specification (`asp-tlp-64b`)

This document defines the physical and link layer transport specification for carrying 64-byte AbstractX TLPs over SPI and Dual-SPI interfaces.

---

## 1. Physical Layer Wiring & Pinouts

AbstractX supports two physical transport configurations between the Linux/RTOS host and the FPGA slave fabric:

### A) Dual-SPI SDR Profile (Primary High-Throughput Target)

| Host Signal | FPGA Signal | Direction | Description |
|---|---|---|---|
| `SCLK` | `SPI_SCLK` | Host $\rightarrow$ FPGA | Serial SPI Clock (up to 50 MHz) |
| `CSn` | `SPI_CS_N` | Host $\rightarrow$ FPGA | Active-Low Chip Select |
| `IO0` | `SPI_IO0` | Bidirectional | Data Bit 0 (MOSI during host write / MISO 0 during read) |
| `IO1` | `SPI_IO1` | Bidirectional | Data Bit 1 (MISO 1 during read / NC during write) |
| `GPIO_IRQ` | `INT_REQ` | FPGA $\rightarrow$ Host | Egress TLP Ready Doorbell Interrupt |

### B) Single-SPI Standard Profile (Fallback Target)

| Host Signal | FPGA Signal | Direction | Description |
|---|---|---|---|
| `SCLK` | `SPI_SCLK` | Host $\rightarrow$ FPGA | Serial SPI Clock |
| `CSn` | `SPI_CS_N` | Host $\rightarrow$ FPGA | Active-Low Chip Select |
| `MOSI` | `SPI_MOSI` | Host $\rightarrow$ FPGA | Master Output Slave Input |
| `MISO` | `SPI_MISO` | FPGA $\rightarrow$ Host | Master Input Slave Output |
| `GPIO_IRQ` | `INT_REQ` | FPGA $\rightarrow$ Host | Egress TLP Ready Doorbell Interrupt |

### C) SPI Clock Frequency Flexibility (100 kHz to 50 MHz)

The AbstractX FPGA over-samples incoming `SCLK` and `CS_N` using its internal master clock. The host SPI clock (`SCLK`) can run at **ANY frequency from 100 kHz up to 50 MHz** (e.g. **10 MHz**, 12 MHz, 25 MHz, 50 MHz):
- **At 10 MHz SCLK (Standard Single-SPI)**: Transferring a 64-byte TLP packet takes **$5.12\ \mu\text{s}$** ($1.25\text{ MB/s}$ throughput).
- **At 10 MHz SCLK (Dual-SPI Mode)**: Transferring a 64-byte TLP packet takes **$2.56\ \mu\text{s}$** ($2.50\text{ MB/s}$ throughput).

---

## 2. Fixed 64-Byte Burst & Dual-SPI 2x Speed Mechanics

Unlike variable-length protocols, `asp-tlp-64b` operates exclusively on **fixed 64-byte (512-bit) transfer units**:

### 2.1 Dual-SPI 2x Speed & Parallel Bit Mechanics
- **Standard Single-SPI Mode**: Data is transferred 1 bit per SCLK clock pulse over `MOSI`/`MISO`. Transmitting a 64-byte (512-bit) TLP requires **512 SCLK clock cycles**.
- **Dual-SPI Mode (2x Throughput)**: Data is transferred **2 bits in parallel per SCLK clock pulse** over `SDIO0` and `SDIO1` simultaneously:
  - SCLK Pulse 1: Bit 7 on `SDIO1`, Bit 6 on `SDIO0`.
  - SCLK Pulse 2: Bit 5 on `SDIO1`, Bit 4 on `SDIO0`.
  - SCLK Pulse 3: Bit 3 on `SDIO1`, Bit 2 on `SDIO0`.
  - SCLK Pulse 4: Bit 1 on `SDIO1`, Bit 0 on `SDIO0`.
- **200% Bandwidth**: Transmitting a 64-byte TLP takes **only 256 SCLK clock cycles** (exactly half the clock pulses of standard SPI). At a 50 MHz SPI clock, Dual-SPI delivers **100 Megabits per second (12.5 MB/s)** of real PCIe-like register throughput!

- **`CS_N` Framing**: Deassertion of `CS_N` resets the internal 512-bit bit-counter and shift register state machine, ensuring hardware synchronization across packet bursts.

---

## 3. Host-FPGA Command Protocol

Every Dual-SPI burst begins with a 1-byte command phase issued by the host while driving `CS_N` low:

| Command | Name | Description |
|---|---|---|
| `0xA1` | `TLP_WRITE_BURST` | Host transmits a 64-byte TLP (`MemRd`, `MemWr`, or `DMA_Cfg`) to FPGA ingress |
| `0xA2` | `TLP_READ_BURST` | Host reads a 64-byte TLP (`CplD`, `Cpl`, or `DMA_Stream`) from FPGA egress buffer |
| `0xA0` | `TLP_READ_STATUS` | Host queries ingress/egress FIFO watermarks and diagnostic bitfield |

### 3.1 Write Burst Sequence (`TLP_WRITE_BURST` - `0xA1`)
1. Host asserts `CS_N` low.
2. Host outputs command byte `0xA1`.
3. Host clocks out 64 TLP bytes (256 Dual-SPI clock cycles or 512 SPI clock cycles).
4. FPGA shift register ingests TLP, calculates CRC32, and pushes valid TLP to Wishbone/routing gateway.
5. Host deasserts `CS_N` high.

### 3.2 Read Burst Sequence (`TLP_READ_BURST` - `0xA2`)
1. FPGA asserts `INT_REQ` pin high when egress FIFO has $\ge 1$ 64-byte TLP ready.
2. Host asserts `CS_N` low.
3. Host outputs command byte `0xA2`.
4. FPGA clocks out 64 TLP bytes from egress FIFO.
5. Host deasserts `CS_N` high.
6. If egress FIFO becomes empty, FPGA deasserts `INT_REQ` low.

---

## 4. Hardware Shift Register Seam Architecture

The SPI slave module presents a clean 512-bit parallel interface seam to internal fabric routers:

```
                  +-------------------------------+
  SPI Pins        |       asp_spi_frontend        |     AXIS / Wishbone Seam
  --------------> |                               | ----------------------------->
  SCLK, CS_N,     |  512-Bit Fixed Shift Register | tdata[511:0] (64-Byte TLP)
  IO0, IO1        |  Parallel CRC32 Engine        | tvalid, tready, tlast
                  +-------------------------------+
```

### RTL Interface Seam Signals:
- `tdata[511:0]`: Complete 64-byte TLP parallel vector.
- `tvalid`: High when a complete 64-byte packet is received with valid CRC32.
- `tready`: Backpressure signal from Wishbone/router gateway.
