# ASP SPI Transport Profile (ASP/1)

This document defines how ASP frames are carried over SPI for the AbstractX host↔fabric link.

## Why this exists

SPI is synchronous: bytes are clocked in/out with no native packet boundaries.

ASP therefore treats SPI as a byte stream and provides packet framing at the protocol layer.

## Pro Performance deployment profile (current target)

Current target profile is:

- Host: **Linux SPI master**
- Device: **FPGA SPI slave**
- Data path: **SPI only (no USB data path)**

Bring-up note:

- A **Pi Zero** is a practical host for testing and early bring-up.

Typical physical mapping:

| Example Linux SPI host | Signal | FPGA side |
|---|---|---|
| GPIO10 | MOSI | SPI slave data-in |
| GPIO9 | MISO | SPI slave data-out |
| GPIO11 | SCLK | SPI clock |
| GPIO8 | CE0 (CSn) | SPI chip select |
| Any input GPIO | `INT_REQ` | FPGA IRQ/doorbell output |

Operating guidance:

- Baseline deterministic operating point: **5 Mbps**.
- Higher rates are allowed with validated timing/signal integrity.
- Keep interconnect short and provide robust shared ground references.

## Transport model

- Link type: SPI slave/device receives bursts from SPI master/host.
- Unit on wire: bytes clocked synchronously with SCLK.
- ASP framing: independent of SPI burst boundaries.

Meaning:

- one SPI burst may contain a partial ASP frame,
- one burst may contain exactly one frame,
- one burst may contain multiple concatenated frames,
- command and background traffic (telemetry/log/debug) may be interleaved in the same stream.

ASP-over-SPI is therefore **streaming/multiplexed**, not send-one-wait-one:

- do not assume a request must be immediately followed by its reply on wire,
- use AXID/sequence/context to correlate transactions,
- continue parsing and routing all frames as they arrive.

## SPI slave stream-interface profile

The SPI slave is treated as a transport shim that converts pin-level signaling into stream-seam transactions.

### Interface seam

Implementations **SHOULD** expose a stream-style seam between SPI pins and protocol parser/framer logic, for example:

- RX seam (SPI -> protocol):
	- `rx_data[7:0]`
	- `rx_valid`
	- `rx_ready`
- TX seam (protocol -> SPI):
	- `tx_data[7:0]`
	- `tx_valid`
	- `tx_ready`

Equivalent AXIS-style byte-stream signals are acceptable if behavior is equivalent.

### Behavioral rules

- Pin-level SPI details **MUST** be isolated from protocol parser logic through this seam.
- RX data **MUST** preserve byte order from wire to parser input.
- TX path **MUST** respect backpressure (`tx_ready`) without corrupting frame byte order.
- If TX data is unavailable while SPI clocks continue, the transport layer **MAY** emit pad bytes as defined in this profile.
- Pad-byte behavior **MUST NOT** modify ASP frame semantics.

## AbstractX vChip SPI control profile

This profile models the FPGA as an AbstractX virtual network chip on SPI.

Concrete byte/register layout is defined in `ASP_SPI_REGISTER_MAP.md`.

### Required external signaling

- `INT_REQ` (FPGA -> host GPIO interrupt line) **SHOULD** be implemented for asynchronous RX readiness when provided by the selected peripheral/profile.
- SPI data plane uses host-driven clocks; the FPGA cannot transmit unless clocked by the host.

DMA autonomy note:

- Streaming DMA paths may support auto-trigger operation from internal thresholds/events (for example, fill watermark, wrap, fault) independent of per-transfer host polling, depending on peripheral capabilities.

### Command byte set (host -> FPGA)

| Command | Name | Purpose |
|---|---|---|
| `0x80` | `ASP_CMD_WRITE_DATA` | Host writes one ASP frame payload into FPGA ingress path |
| `0x01` | `ASP_CMD_READ_STATUS` | Host reads status/length metadata |
| `0x02` | `ASP_CMD_READ_DATA` | Host clocks out ASP payload bytes from FPGA egress buffer |

### Status/register semantics (logical profile)

| Field/Register | Purpose |
|---|---|
| `ASP_REG_VERSION` | protocol/profile version reporting |
| `ASP_REG_RX_LEN` | number of bytes ready for host read in egress buffer |
| `ASP_REG_STATUS` | ready/error bits (implementation-specific layout) |

Equivalent implementations may encode these fields in fixed status response bytes as long as behavior is equivalent.

### Two-phase read flow (IRQ + length-first)

Phase 1 (status):

1. FPGA asserts `INT_REQ` when egress payload is available.
2. Host issues `ASP_CMD_READ_STATUS`.
3. FPGA returns readiness + `ASP_REG_RX_LEN`.

Phase 2 (payload):

1. Host issues `ASP_CMD_READ_DATA`.
2. Host clocks exactly `RX_LEN` bytes.
3. FPGA de-queues those bytes from egress buffer.
4. FPGA deasserts `INT_REQ` when buffer is empty (or below implementation threshold).

Normative behavior:

- Host **MUST** bound read length by returned `RX_LEN`.
- FPGA **MUST** keep `RX_LEN` coherent with bytes available to `READ_DATA`.
- SPI chip-select edges **SHOULD** reset command/bit alignment state for deterministic transaction framing.
- ASP CRC/version checks **SHOULD** be validated before forwarding to the Linux network stack.

## RX behavior requirements

Receiver/parser behavior:

1. **MUST** scan byte stream for sync (`0xA5` in `asp-compat-v1`).
2. **MUST** parse fixed header (`version, flags, axid, seq, payload_len`).
3. **MUST** enforce implementation payload cap (current profile cap: 512).
4. **MUST** wait for full `payload + crc16` before validation.
5. **MUST** verify CRC16/XMODEM over `version..payload`.
6. On failure, **MUST** advance and resynchronize.

## TX behavior requirements

- TX path **MUST** emit complete ASP frames as byte stream.
- Bursting policy **SHOULD** prefer complete-frame batching for efficiency.
- CONTROL traffic (`axid=0x01`) **SHOULD** receive highest scheduler/mux priority where applicable.

## Dummy/pad byte behavior (implementation detail)

Because SPI is full-duplex and synchronous, the master must keep clocking bytes to receive reply bytes.

Implementations **MAY** use dummy/pad bytes during SPI bursts to:

- prime internal bus transactions,
- continue clocking while waiting for response bytes,
- flush/align transport-shim FIFOs.

Scope rule:

- Dummy/pad bytes are transport-shim behavior, not ASP wire semantics.
- ASP frame definition remains unchanged and **MUST NOT** require any fixed pad-byte value.

## Framing and buffering profile limits

For `asp-compat-v1`:

- `payload_len` field: `u16` wire capability,
- project profile cap: 512 bytes payload,
- frame length:

$$
\text{frame\_len} = 1 + 1 + 1 + 1 + 2 + 2 + \text{payload\_len} + 2
$$

With `payload_len=512`, max frame size is 522 bytes.

## Mode and signal expectations

Unless board-specific constraints require otherwise, endpoints **SHOULD** use one fixed SPI mode consistently (recommended: Mode 0).

Regardless of chosen mode, ASP semantics do not change.

## Error/accounting guidance

Implementations **SHOULD** maintain counters for:

- CRC failures,
- length-limit rejects,
- sync-loss/resync events,
- FIFO overflow/drop events,
- unknown-AXID drops.

Counters **SHOULD** be exposed through CONTROL diagnostics and/or capability status responses.

## Ownership split reminder

- RTL fast path: SPI byte ingest, parser, CRC, routing, FIFO handling.
- Control plane software/firmware: validated frame handling, op dispatch, policy/result codes.

Control-plane code **SHOULD NOT** parse raw SPI bytes in the hot path.

---

*Revision: 1.0 (Apr 2026)*
