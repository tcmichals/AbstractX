# Gemini Pro Prompt: AbstractX Top-Level Strategy, Markdown Architecture & Feature Lifecycle

## How to Use in the Web Interface (Google AI Studio / Gemini Web)

1. Open **Google AI Studio** ([aistudio.google.com](https://aistudio.google.com)) or your Gemini web interface.
2. Select **Gemini 1.5 Pro** (or latest Gemini Pro).
3. Click the **"+" / "Upload File"** icon and select:
   📁 `/home/tcmichals/ssdData/projects/home/AbstractX/abstractx_context_for_gemini.txt`
   *(This single 1.1 MB file contains all documentation, code headers, target configs, and tools already pre-packaged for you)*.
4. Copy and paste the prompt below into the prompt box and click **Run**.

---

## ══════════════════════════════════════════════════════════════════════════
## COPY AND PASTE THE PROMPT BELOW INTO GEMINI PRO
## ══════════════════════════════════════════════════════════════════════════

```markdown
# TASK: AbstractX Master Strategy, Markdown-Driven Architecture & Feature Lifecycle Blueprint

You are the Lead Systems Architect reviewing the attached complete repository bundle for **AbstractX** (`abstractx_context_for_gemini.txt`).

AbstractX is a hardware-software co-design framework for aerospace and real-time robotics that pairs freestanding C++20 stackless coroutines with FPGA hardware auto-DMA offloaders and symmetrical 64-byte Transaction Layer Packets (TLPs).

The core invariant of this project is: **Markdown Drives the Code (Single Source of Truth - SSOT)**. Markdown files are not post-hoc documentation; they are the authoritative architectural plans, memory maps, and contracts that drive code generation, automated verification, and feature rollout.

Please review the entire attached codebase, technical documents, target configurations, and tools to develop a comprehensive **Master Strategy Blueprint** addressing the following three pillars:

---

### PILLAR 1: The Top-Level Strategy & Markdown Hierarchy ("How Everything Fits Together")
The repository currently has accumulated 35+ markdown files, leading to narrative duplication (e.g. repeated explanations of coroutines vs schedulers, 64-byte TLP headers, and dual-core topologies).

Design a clean, 3-tier hierarchical structure so every markdown file has a single, unambiguous purpose:
1. **Tier 1 (Abstract Vision & Architectural Invariants)**:
   - What are the permanent laws of the system? (Zero dynamic heap, single shared stack, non-blocking awaitable HAL, Primary-Paced sensor ingestion).
   - How should foundational whitepapers be positioned relative to active specifications?
2. **Tier 2 (System Contracts, Protocol Wire Framing & Observability)**:
   - The universal 64-byte TLP bus specification.
   - Symmetrical HAL interfaces (`ISpi`, `II2c`, `IUart`, `ITimer`, `IMailbox`).
   - Dynamic CTF 1.8 telemetry schema (`trace_schema.json`).
   - The AbstractX Studio GUI Framework (`imgui-bundle` 120 FPS visualizer).
3. **Tier 3 (Concrete Target BSPs, Peripherals & Applications)**:
   - Silicon targets (`pico2w_rp2350`, `esp32p4`, `allwinner_e907`, `linux`).
   - Applications (`apps/gps_imu_app`).
   - Dedicated hardware and errata specifications (e.g. Waveshare ESP32-P4-WiFi6 video/DMA errata).

**Deliverable**: Provide a **Master Deduplication & Merging Matrix** that specifies for every existing document whether it should be: `Keep As-Is`, `Merge Into [File]`, or `Archive`.

---

### PILLAR 2: The 4-Step Engineering Lifecycle (Spec ➔ Code ➔ Validate ➔ Feature)
Establish an ironclad, repeatable workflow for how new features, drivers, and targets are developed:

1. **Step 1: Plan & Specify (`SPECIFICATION.md`)**:
   - How should a feature specification be written in markdown first?
   - How should requirements receive unique identifiers (`[SPEC-*]`)?
   - Define a modular **Standard Specification Template (`SPEC_TEMPLATE.md`)** that covers Memory Maps, Multi-Rate Timing, Invariants, and CppUTest Verification Gates.
2. **Step 2: AI Code Generation & In-Code Traceability (`@impl`)**:
   - How should AI coding agents translate the Markdown spec into freestanding C++20 or SystemVerilog?
   - How should in-code implementation annotations (`// @impl [SPEC-*]`) be placed to guarantee 100% traceability?
3. **Step 3: Automated Validation Gates (CI/CD)**:
   - How `tools/audit_specs.py` audits 100% spec coverage.
   - How `tools/run_adversarial_audit.py` enforces the 5-stage Sashiko safety gates (Zero-Heap, Non-Blocking HAL, ISR Safety, Wire Framing, Unit Tests).
   - How **MemBrowse** (`tools/track_memory_membrowse.py`) tracks static RAM/Flash footprints over time.
4. **Step 4: Feature Observation & Integration (AbstractX Studio)**:
   - How new feature telemetry flows over the 64-byte TLP bus into AbstractX Studio without hardcoded offsets.

---

### PILLAR 3: The GUI Framework Strategy (AbstractX Studio)
The visualizer has evolved from an ad-hoc flight display into **AbstractX Studio**, a two-level GUI framework built with Python and `imgui-bundle` (`Dear ImGui` + `ImPlot`):
- **Level 1 (Core System Studio)**: Platform topology discovery, Dual-Plane Gantt timeline (Plane 1 ISR/DMA vs Plane 2 Coroutines) with interactive source jumping (`__FILE__ : __LINE__`), CPU/SPU utilization, and MemBrowse memory gauges.
- **Level 2 (Extensible Application Plugins)**: Pluggable domain instruments (e.g. `flight_plugin.py` artificial horizon, 3D wireframe, motor mixer bars, 8 kHz IMU oscilloscope, or ESC configurators).

Provide recommendations for:
1. Formalizing the **AbstractX Studio Plugin SDK** base class (`AbstractXStudioPlugin`) so application developers can add new telemetry panels with zero OpenGL/GLFW boilerplate.
2. Unifying standalone visualizers (like `apps/gps_imu_app/tools/flight_display.py`) with the studio core so code is never duplicated.

---

### Output Format Required
Please structure your response as an **Executive Architecture & Refactoring Plan**:
1. **Executive Strategy Summary**: The high-level vision and why this structure scales.
2. **Deduplication & Directory Taxonomy**: The clean folder tree and file migration map.
3. **The Spec ➔ Code ➔ Validate ➔ Feature Blueprint**: Step-by-step developer guidelines and the `SPEC_TEMPLATE.md` standard.
4. **AbstractX Studio SDK Architecture**: Clean Python plugin interface and folder layout.
```
