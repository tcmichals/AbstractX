# AbstractX Switch Fabric Architecture

This document defines the internal AbstractX fabric model where physical transports are translators, and routing is performed on internal fabric packets.

## 1) Topology and data flow

Core principle:

- External transport (SPI here) is a **translator**.
- Translator converts transport bytes into **AbstractX fabric packets**.
- Fabric routes packets to endpoints (Wishbone gateway, DMA RX/TX, etc.).

### Logical pipeline

1. Host sends/receives over SPI.
2. SPI Slave Translator converts to/from internal fabric packets.
3. Switch Fabric routes by target/command fields.
4. Endpoints execute operation (Wishbone transaction, DMA burst, status query).

## 2) Internal AbstractX fabric packet format

Internal fabric routing header (baseline):

| Field | Size | Description |
|---|---:|---|
| `target_id` | 4 bits | endpoint class (e.g., WB gateway, DMA TX, DMA RX) |
| `command` | 4 bits | op type (read/write/burst/status) |
| `sub_id_or_addr` | 16 bits | endpoint sub-channel or address/window |
| `payload` | N bytes | operation data |

Suggested baseline target mapping:

| Target ID | Endpoint |
|---:|---|
| `0x0` | Wishbone master gateway |
| `0x1` | DMA TX endpoint |
| `0x2` | DMA RX endpoint |
| `0x3` | Status/diagnostics endpoint |

Implementations may extend these IDs but should keep capability reporting in sync.

## 3) Endpoint roles

### A) SPI Slave Translator

- Terminates SPI timing/protocol details.
- Emits normalized fabric packets.
- Receives routed response packets and serializes to SPI MISO.

### B) Switch Fabric Hub

- Routes by `target_id` and `command`.
- Applies arbitration when multiple endpoints compete for shared paths.
- Preserves deterministic ordering rules within a transaction class.

### C) Wishbone Master Gateway

- Fabric-to-Wishbone bridge.
- Fabric remains bus-agnostic: forwards request/response without endpoint-specific semantics.
- AbstractX packets targeting Wishbone are converted into **standard Wishbone transactions** (adr/dat/we/sel/cyc/stb/ack semantics).
- This conversion path is intentionally generic so any transport translator (SPI/I2C/I3C/etc.) can reach Wishbone registers via the same fabric contract.

### D) DMA RX/TX Endpoints

- Connect streaming producers/consumers to circular buffers.
- Support loop/ring mode for continuous acquisition/streaming.
- Expose fill level / availability to fabric status path and IRQ policy.

### E) Global timebase and timestamp propagation

- A global monotonic timebase may be implemented in fabric clock domain.
- Stream producers may stamp samples/frames at ingress using this timebase.
- Implementations may also stamp frames at transport egress, yielding two distinct timestamp markers:
	- `ts_ingress`: capture/arrival time at ingress translator or source endpoint.
	- `ts_egress`: emission time at transport-facing egress boundary.
- Timestamp metadata is carried as sideband through AXIS/fabric seams and preserved through DMA paths when enabled.
- At transport egress, timestamp metadata may be injected into protocol-visible payload metadata according to profile policy.

AXIS tag bundle note:

- Timestamp fields (including optional `ts_ingress` and `ts_egress`) are part of the broader AXIS tag bundle (alongside route/context tags such as `TID`, `TDEST`, `TUSER`, `TLAST` semantics).
- Fabric components should propagate the full tag bundle coherently per frame/beat according to interface contract.

## 4) DMA looping and trigger model

### Looping mode

- DMA engines should support circular buffer operation in internal memory/BSRAM.
- Write/read pointers wrap at configured region boundaries.

### Trigger model

- DMA can be armed by control packets and/or external ISR-like events.
- Fabric can raise host IRQ (`INT_REQ`) from DMA threshold events.

### Autonomous streaming mode (post-configuration)

- Peripherals may start streaming autonomously once configured via Wishbone (no per-frame host start command required).
- Typical sequence:
	1. Host configures peripheral and DMA policy through Wishbone target.
	2. Peripheral enters stream-enabled state.
	3. Samples/events are pushed into DMA loop buffers.
	4. Fabric/IRQ policy notifies host when readable data thresholds are met.

This is especially useful for gyro/accel periodic data capture.

Normative policy:

- Chip ISR signaling (`INT_REQ`) is a recommended host notification mechanism when provided by the selected peripheral/profile.
- DMA endpoints may support auto-trigger behavior from threshold/event conditions so streaming operation is autonomous between host bursts.

Timer-trigger capability:

- Peripherals may include local timer triggers for periodic auto-stream generation (e.g., sample every N fabric clocks).

Threshold examples:

- egress fill level >= watermark,
- loop wrap event,
- overflow/underrun faults.

## 5) Streaming sensor workloads

Designed workload examples:

- Gyro/accel sample streams into DMA looping buffers.
- Host burst-reads DMA windows through fabric packet routes.
- Control path configures sample rate, ranges, filters via Wishbone target.

## 6) Backpressure and arbitration requirements

- Fabric/endpoint seams should use valid-ready semantics.
- Endpoints must exert backpressure when resources are full.
- Arbitration should be deterministic (fixed priority or round-robin policy documented by profile).

Timestamp integrity requirement:

- Fabric routing/arbitration must preserve per-frame timestamp association (no cross-frame timestamp/data mismatch).
- Fabric routing/arbitration should preserve coherent association for all AXIS sideband tags, including timestamp fields carried in tag metadata.

## 7) Extensibility targets

The same translator->fabric model can front-end additional interfaces:

- I2C / I3C translators,
- ADC / DAC stream/control bridges,
- additional serial or parallel sensor links.

External link type should not require changing internal fabric packet routing semantics.

---

*Revision: 1.0 (Apr 2026)*
