# ASP Specification Direction

**ASP** = **AbstractX Switch Protocol**

This document defines protocol direction for AbstractX, specifying the **PCIe-like 64-byte Fixed TLP Architecture (`asp-tlp-64b`)** as the primary normative profile for hardware switch fabric implementations, with legacy `asp-compat-v1` retained for reference.

---

## 1) Architecture Strategy and Objectives

AbstractX has pivoted from legacy dynamic variable-length byte-stream framing to a fixed 64-byte PCIe-like Transaction Layer Packet (TLP) architecture.

### Primary Objectives:
1. **Ultra-Low FPGA Logic Footprint**: Eliminates dynamic byte-boundary logic, dynamic CRC framing state machines, and dynamic payload counters. Shift registers and FIFOs operate on static 512-bit (64-byte) containers, minimizing LUT/FF consumption on Gowin (Tang Nano 9K / Primer 20K) and QMTECH Zynq PL.
2. **Unified Data & Control Plane**: Memory read/writes (`MemRd`/`MemWr` to Wishbone bus targets), autonomous sensor telemetry (IMU auto-DMA), and serial streaming (UART ESC tunnel) use the *exact same* 64-byte TLP envelope.
3. **Split-Transaction Memory Model**: PCIe-style `Tag` tracking enables non-blocking, pipelined register accesses over SPI/Dual-SPI links.
4. **Hardware Timestamping**: Integrated 64-bit nanosecond hardware timestamps embedded directly into TLP headers for sub-microsecond telemetry jitter analysis.

---

## 2) ASP Wire Profile Strategy

### A) `asp-tlp-64b` (Primary Normative Profile)

All transactions over SPI / Dual-SPI / DMA transport take the form of a fixed 64-byte (16 x 32-bit DWORD) TLP container:

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
Total: 64 Bytes (512 Bits / 16 DWORDs)
```

#### TLP Type Classifications

| Type | Name | Direction | Role |
|---|---|---|---|
| `0x01` | `MemRd` | Host $\rightarrow$ FPGA | Read request targeting Wishbone `Address` for `Length` DWs. Response emitted via `CplD` with matching `Tag`. |
| `0x02` | `MemWr` | Host $\rightarrow$ FPGA | Posted Write operation targeting Wishbone `Address` with up to 40B data payload (10 DWs). |
| `0x03` | `CplD` | FPGA $\rightarrow$ Host | Completion with Data in response to `MemRd`. Contains requested register data and `Tag`. |
| `0x04` | `Cpl` | FPGA $\rightarrow$ Host | Completion status (ACK/NACK) for posted operations. |
| `0x10` | `DMA_Stream` | FPGA $\rightarrow$ Host | Autonomous stream TLP (IMU auto-DMA, UART ESC tunnel) with hardware 64-bit timestamp. |
| `0x11` | `DMA_Cfg` | Host $\rightarrow$ FPGA | Configuration TLP for autonomous streaming and IMU auto-DMA registers. |

### B) `asp-compat-v1` (Legacy Profile)
Historical variable-length frame format (`0xA5` sync, dynamic dynamic byte length, CRC16). Retained solely for backwards migration reference.

---

## 3) AXID / Channel Routing Model

AbstractX routes packets by `Channel/AXID` into hardware execution planes:

| AXID | Name | Target Engine | Role |
|---|---|---|---|
| `0x01` | `CONTROL` | Wishbone Gateway | Host register access (`MemRd`, `MemWr`) |
| `0x02` | `TELEMETRY` | IMU Auto-DMA IP | Autonomous sensor telemetry streams |
| `0x03` | `FC_LOG` | Flight Log FIFO | High-rate internal log stream |
| `0x04` | `DEBUG_TRACE` | Logic Analyzer | Debug trace egress |
| `0x05` | `ESC_SERIAL` | UART ESC Engine | Serial tunnel (2-character idle / 40B full buffer flush) |
| `0x07` | `ILA_TRACE` | Embedded Scope | Logic trace data |

---

## 4) Transport Layer Profile (Dual-SPI & SPI)

AbstractX specifies Dual-SPI SDR mode (`SCLK`, `CS_N`, `IO0`, `IO1` bidirectional) as the primary external link:

- **Burst Unit**: Exactly 64 bytes (256 `SCLK` cycles under Dual-SPI; 512 `SCLK` cycles under Single-SPI).
- **No Framing Delimiters**: Fixed packet size eliminates byte scanning and dynamic sync acquisition.
- **Interrupt Signalling (`INT_REQ`)**: FPGA asserts `INT_REQ` when the egress FIFO contains $\ge 1$ 64-byte TLP ready for host clocking.

---

## 5) IMU Auto-DMA IP Core Integration

AbstractX includes a hardware **IMU SPI Master & Auto-DMA Engine**:
- **SPI Master Engine**: Communicates directly with external IMU sensor ICs (e.g. ICM-42688, BMI088).
- **Hardware Interrupt Trigger (`IMU_INT`)**: External interrupt pin on IMU hardware triggers an automated burst-read sequence in hardware without host CPU involvement.
- **Timestamp Capture**: Latches 64-bit nanosecond timer on `IMU_INT` assertion.
- **TLP Packetization**: Automatically packages raw sensor bytes into a 64-byte `DMA_Stream` TLP (`Type=0x10`, `Channel=0x02`) and enqueues to transport FIFO.

---

## 6) Source-of-Truth Note

This document is the official architectural direction reference for AbstractX. Implementation in `rtl/` and software drivers in `python/` and `rust/` must conform to the 64-byte TLP standard defined herein.
