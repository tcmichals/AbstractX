# ASP SPI Register Map and Wire Contract (vChip Profile)

This document defines the byte-level command/register contract for the AbstractX vChip SPI profile.

## Scope

Applies to the external profile:

- Host: Pi Zero 2W native SPI master
- Device: FPGA SPI slave (Tang Nano 9K class)
- Data path: SPI-only (no USB data path)

This contract complements:

- `ASP_PROTOCOL.md` (ASP frame semantics)
- `ASP_SPI_TRANSPORT.md` (transport and flow requirements)

---

## 1) Command byte map

| Command | Symbol | Direction | Purpose |
|---|---|---|---|
| `0x80` | `ASP_CMD_WRITE_DATA` | Host -> FPGA | Write one ASP payload/frame into FPGA ingress path |
| `0x01` | `ASP_CMD_READ_STATUS` | Host <- FPGA | Read status/length metadata |
| `0x02` | `ASP_CMD_READ_DATA` | Host <- FPGA | Read queued ASP payload bytes from FPGA egress |

Command interpretation rule:

- First command byte after CS assert **MUST** define transaction mode.
- CS deassert **SHOULD** reset command/bit-alignment state.

---

## 2) Logical status/register fields

| Field | Width | Description |
|---|---:|---|
| `ASP_REG_VERSION` | 8 | SPI profile/interface revision |
| `ASP_REG_STATUS` | 8 | Ready/error/flow flags |
| `ASP_REG_RX_LEN` | 16 | Number of bytes available for `READ_DATA` |

### `ASP_REG_STATUS` bit assignment (recommended baseline)

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `RX_READY` | 1 when at least one payload byte is available |
| 1 | `RX_OVERFLOW` | Sticky overflow indication in egress staging/FIFO |
| 2 | `CRC_ERR` | Sticky indication of recently dropped bad-CRC frame |
| 3 | `LEN_ERR` | Sticky indication of recently dropped bad-length frame |
| 4 | `TX_ACCEPT` | 1 when ingress path can accept `WRITE_DATA` payload bytes |
| 5 | `RESYNC_EVT` | Sticky indication of parser resync event |
| 6 | Reserved | Read as 0 |
| 7 | Reserved | Read as 0 |

Status bits are implementation-defined beyond this baseline; if changed, the capability payload **MUST** advertise updated layout/version.

### Optional status extensions (peripheral streaming profiles)

Implementations may expose additional status bits/registers such as:

- `STREAM_ACTIVE`: peripheral currently emitting autonomous stream payloads.
- `AUTO_TRIGGER_ACTIVE`: timer/threshold/event trigger path armed.
- `TRIGGER_MISSED`: trigger occurred while destination path was backpressured/full.

These are profile/peripheral-specific and should be advertised through capabilities.

Optional timestamp capability notes:

- `TS_INGRESS_VALID`: latest egress payload metadata includes ingress timestamp field.
- `TS_EGRESS_VALID`: latest egress payload metadata includes egress timestamp field.

When dual-timestamp mode is enabled by profile, metadata consumers should interpret:

- `ts_ingress` as frame/sample capture-arrival marker,
- `ts_egress` as transport emission marker.

---

## 3) `READ_STATUS` response layout

### Transaction form

- MOSI: command byte + dummy bytes
- MISO: status packet bytes

### Baseline fixed response (4 bytes after command)

| Response Byte | Field |
|---:|---|
| B0 | `ASP_REG_VERSION` |
| B1 | `ASP_REG_STATUS` |
| B2 | `ASP_REG_RX_LEN[15:8]` |
| B3 | `ASP_REG_RX_LEN[7:0]` |

Normative behavior:

- Host **MUST** treat `RX_LEN` as authoritative upper bound for immediate `READ_DATA`.
- FPGA **MUST** present coherent `RX_LEN` for bytes readable in the next `READ_DATA` transaction.

---

## 4) `READ_DATA` transaction

### Request

- Host issues `ASP_CMD_READ_DATA` then clocks N dummy bytes.

### Response

- FPGA returns N data bytes dequeued from egress buffer.

Normative behavior:

- Host **MUST** set `N <= RX_LEN` from the latest successful `READ_STATUS`.
- FPGA **MUST NOT** reorder bytes within a queued ASP frame.
- If host clocks beyond available bytes (protocol misuse), FPGA **MAY** return pad bytes and set error/status indication.

---

## 5) `WRITE_DATA` transaction

### Request

- Host issues `ASP_CMD_WRITE_DATA` followed by payload bytes.

### Effect

- FPGA enqueues bytes into ingress staging/FIFO for ASP parser path.

Normative behavior:

- Host **SHOULD** only write when `TX_ACCEPT=1`.
- FPGA **SHOULD** provide backpressure-safe staging and set overflow/error indicators on violation.

---

## 6) IRQ (`INT_REQ`) behavior

`INT_REQ` is the asynchronous doorbell from FPGA to host.

Recommended behavior:

1. Assert when `RX_LEN > 0`.
2. Keep asserted while readable payload remains.
3. Deassert when egress queue is empty (or below configured threshold).

Normative behavior:

- Host IRQ handler **SHOULD** issue `READ_STATUS` first, then `READ_DATA` for exact length.
- IRQ signaling **MUST** not be required to parse framing; it is readiness signaling only.

---

## 7) Example host flows

### FPGA -> Host (read path)

1. `INT_REQ` rises.
2. Host sends `ASP_CMD_READ_STATUS` and receives `[VER, STATUS, LEN_H, LEN_L]`.
3. Host computes `N = RX_LEN`.
4. Host sends `ASP_CMD_READ_DATA` and clocks exactly `N` bytes.
5. Host validates ASP version/length/CRC before writing to `tun0`.

### Host -> FPGA (write path)

1. Host checks status (`TX_ACCEPT`).
2. Host sends `ASP_CMD_WRITE_DATA` + ASP bytes.
3. FPGA ingests bytes into parser ingress path.

---

## 8) 4K packet profile note

A practical profile for large packets may set payload targets around 4096 bytes at transport level while preserving ASP framing semantics.

For deterministic behavior:

- status/length phase **SHOULD** always precede large reads,
- host **SHOULD** avoid overclocking beyond validated signal-integrity limits,
- software **SHOULD** discard payloads failing ASP CRC/version/length checks.

## 9) Suggested Wishbone-side stream trigger controls (peripheral profile)

Although this SPI register-map focuses on host transport commands, autonomous peripheral streaming is typically configured through Wishbone registers routed by the AbstractX fabric.

Suggested control fields:

- `STREAM_ENABLE` (RW): master enable for autonomous streaming.
- `AUTO_TRIGGER_ENABLE` (RW): enable timer/event threshold triggered emission.
- `TRIGGER_PERIOD` (RW): periodic trigger interval (implementation clock domain units).
- `STREAM_STATUS` (RO): active/backpressure/missed-trigger indicators.

---

*Revision: 1.0 (Apr 2026)*
