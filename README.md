# AbstractX

**Extensible Hardware-to-Linux Communication Framework**

AbstractX is a professional-grade, protocol-agnostic framework for bridging FPGA fabric (AXI-Stream) with Linux userspace through standard virtual network interfaces (TUN/TAP).

By abstracting the physical transport layer—5 Mbps SPI, high-speed serial, internal AXI mailboxes, and more—AbstractX lets hardware accelerators behave like standard network-connected devices.

Protocol direction: AbstractX uses **ASP (AbstractX Switch Protocol)** as its canonical runtime protocol. ASP is the next-generation AbstractX protocol family with a transport-agnostic switch-fabric model.

Normative protocol spec: `docs/ASP_PROTOCOL.md`.

Normative SPI transport profile: `docs/ASP_SPI_TRANSPORT.md`.

SPI command/register wire contract: `docs/ASP_SPI_REGISTER_MAP.md`.

SPI-slave stream-seam update notes: `docs/ASP_SPI_TRANSPORT.md` ("SPI slave stream-interface profile").

Requirements and quality gates: `docs/ASP_REQUIREMENTS.md`.

Validation matrix and evidence gates: `docs/ASP_VALIDATION_MATRIX.md`.

Release checkpoints and sign-off flow: `docs/ASP_RELEASE_PROCESS.md`.

Direction and migration notes: `docs/ASP_SPEC_DIRECTION.md`.

Documentation index/map: `docs/README.md`.

Switch fabric architecture (translator->fabric->DMA/Wishbone): `docs/ABSTRACTX_SWITCH_FABRIC_ARCHITECTURE.md`.

## 🔄 Key differences and updates

- The SPI slave path is aligned to a **stream interface seam** (ready/valid style) between pin-level SPI logic and protocol parser logic.
- External host profile is **Pi Zero 2W native SPI master -> FPGA SPI slave** with **no USB data path**.
- ASP transport remains byte-stream based over SPI, with framing and integrity enforced at protocol layer.
- FPGA-to-host async receive path uses a **chip ISR doorbell (`INT_REQ`)** plus a **length-first read flow** so the Pi clocks exactly the required bytes.
- DMA paths are designed for **auto-trigger operation** (threshold/event driven) so stream capture/drain proceeds without per-transfer host polling.
- SPI command-phase profile is defined for deterministic operation: `0x80` (WRITE_DATA), `0x01` (READ_STATUS), `0x02` (READ_DATA).
- Baseline deterministic operating point is 5 Mbps SPI (scalable higher when signal integrity/timing closure allow).
- After Wishbone configuration, peripherals may enter **autonomous streaming mode** (chip-side start), feeding DMA without per-frame host trigger traffic.

### AbstractX naming for the "wireless-chip-like" SPI model

- Treat the FPGA endpoint as an **AbstractX virtual network chip (vChip)** on SPI.
- SPI transport is a **translator** into internal **AbstractX fabric packets**.
- Fabric routing uses **target/command/sub-id** semantics; ASP remains the external protocol family.
- Core AbstractX idea: selected protocol fields are converted into internal **AXIS tag metadata** for switching/routing, then reconstructed back into protocol-visible fields at transport egress.
- Wishbone register access is reached through a **generic AbstractX-to-Wishbone gateway conversion** (standard bus transactions), not transport-specific glue logic.
- The Linux bridge acts as the **AbstractX host driver** between SPI and `tun0`.
- Timestamping follows the same **AXIS tag propagation model** as other sideband tags (e.g., route/context metadata), with optional dual markers: **ingress timestamp** (capture time) and **egress timestamp** (emit time).

## ✨ Key Features

- **Protocol-agnostic transport**: Decouples hardware logic from the physical link layer.
- **AXI-Stream-native design**: Encapsulates and routes AXIS packets using AbstractX IDs (**AXIDs**).
- **High-throughput architecture**: Optimized for low-latency, large-frame transfer workloads.
- **Deterministic SPI slave support**: Includes IRQ-based hardware flow control for reliable timing.
- **Linux TUN/TAP integration**: Exposes FPGA packet streams through familiar Linux networking paths.

## 🧩 System Architecture

AbstractX functions as a virtual NIC (**vNIC**) for FPGA-attached hardware pipelines.

### Hardware Side (FPGA)

The AbstractX RTL gateway receives transport bytes, translates them into internal fabric packets, and routes payloads through the AbstractX switch fabric.

| Component | Responsibility |
|---|---|
| SPI Slave Translator | Physical SPI to internal AbstractX packet translation |
| AbstractX Switch Fabric | Packet routing to endpoint targets |
| Wishbone Gateway | Fabric-targeted control/register bus operations |
| DMA RX/TX Endpoints | Looping stream buffers and burst transfer endpoints |

### Software Side (Linux)

The AbstractX bridge daemon runs on the host (for example, Raspberry Pi Zero 2W or Zynq MPSoC A53).

- **Read path**: Triggered by GPIO IRQ, reads exact packet length over SPI, then injects payload into `/dev/net/tun`.
- **Write path**: Reads packets from TUN and streams them to FPGA through the Linux SPI master.

## 📦 ASP Wire Profiles

ASP currently defines two wire profiles:

- **`asp-compat-v1` (default)**: compatibility profile for low-risk migration from legacy wire behavior.
- **`asp-native` (future)**: reserved for a fully native AbstractX envelope once migration gates and tooling readiness are complete.

Default (`asp-compat-v1`) frame fields:

| Field | Size | Description |
|---|---|---|
| Sync | 8-bit | `0xA5` start-of-frame marker |
| Version | 8-bit | Protocol version (`0x01`) |
| Flags | 8-bit | Metadata/ACK behavior |
| AXID | 8-bit | Routing tag (e.g., `0x01` control, `0x05` stream tunnel) |
| Sequence | 16-bit | Request/response correlation |
| Length | 16-bit | Payload byte length (project profile cap currently 512) |
| Payload | N bytes | Stream payload |
| CRC-16 | 16-bit | CRC16/XMODEM over `Version..Payload` |

Transport note: over SPI, ASP is a **byte-stream protocol**. SPI burst boundaries do not define packet boundaries.

## 🛠️ Getting Started

### 1) Hardware Wiring (SPI Example)

Connect Linux host (e.g., Pi Zero 2W) to FPGA (e.g., Tang Nano 9K):

- `MOSI`, `MISO`, `SCLK`, `CS`
- `IRQ` (FPGA output → host GPIO input) for asynchronous receive notification

### 2) FPGA Integration

Instantiate `abstractx_slave_top.v` and connect your internal AXIS master/slave ports to the AbstractX switch fabric.

### 3) Linux Bridge Build/Run

Build and launch the bridge daemon:

```bash
gcc abstractx_bridge.c -o abstractx_bridge -lpthread
sudo ./abstractx_bridge --interface tun0 --speed 5000000
```

## 📘 Typical Use Cases

- **Real-time hardware offload**: Move CPU-intensive pipelines into FPGA logic.
- **High-speed telemetry**: Stream sensor/system data into Linux applications via UDP/TCP stacks.
- **Edge acceleration**: Feed inference or DSP engines via a network-like software API.

## 🤝 Contributing

Contributions are welcome. If you extend transport backends, add AXID mappings, or improve bridge reliability/performance, feel free to open a PR.

## 📄 License

This project is licensed under the terms in `LICENSE`.