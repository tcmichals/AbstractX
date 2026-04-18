# ASP Specification Direction

**ASP** = **AbstractX Switch Protocol**

This document defines protocol direction for AbstractX as the next-generation ASP evolution with explicit legacy compatibility profiles.

## 1) Lineage and naming

- **Legacy v1 wire behavior**: proven protocol and implementation baseline from `rt-fc-offloader`
- **ASP**: AbstractX-era protocol family for a transport-agnostic hardware switch fabric

Design intent:

1. Keep deterministic behavior and implementation simplicity from the legacy v1 baseline.
2. Preserve migration feasibility for host tooling and simulation assets.
3. Expand toward transport-agnostic routing and capability discovery.

## 2) ASP wire-profile strategy

ASP defines two wire profiles:

### A) `asp-compat-v1` (default now)

This profile is wire-compatible with legacy v1 behavior and is the recommended default for bring-up and migration.

| Field | Size | Notes |
|---|---:|---|
| `sync` | 1 | `0xA5` |
| `version` | 1 | `0x01` |
| `flags` | 1 | ACK and metadata bits |
| `axid` | 1 | Equivalent to legacy channel ID |
| `seq` | 2 | Request/response correlation |
| `payload_len` | 2 | Project cap currently 512 |
| `payload` | N | Transported bytes |
| `crc16` | 2 | CRC16/XMODEM over `version..payload` |

### B) `asp-native` (future profile)

A future profile for an AbstractX-specific envelope (for example, an explicit magic field such as `0xAB1X`) is allowed, but it is **not** the default until migration gates pass and host adapters support it.

## 3) AXID model (initial)

Initial AXID assignments track legacy v1 semantics to minimize migration risk:

| AXID | Name | Role |
|---|---|---|
| `0x01` | CONTROL | Register/control operations (`READ_BLOCK`, `WRITE_BLOCK`) |
| `0x02` | TELEMETRY | Reserved/stream telemetry |
| `0x03` | FC_LOG | Reserved/log stream |
| `0x04` | DEBUG_TRACE | Debug trace egress |
| `0x05` | ESC_SERIAL | Raw serial tunnel (e.g., ESC/4-way/MSP payload cargo) |
| `0x07` | ILA_TRACE | Optional compressed logic trace |

## 4) Transport profile requirements (SPI)

ASP-over-SPI follows legacy v1 transport semantics:

- SPI is treated as a **byte stream**, not a packet boundary mechanism.
- A burst may contain partial, single, or multiple concatenated ASP frames.
- Command and background traffic may interleave.
- Parser must resync by scanning for sync and validating header/CRC.

Pad/dummy byte rule:

- Pad bytes are transport-shim behavior only.
- Pad bytes are **not** part of ASP wire semantics.

Framing math for compat profile:

$$
\text{frame\_len} = 1 + 1 + 1 + 1 + 2 + 2 + \text{payload\_len} + 2
$$

With payload cap 512, max frame size is 522 bytes.

## 5) Discovery and capabilities

AbstractX adopts deterministic discovery requirements:

- Required ops: `HELLO` and `GET_CAPS`
- Capability response should include protocol profile (`asp-compat-v1` or `asp-native`), feature bits, and transport availability.

## 6) Migration policy from legacy v1

ASP migration should preserve behavior parity while renaming and expanding protocol semantics.

Minimum policy:

1. Keep host-visible behavior equivalent for critical workflows.
2. Preserve deterministic parser/CRC/router/FIFO behavior.
3. Reuse legacy simulation assets where possible under ASP naming.

Compatibility alias note:

- Historical tooling may use deprecated profile aliases; these should be treated as aliases of `asp-compat-v1` during migration.
4. Switch default profile to `asp-native` only after compatibility and regression gates pass.

## 7) Source-of-truth note

For this repository, this file is the protocol direction reference for ASP naming, profiles, and migration policy. Implementation details can evolve, but profile/compatibility statements here should stay synchronized with runtime behavior and tests.
