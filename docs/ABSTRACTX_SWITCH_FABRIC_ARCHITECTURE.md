# AbstractX Switch Fabric Architecture (`asp-tlp-64b`)

This document defines the internal hardware architecture for the AbstractX switch fabric operating under the **PCIe-like 64-Byte TLP Profile (`asp-tlp-64b`)**.

---

## 1. Top-Level Topology & Data Flow

External transports (Dual-SPI, Single-SPI, DMA) act as **Translators** that serialize/deserialize fixed 64-byte TLPs between physical pins and the internal AXI-Stream / Wishbone switch fabric.

```
 +-------------------------------------------------------------------------+
 |                         ABSTRACTX SWITCH FABRIC                         |
 |                                                                         |
 |  External Transport             Fabric Hub / Router       Endpoints     |
 |  +------------------+           +------------------+   +--------------+ |
 |  | Dual-SPI / SPI   |  64B TLP  | AXI-Stream Hub   |   | Wishbone     | |
 |  | Translator Core  | --------> | (Channel/AXID    | ->| Master Gateway| |
 |  +------------------+  AXIS     |  Router)         |   +--------------+ |
 |                                 +------------------+   | IMU Auto-DMA | |
 |                                          |             | Core         | |
 |                                          |             +--------------+ |
 |                                          |             | UART ESC     | |
 |                                          v             | DMA Tunnel   | |
 |                                   Egress TLP FIFO      +--------------+ |
 +-------------------------------------------------------------------------+
```

---

## 2. Internal TLP Packet Routing Model

All endpoints and translators exchange data as **512-bit (64-byte) parallel vectors** accompanied by standard AXI-Stream control signals (`tvalid`, `tready`, `tlast`):

```
AXI-Stream TLP Seam:
- tdata[511:0]  : 64-Byte TLP Container Vector
- tvalid        : Valid TLP Beat
- tready        : Endpoint Backpressure
- tuser[63:0]   : Hardware Nanosecond Ingress Timestamp
```

### Channel Routing Mapping (`tdata[23:16]` - `Channel/AXID` Field)

| Channel ID | Endpoint Name | Description |
|---:|---|---|
| `0x01` | `CONTROL` / Wishbone Gateway | Transmits `MemRd` & `MemWr` TLPs to on-chip Wishbone bus targets |
| `0x02` | `TELEMETRY` / IMU Auto-DMA IP | Egress path for timestamped IMU telemetry stream TLPs |
| `0x03` | `FC_LOG` | High-rate flight log stream |
| `0x04` | `DEBUG_TRACE` | Logic analyzer trace stream |
| `0x05` | `ESC_SERIAL` | UART ESC tunnel (2-character idle / 40B full buffer flush) |

---

## 3. Core Endpoints & Gateways

### A. Dual-SPI Slave Translator
- Converts pin-level Dual-SPI clock and data signals into 64-byte parallel vectors.
- Enforces IEEE 802.3 CRC32 checking on incoming TLPs before forwarding to fabric.
- Drives FPGA `INT_REQ` pin high when the egress TLP FIFO contains ready responses (`CplD`, `DMA_Stream`).

### B. Wishbone Master Gateway
- Converts incoming `MemRd` and `MemWr` TLPs into standard Wishbone cycles (`CYC`, `STB`, `WE`, `ADR`, `DAT_I`, `DAT_O`, `ACK`).
- For `MemRd`: Performs Wishbone read cycle(s), builds a matching `CplD` TLP carrying the request's `Tag`, and routes it back to the egress TLP FIFO.

### C. IMU SPI Master & Auto-DMA Core
- Contains a dedicated SPI Master for external IMU sensor ICs.
- Triggered by external hardware interrupt line (`IMU_INT`).
- Instantly captures 64-bit nanosecond timer (`tuser`), executes pre-configured SPI burst read, packages sample into 64-byte `DMA_Stream` TLP, and emits to `Channel = 0x02`.

### D. UART ESC DMA Tunnel Engine
- Implements 2-character idle timeout (~20 bit times) + 40-byte full buffer trigger logic.
- Packages serial RX payload into 64-byte `DMA_Stream` TLP targeting `Channel = 0x05`.

---

## 4. Backpressure & Priority Arbitration

- **Control Priority**: `MemRd` / `MemWr` control TLPs (`Channel = 0x01`) receive highest priority in fabric crossbars to prevent register lockups during heavy telemetry streaming.
- **Valid-Ready Handshaking**: Every internal seam uses standard `tvalid`/`tready` backpressure to prevent packet loss under high host contention.
