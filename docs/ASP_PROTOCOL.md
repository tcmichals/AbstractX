# ASP Protocol Specification (ASP/1)

**ASP** = **AbstractX Switch Protocol**

This document is the **normative** protocol specification for ASP/1 in the AbstractX repository.

> Structure intentionally mirrors the offloader protocol spec blocks to keep migration and review straightforward.

> Canonical transport profile: `ASP_SPI_TRANSPORT.md`.

> Requirements and quality gates: `ASP_REQUIREMENTS.md`.

> Validation matrix and release evidence gates: `ASP_VALIDATION_MATRIX.md`.

> Switch fabric architecture: `ABSTRACTX_SWITCH_FABRIC_ARCHITECTURE.md`.

## 1. Frame Format (Wire Level)

All multi-byte integers are **Big-Endian** on the wire.

For ASP/1 default profile (`asp-compat-v1`), the wire format is:

| Field | Size | Description |
|---|---:|---|
| `sync` | 1 | `0xA5` |
| `version` | 1 | Protocol version, currently `0x01` |
| `flags` | 1 | Bitfield (`0x01` = ACK request; other bits reserved unless documented) |
| `axid` | 1 | Logical routing ID |
| `seq` | 2 | Sequence number for request/response correlation |
| `payload_len` | 2 | Payload byte length (project profile cap currently 512) |
| `payload` | N | Data bytes |
| `crc16` | 2 | CRC16/XMODEM over `version..payload` |

### Normative requirements

- An encoder **MUST** emit fields in the order shown above.
- A decoder **MUST** parse by exact field widths above.
- A decoder **MUST** reject frames with invalid CRC.
- A decoder **MUST** enforce local payload limits and reject oversized payloads.
- On parse failure, a decoder **MUST** resynchronize by scanning for the next `sync` candidate byte.

SPI integration note:

- In this repository, SPI slave integration **SHOULD** use the stream-seam profile defined in `ASP_SPI_TRANSPORT.md` ("SPI slave stream-interface profile").
- Protocol blocks **SHOULD** operate on stream-seam bytes rather than direct pin-level SPI signals.
- AbstractX vChip control/read-write behavior **SHOULD** follow `ASP_SPI_TRANSPORT.md` ("AbstractX vChip SPI control profile").

---

## 2. AXID Architecture

ASP routes frames by `axid` into deterministic hardware/software planes.

| AXID | Name | Current role |
|---|---|---|
| `0x01` | CONTROL | Register/control operations (`READ_BLOCK`, `WRITE_BLOCK`) |
| `0x02` | TELEMETRY | Reserved telemetry stream |
| `0x03` | FC_LOG | Reserved logging stream |
| `0x04` | DEBUG_TRACE | Debug trace egress |
| `0x05` | ESC_SERIAL | Raw serial tunnel (ESC/4-way/MSP payload cargo) |
| `0x07` | ILA_TRACE | Optional compressed trace stream |

Unknown AXIDs:

- Implementations **MAY** drop unknown AXIDs.
- Implementations **SHOULD** account for unknown-route drops in diagnostics counters.

### 0x01: CONTROL plane

CONTROL carries memory-mapped control semantics.

#### A. `WRITE_BLOCK` (`0x11`)

- `op_id`: `0x11`
- `address`: 32-bit target address
- `data`: payload to write (single or multiple 32-bit words)

#### B. `READ_BLOCK` (`0x10`)

- `op_id`: `0x10`
- `address`: 32-bit target address
- Response payload returns register data in implementation-defined response encoding.

CONTROL requirements:

- CONTROL handling **MUST** be deterministic under backpressure.
- A response path **SHOULD** preserve request `seq` for correlation.
- CONTROL writes may arm autonomous peripheral streaming modes via Wishbone-backed control registers.

### 0x05: STREAM plane (ESC serial bridge)

This AXID is a raw byte tunnel.

- Payload bytes **MUST** be forwarded in-order to the bound serial engine.
- Receive bytes **MUST** be packetized and returned in ASP frames with `axid=0x05`.

## 2.1 Internal AbstractX fabric packet routing model

ASP external transport may be translated into internal fabric packets before endpoint execution.

Baseline internal routing header:

| Field | Size | Description |
|---|---:|---|
| `target_id` | 4 bits | destination endpoint class |
| `command` | 4 bits | operation type |
| `sub_id_or_addr` | 16 bits | endpoint sub-ID or address window |
| `payload` | N bytes | operation payload |

Normative integration rule:

- Transport-facing modules **MAY** translate ASP payloads to internal fabric packets.
- Fabric arbitration/backpressure behavior **MUST** remain deterministic.
- Endpoint interconnect policy details are defined in `ABSTRACTX_SWITCH_FABRIC_ARCHITECTURE.md`.

AXIS sideband propagation rule:

- Implementations should treat timestamp fields as part of the AXIS sideband/tag bundle.
- Profiles may carry two timestamp fields in the tag bundle: `ts_ingress` (capture/arrival marker) and `ts_egress` (transport emission marker).
- Tag propagation should remain coherent with frame boundaries and routing context (no cross-frame tag mixing).
- When configured, transport adapters may inject timestamp/tag metadata into protocol-visible payload metadata.

---

## 3. Production Memory Map (AbstractX profile)

Address map in this repository is currently aligned with the validated offloader map for migration stability.

| Absolute Address | Peripheral | Description |
|---|---|---|
| `0x40000000` | WHO_AM_I | Read-only ID register |
| `0x40000100` | PWM Decoder | Pulse measurement registers |
| `0x40000300` | DShot Controller | Raw motor command/control registers |
| `0x40000400` | Serial/DShot Mux | Mode/channel/break control |
| `0x40000600` | NeoPixel | Pixel buffer + trigger |
| `0x40000900` | ESC UART | TX/RX/status/baud registers |
| `0x40000C00` | LED Controller | LED set/clear/toggle/read |

Memory-map requirements:

- `READ_BLOCK`/`WRITE_BLOCK` semantics **MUST** remain deterministic.
- Unmapped accesses **SHOULD** return a safe non-hanging result (for example ACK with zero data) where applicable.

### 3.1 Generic AbstractX-to-Wishbone conversion

When a translated internal AbstractX fabric packet targets the Wishbone gateway:

- Gateway **MUST** convert it to a standard Wishbone transaction shape.
- Fabric-level routing **MUST NOT** depend on transport-specific details (SPI vs other translators).
- Register access behavior should remain stable regardless of ingress transport.

This is a core next-gen ASP goal: keep the Wishbone path generic and reusable beyond legacy protocol-specific integration patterns.

### 3.2 Autonomous stream control profile (recommended)

For stream-capable peripherals, a recommended Wishbone control model includes:

- `STREAM_ENABLE`: enable autonomous producer state.
- `AUTO_TRIGGER_ENABLE`: allow threshold/timer/event driven production.
- `TRIGGER_PERIOD`: optional timer period for periodic sampling/stream emission.

Exact register offsets are peripheral-specific and should be exposed via capability/discovery metadata.

---

## 4. Theory of Operation: Legacy MSP-over-ASP Migration

Migration strategy mirrors offloader behavior while using AbstractX/ASP naming.

1. **Cargo (MSP)**: Host tooling generates MSP or 4-way payload bytes.
2. **Carrier (ASP)**: Host wraps bytes in ASP frame with `axid=0x05`.
3. **Extraction**: Router delivers bytes to serial tunnel path.
4. **Execution**: Hardware/software applies passthrough/control logic.
5. **Control writes**: Host issues `axid=0x01` CONTROL operations for deterministic register updates.

Migration requirements:

- Critical workflows **MUST** preserve user-visible behavior parity.
- Error/result behavior **MUST** remain deterministic and documented.

---

## 5. Transport Profile (SPI)

ASP over SPI follows byte-stream transport semantics.

- SPI burst boundaries **MUST NOT** be treated as ASP frame boundaries.
- A burst **MAY** include partial, single, or multiple ASP frames.
- Command and background traffic **MAY** be interleaved.

Pad-byte rule:

- Transport pad/dummy bytes are shim behavior only and **MUST NOT** alter ASP frame semantics.

Frame length equation:

$$
\text{frame\_len} = 1 + 1 + 1 + 1 + 2 + 2 + \text{payload\_len} + 2
$$

At `payload_len = 512`, max frame size is 522 bytes.

### 5.1 SPI command protocol binding (external SPI profile)

Canonical byte/register contract: `ASP_SPI_REGISTER_MAP.md`.

For the external Pi Zero 2W -> FPGA profile, the following command binding is normative:

| Command | Name | Meaning |
|---|---|---|
| `0x80` | `ASP_CMD_WRITE_DATA` | Host writes ASP payload bytes to FPGA ingress |
| `0x01` | `ASP_CMD_READ_STATUS` | Host reads readiness/length metadata |
| `0x02` | `ASP_CMD_READ_DATA` | Host clocks out queued ASP payload bytes |

Normative flow:

1. FPGA asserts `INT_REQ` when egress payload is available.
2. Host issues `ASP_CMD_READ_STATUS` to obtain `RX_LEN`.
3. Host issues `ASP_CMD_READ_DATA` and clocks exactly `RX_LEN` bytes.
4. FPGA deasserts `INT_REQ` when egress queue is empty (or below implementation threshold).

Safety requirements:

- Host **MUST** bound read size by returned `RX_LEN`.
- FPGA **MUST** keep length metadata coherent with readable queued bytes.
- ASP frame validation (version/length/CRC) **SHOULD** occur before forwarding to `tun0`.

---

## 6. Discovery and Capability Requirements

ASP capability discovery requirements:

- `HELLO` **MUST** be supported.
- `GET_CAPS` **MUST** be supported.
- Capability payload **SHOULD** include:
  - protocol profile (`asp-compat-v1` or `asp-native`),
  - supported AXID set,
  - transport availability,
  - version/capability feature bits.

---

## 7. Compatibility and Profile Direction

ASP currently defines:

- `asp-compat-v1` (**default**): wire-compatible migration profile.
- `asp-native` (**future**): AbstractX-native envelope/profile.

Profile direction requirements:

- New deployments **SHOULD** start on `asp-compat-v1` unless both endpoints explicitly negotiate `asp-native`.
- Default switch to `asp-native` **MUST NOT** occur until compatibility and regression gates pass.

---

*Revision: 1.0 (Apr 2026)*
