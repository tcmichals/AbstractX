# Gemini Pro Prompt: AbstractX Master Strategy, Observability Benchmark & Architecture Blueprint

## How to Use in the Web Interface (Google AI Studio / Gemini Web)

1. Open **Google AI Studio** ([aistudio.google.com](https://aistudio.google.com)) or your Gemini web interface.
2. Select **Gemini 1.5 Pro** (or latest Gemini Pro).
3. Click the **"+" / "Upload File"** icon and select:
   📁 `/home/tcmichals/ssdData/projects/home/AbstractX/abstractx_context_for_gemini.txt`
   *(This single bundle contains all documentation, specifications, comparative analyses, code headers, target configs, and tools pre-packaged for you)*.
4. Copy and paste the prompt below into the prompt box and click **Run**.

---

## ══════════════════════════════════════════════════════════════════════════
## COPY AND PASTE THE PROMPT BELOW INTO GEMINI PRO
## ══════════════════════════════════════════════════════════════════════════

```markdown
# TASK: AbstractX Master Strategy, Comparative Observability Benchmark & Architectural Blueprint

You are the Lead Systems Architect and Principal Developer Tooling Engineer reviewing the attached complete repository bundle for **AbstractX** (`abstractx_context_for_gemini.txt`).

AbstractX is a hardware-software co-design framework for aerospace and real-time robotics that pairs freestanding C++20 stackless coroutines with FPGA hardware auto-DMA offloaders and symmetrical 64-byte Transaction Layer Packets (TLPs).

The core invariant of this project is: **Markdown Drives the Code (Single Source of Truth - SSOT)**. Markdown specifications (`SPECIFICATION.md`) are not post-hoc documentation; they are authoritative architectural plans, memory maps, and contracts that drive code generation, automated verification, and feature rollout.

Please review the entire attached codebase, technical specifications (`tools/visualizer/SPECIFICATION.md`), target configurations, tests, and the comparative profiling study (`docs/tier2_contracts/observability/RTOS_VS_CPP20_COROUTINES_OBSERVABILITY.md`) to deliver an exhaustive **Master Strategy & Architecture Blueprint** addressing the following four pillars:

---

### PILLAR 1: The Top-Level Strategy & Markdown Hierarchy ("How Everything Fits Together")
The repository currently contains 35+ markdown files, leading to narrative duplication (e.g., repeated explanations of coroutines vs. schedulers, 64-byte TLP headers, and dual-core topologies).

Design a clean, 3-tier hierarchical structure so every markdown document has a single, unambiguous purpose:
1. **Tier 1 (Abstract Vision & Architectural Invariants)**:
   - What are the permanent laws of the system? (Zero dynamic heap, single shared stack, non-blocking awaitable HAL, Primary-Paced sensor ingestion).
   - Foundational whitepapers vs. active specifications.
2. **Tier 2 (System Contracts, Protocol Wire Framing & Observability)**:
   - The universal 64-byte TLP bus specification.
   - Symmetrical HAL interfaces (`ISpi`, `II2c`, `IUart`, `ITimer`, `IMailbox`).
   - Dynamic CTF 1.8 telemetry schema (`trace_schema.json`).
   - The AbstractX Studio GUI Framework (`imgui-bundle` 120 FPS visualizer) and comparative profiling benchmarks.
3. **Tier 3 (Concrete Target BSPs, Peripherals & Applications)**:
   - Silicon targets (`pico2w_rp2350`, `esp32p4`, `allwinner_e907`, `linux`).
   - Applications (`apps/gps_imu_app`).
   - Dedicated hardware and errata specifications (e.g. Waveshare ESP32-P4-WiFi6 video/DMA errata).

**Deliverable**: Provide a **Master Deduplication & Merging Matrix** that specifies for every existing document whether it should be: `Keep As-Is`, `Merge Into [File]`, or `Archive`.

---

### PILLAR 2: The 4-Step Engineering Lifecycle (Spec ➔ Code ➔ Validate ➔ Feature)
Establish an ironclad, repeatable workflow for how new features, drivers, and targets are developed:

1. **Step 1: Plan & Specify (`SPECIFICATION.md`)**:
   - How feature specifications are written in markdown first.
   - How requirements receive unique identifiers (`[SPEC-*]`).
   - Define a modular **Standard Specification Template (`SPEC_TEMPLATE.md`)** covering Memory Maps, Multi-Rate Timing, Invariants, and CppUTest Verification Gates.
2. **Step 2: AI Code Generation & In-Code Traceability (`@impl`)**:
   - How AI coding agents translate the Markdown spec into freestanding C++20 or SystemVerilog.
   - How in-code implementation annotations (`// @impl [SPEC-*] <file_path>`) are placed to guarantee 100% traceability.
3. **Step 3: Automated Validation Gates (CI/CD)**:
   - How `tools/audit_specs.py` audits 100% spec coverage.
   - How `tools/run_adversarial_audit.py` enforces the 5-stage Sashiko safety gates (Zero-Heap, Non-Blocking HAL, ISR Safety, Wire Framing, Unit Tests).
   - How **MemBrowse** (`tools/track_memory_membrowse.py`) tracks static RAM/Flash footprints over time and enforces zero dynamic heap.
4. **Step 4: Feature Observation & Integration (AbstractX Studio)**:
   - How new feature telemetry flows over the 64-byte TLP bus into AbstractX Studio without hardcoded offsets.

---

### PILLAR 3: Comparative Profiling Benchmark & Observability Workbench (AbstractX Studio)
The visualizer has evolved from an ad-hoc flight display into **AbstractX Studio** (`tools/visualizer/abstractx_studio.py`), a multi-window HelloImGui docking workbench built with Python and `imgui-bundle` (`Dear ImGui` + `ImPlot` + `HelloImGui`).

#### A. conventional profiling tools vs. AbstractX AbstractX Studio vs. AbstractX Studio
Firmware developers previously evaluated **conventional profiling tools** and **AbstractX AbstractX Studio**:
- **conventional profiling tools** is ImGui-based and fast, but proved difficult to use for embedded firmware: its deep hierarchical call-stack zones create immense clutter for multi-kHz cyclic loops, its target agent requires dynamic memory and heavy networking, and it has no awareness of FPGA crossbars or hardware TLPs.
- **AbstractX Studio** has the ideal visualization paradigm (horizontal task/ISR swimlanes, execution time budgets, causality predecessor/successor chains, and clear issue drill-down), but is proprietary, expensive, tied to legacy RTOS threading models, and lacks modern C++20 coroutine, FPGA, and memory budget integration.

Evaluate how AbstractX Studio combines the best of both worlds:
1. **Four Silicon Swimlanes**: Core 0 (Linux / M33), Core 1 (Coroutine Engine), SPU (FPGA AXI Crossbar), and Interrupts (PLIC ISRs/Doorbells).
2. **Issue Drill-Down Inspector**: Computes budget utilization %, traces causality chains (`Predecessor ──▶ Active Task ──▶ Successor`), diagnoses root cause, and inspects C++ source code.
3. **Synchronized Real-Time Line Charts**: High-speed `ImPlot` multi-line graphs tracking task latencies and SPSC lock-free queue depths against deadline threshold lines.
4. **Static Memory Observability**: MemBrowse zero-heap verification ($0\text{ B}$ dynamic heap badge and static ELF section bars).

#### B. Dynamic Multi-Window Docking Suite (6 Windows)
To eliminate visual clutter and accommodate an expanding suite of specialized tools:
1. **Window 1: AbstractX Core Studio**: Platform topology, C++20 Coroutine Inspector, CPU/SPU gauges, MemBrowse memory bars.
2. **Window 2: User Domain Flight Instruments**: Vector PFD artificial horizon, 3D attitude wireframe, quad-X motor demands, 8 kHz IMU real-time oscilloscope.
3. **Window 3: 64-Byte TLP Bus Debugger**: Live packet stream table, 20-byte wire header decode, color-coded hex dump, CRC32 verification.
4. **Window 4: Real-Time System Event Log**: Substring filter, severity levels (`ALL`, `INFO`, `TLP`, `CORO`, `ISR`, `WARN`), auto-scroll.
5. **Window 5: Source Code Performance & Hotspot Inspector**: Interactive C++ source code browsing, line-by-line execution latencies, timing budget statuses, overrun warnings, and concurrency diagnostics.
6. **Window 6: FPGA & Hardware Peripherals Inspector**: SPI0 Auto-DMA throughput/clock/saturation metrics, AXI-Stream TLP Crossbar routing latency (150 MHz / 13.3 ns), and DShot motor pulse generation.

#### C. Dynamic Window Decoupling & Viewport Management
AbstractX Studio features dynamic window management:
- **Focus Layout Presets**: `Balanced (4-Pane)`, `Flight Focus`, `Core Studio Focus`, `Source Focus`, `FPGA Focus`.
- **Window Maximize / Restore**: Dedicated `[⛶ Expand Window]` and `[🗗 Restore All Panes]` buttons on each window header.
- **Multi-Viewport Pop-Out**: Native support (`enable_viewports = True`) permitting any window to be torn off into its own independent desktop OS window for multi-monitor workstations.

**Deliverable**: Provide recommendations for:
1. Drawing **Bezier causality dependency curves** on the `ImDrawList` canvas between triggering ISRs and coroutines.
2. Synchronizing timeline scrubber dragging with `ImPlot` line charts and event trace tables.
3. Adding **latency jitter distribution histograms** (`implot.plot_histogram`) to detect tail latencies ($p_{95}, p_{99}$).

---

### PILLAR 4: AbstractX Studio SDK & Standalone Unification
Provide recommendations for:
1. Formalizing the **AbstractX Studio Plugin SDK** base class (`AbstractXStudioPlugin`) so application developers can add new telemetry panels with zero OpenGL/GLFW boilerplate:
   - `on_init(self, context)`
   - `on_tlp_packet(self, tlp_header, payload_dict)`
   - `render_ui(self, delta_time, state)`
   - `render_menu_items(self)`
2. Unifying standalone visualizers (like `apps/gps_imu_app/tools/flight_display.py`) with the studio core so code is never duplicated.

---

### Output Format Required
Please structure your response as an **Executive Architecture & Refactoring Plan**:
1. **Executive Strategy Summary**: High-level architectural evaluation and key strengths.
2. **Deduplication & Directory Taxonomy**: The clean folder tree and file migration map across Tiers 1, 2, and 3.
3. **The Spec ➔ Code ➔ Validate ➔ Feature Blueprint**: Step-by-step developer guidelines and the `SPEC_TEMPLATE.md` standard.
4. **AbstractX Studio Architecture & Comparative Benchmark**: Evaluation of conventional profiling tools vs AbstractX Studio vs AbstractX Studio, dynamic multi-window docking, and concrete Python/ImGui recommendations.
5. **AbstractX Studio SDK & Unification Architecture**: Clean Python plugin interface and folder layout.
```
