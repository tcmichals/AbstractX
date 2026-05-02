# AbstractX

**Small. Fast. Real-time.**

AbstractX is a compact hardware-to-Linux streaming and control framework for FPGA systems.

It gives you one clean path for:

- **register access**,
- **high-rate streaming data**,
- **timestamped transport**, and
- **real-time mixed workloads**

over a simple external link such as **SPI**.

The core idea is straightforward: AbstractX translates protocol fields into **AXIS tags** for internal switching, moves data through a lightweight switch fabric, then reconstructs protocol-visible traffic at the edge. That means **control-plane traffic and streaming traffic can live together in one design** without adding unnecessary complexity.

## Why use AbstractX?

AbstractX is built for projects that need FPGA performance without a giant software stack.

Use it when you want:

- a **small codebase** that is easy to reason about,
- **fast real-time behavior** with deterministic transport flow,
- **mixed register + stream access** in one protocol,
- **autonomous streaming** from peripherals without per-frame host polling,
- **timestamped data paths** for measurement and correlation,
- a clean bridge from hardware pipelines into **Linux userspace**.

In short: it is designed to feel like a practical embedded data plane rather than a heavyweight research stack.

## What it does

AbstractX turns an FPGA endpoint into a small virtual hardware device that a Linux host can talk to efficiently.

It supports:

- **register reads/writes** through a generic fabric-to-Wishbone path,
- **streaming payload transport** for sensor, telemetry, serial, or data-capture flows,
- **shared transport** where control operations and stream data coexist,
- **IRQ + length-first reads** for deterministic host-side receive behavior,
- **AXIS tag propagation** for routing and metadata handling,
- **dual timestamp support** with optional ingress and egress markers.

## Simple block diagram

```text
+----------------------+    SPI    +---------------------------+    +---------------------------+
| Linux Host           | <-------> | SPI to/from AbstractX     | <->| AbstractX Switch Fabric   |
| Control + Apps       |           | Transport Translator      |    | route + switch + tags     |
+----------------------+           +---------------------------+    +-------------+-------------+
        ^                                                                         ^
        | IRQ / INT_REQ                                                           |
        +-------------------------------------------------------------------------+

                                                                              /   \
                                                                             v     v

   control path <--> +----------------------+                      +----------------------+ <--> stream path
                     | Wishbone Gateway     |                      | DMA / Stream         |
                     | Register Access      |                      | Endpoints            |
                     +----------+-----------+                      +----------+-----------+
                                |                                             |
                                v                                             v
                     +----------------------+                      +----------------------+
                     | Control Peripherals  |                      | Streaming Sources    |
                     | GPIO / status / ADC  |                      | gyro / accel / IO   |
                     +----------------------+                      +----------------------+

                                 metadata / AXIS tags: route + ts_in + ts_out
```

## Key features

- **Small and focused**  
	The design is intentionally compact and understandable.

- **Fast real-time transport**  
	Built for deterministic data movement, low overhead, and hardware-friendly flow control.

- **Register access + streaming in one path**  
	Control transactions and continuous stream traffic use the same overall framework.

- **Autonomous streaming**  
	Peripherals can be configured once, then stream on their own through DMA-style paths.

- **Great for sensors and converters**  
        A good fit for **gyro/accel devices**, **ADC capture**, telemetry sources, and other periodic producers.

- **Timestamping built in**  
	Metadata can include **ingress** and **egress** timestamps for correlation, timing analysis, and tracing.

- **AXIS-native internal model**  
	Protocol fields become switching metadata, making internal routing clean and scalable.

- **Linux-friendly**  
	Designed to bridge hardware data into Linux software cleanly, including TUN/TAP-oriented flows.

## Example use cases

### Sensor streaming

Configure a gyro, accelerometer, or ADC once over registers, then let it stream continuously into looped buffers while the host reads bursts when needed.

### Mixed control + data systems

Send register writes for setup and control while streaming real-time samples over the same transport.

### Timestamped measurement pipelines

Attach ingress and egress timing metadata to stream traffic for latency measurement, synchronization, and debug.

### Lightweight FPGA-to-Linux offload

Expose FPGA-side functions to Linux userspace without needing a giant control framework.

## Testing and quality

AbstractX is intended to be **tested, not guessed at**.

The project documentation and scaffolding are built around:

- **Python-based validation**,
- **cocotb simulation**,
- block-level and subsystem-level verification,
- protocol validation for register and stream behavior,
- validation of timestamp/tag handling and deterministic transport behavior.

## Bring-up model

The current reference bring-up path is:

- **Linux SPI master -> FPGA SPI slave**
- **Pi Zero** as a practical bring-up and test host
- IRQ-assisted host reads with exact-length transfers

This keeps the external setup simple while preserving the internal AbstractX model.

## Current development direction

The active integration focus is now **QMTECH Zynq-7020** with:

- Buildroot-based Linux bring-up,
- Python userspace TUN bridge for fast iteration,
- DMA as the preferred in-box transport target, and
- Rust userspace TUN + DMA bridge as the long-term hardened path.

Existing Gowin targets (`tang9k`, `primer20k`) remain valuable working references, but the Zynq path is the current system-integration priority.

## Documentation

The README is the front door. The detailed specification lives in `docs/`:

- `docs/ASP_PROTOCOL.md` — normative protocol behavior
- `docs/ASP_SPI_TRANSPORT.md` — SPI transport profile
- `docs/ASP_SPI_REGISTER_MAP.md` — byte/register wire contract
- `docs/ABSTRACTX_SWITCH_FABRIC_ARCHITECTURE.md` — translator, fabric, DMA, Wishbone architecture
- `docs/ASP_REQUIREMENTS.md` — requirements and quality gates
- `docs/ASP_VALIDATION_MATRIX.md` — verification gates and evidence
- `docs/ASP_RELEASE_PROCESS.md` — release and sign-off flow
- `docs/ASP_SPEC_DIRECTION.md` — protocol direction and compatibility profile notes
- `docs/README.md` — docs map

Useful implementation entry points for the current Zynq work:

- `python/asp_tun_bridge.py` — unified Python SPI/DMA TUN bridge
- `python/TUN_FRAMEWORK.md` — short Python-now / Rust-next rationale
- `rust/tun_dma_bridge/README.md` — Rust userspace TUN + DMA scaffold notes
- `hw/qmtech_zynq7020/README.md` — QMTECH board notes, Pico/XVC, and build conventions

## Protocol direction

AbstractX uses **ASP (AbstractX Switch Protocol)** as its runtime protocol family.

Today the repository defines:

- **`asp-compat-v1`** as the default compatibility profile
- **`asp-native`** as the future fully native AbstractX profile

## Contributing

If you want to extend transport adapters, add endpoints, improve validation, or tighten the RTL/software bridge, contributions are welcome.

## License

This project is licensed under the terms in `LICENSE`.