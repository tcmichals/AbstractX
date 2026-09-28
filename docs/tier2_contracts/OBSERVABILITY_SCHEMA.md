# AbstractX Observability, Tracing & Studio Framework Contracts

**Document:** `docs/tier2_contracts/OBSERVABILITY_SCHEMA.md`  
**Status:** Authoritative Foundation (Tier 2 SSOT)  
**Consolidates:** `ABSTRACTX_VISUALIZER_SPECIFICATION.md`, `BARECTF_AND_LIVE_VISUALIZER_ARCHITECTURE.md`, `ABSTRACTX_PLATFORM_TOPOLOGY_AND_METRICS_SPEC.md`, `HOW_TO_CTF_PING_PONG_TRACING.md`

---

## 1. Universal Observability Architecture

AbstractX treats telemetry not as an afterthought, but as a first-class data plane:
* **Binary Common Trace Format (CTF 1.8)**: Governed by `trace/barectf_config.yaml` and `trace_schema.json`.
* **Zero Overhead**: Flight loops emit raw 64-byte `Tlp64` binary packets into `g_telemetry_ring`.
* **No Hardcoded Payload Offsets**: Downstream visualizers compile Python `struct.Struct` decoders dynamically from the schema.

---

## 2. Dynamic Platform Topology Table (`PlatformTopologyTable`)

At startup, AbstractX emits a dynamic `PlatformTopologyTable` packet describing the silicon architecture:

| Field | Description |
| :--- | :--- |
| `platform_name` | String identifier (`pico2w_rp2350`, `esp32p4`, `allwinner_e907`, `linux`). |
| `active_cores` | Bitmask of online processor cores. |
| `core_clock_hz` | Operating frequencies of Tier 1 (I/O) and Tier 2 (Coroutine) cores. |
| `spsc_ring_depth` | Depths of ingress/egress lock-free rings. |
| `doorbell_type` | Inter-core mechanism (`sio_fifo`, `esp_ipc`, `msgbox`). |

---

## 3. AbstractX Studio Two-Level Architecture

The visualizer studio ([`tools/visualizer/abstractx_studio.py`](file:///home/tcmichals/ssdData/projects/home/AbstractX/tools/visualizer/abstractx_studio.py)) is built on `imgui-bundle` (`Dear ImGui` + `ImPlot`):

### Level 1: Core System Platform Observability
* **Platform Topology**: Visual discovery of cores and SPSC queue depths.
* **Dual-Plane Execution Timeline**: Plane 1 (hardware ISRs and DMA bursts) vs Plane 2 (C++20 coroutine tasks), with interactive jumping to `__FILE__ : __LINE__`.
* **Processor Utilization**: Real-time breakdown of CPU/SPU load and doorbell latency.
* **Continuous Memory Tracking**: Real-time SRAM/Flash budget gauges powered by MemBrowse.

### Level 2: Extensible Application Plugins
* **AbstractXStudioPlugin Interface**: Domain-specific instruments (PFD artificial horizon, 3D attitude perspective wireframe, Quad-X motor mixers, 8 kHz IMU oscilloscope, ESC configurator) plug into the studio tab bar.
