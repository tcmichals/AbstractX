# Gemini Pro 3-Tiered Architecture & Repository Refactoring Framework

This document provides a **structured, 3-tiered prompt framework** designed for **Gemini Pro (1M–2M context window)** to ingest, analyze, and refactor the entire AbstractX codebase, documentation, specification-to-code traceability pipeline, and the **AbstractX Studio** GUI framework.

---

## How to Use This Prompt Framework

Rather than dumping a single monolithic prompt that leads to generalized, hand-waving outputs, this framework uses **Three Iterative Tiers**. Each tier focuses Gemini Pro's attention on a specific architectural plane, producing rigorous, immediately executable deliverables before proceeding to the next tier:

```
┌────────────────────────────────────────────────────────────────────────┐
│      TIER 1: REPOSITORY INVENTORY, DEDUPLICATION & TAXONOMY            │
│      • Macro-structure & folder taxonomy                               │
│      • Narrative deduplication across 35+ markdown files               │
│      • Target & BSP clean separation                                   │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │ Produces: Master Migration Matrix
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│      TIER 2: SPECIFICATION-DRIVEN (SSOT) & TRACEABILITY AUDIT          │
│      • Markdown Drives Code ([SPEC-*] <-> // @impl) audit              │
│      • Freestanding C++20 zero-heap invariant enforcement              │
│      • Modular Spec Template (decoupling Core, Target, and App specs)  │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │ Produces: Specification Architecture
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│      TIER 3: OBSERVABILITY & ABSTRACTX STUDIO GUI FRAMEWORK            │
│      • Two-Level Dear ImGui Studio Architecture (Core vs Plugins)      │
│      • Dynamic CTF 1.8 schema engine & MemBrowse memory integration    │
│      • AbstractX Studio Plugin SDK definition                          │
└────────────────────────────────────────────────────────────────────────┘
```

---

## Ingestion Step: How to Bundle the Repository for Gemini Pro

To feed the entire AbstractX repository into Gemini Pro (via Google AI Studio or Vertex AI), generate a unified context bundle using `repomix` or standard shell tools:

```bash
# Option A: Using repomix (Recommended)
npx repomix --include "apps/**,include/**,targets/**,rtl/**,tools/**,docs/**,trace/**,README.md,AGENTS.md" \
            --ignore "build/**,.git/**,third_party/**" \
            --output abstractx_repo_bundle.txt

# Option B: Using standard bash concatenation
(
  echo "=== REPOSITORY TREE ==="
  tree -I "build|third_party|.git"
  echo "=== FILE CONTENTS ==="
  find apps include targets rtl tools docs trace -type f \( -name "*.hpp" -o -name "*.cpp" -o -name "*.sv" -o -name "*.md" -o -name "*.py" -o -name "*.yaml" -o -name "*.json" \) -exec head -n 1000 -v {} +
) > abstractx_repo_bundle.txt
```

---

## ══════════════════════════════════════════════════════════════════════════
## TIER 1 PROMPT: REPOSITORY INVENTORY, DEDUPLICATION & TAXONOMY
## ══════════════════════════════════════════════════════════════════════════

### System Role & Context
> You are a Principal Systems Architect and Codebase Modernization Specialist reviewing **AbstractX**, an open-source hardware-software co-design framework for aerospace, robotics, and real-time embedded systems. AbstractX pairs freestanding C++20 stackless coroutines with synthesizable SystemVerilog FPGA switch fabrics and PCIe-style 64-byte Transaction Layer Packets (TLPs).
>
> The project has evolved rapidly and now contains over 35 markdown documents across `docs/`, multiple silicon target BSPs (`targets/pico2w_rp2350`, `targets/esp32p4`, `targets/linux`, `targets/allwinner_e907`), synthesizable RTL (`rtl/`), host SITL simulations (`sim/`), dynamic trace schemas (`trace/`), and developer tools (`tools/`).
>
> Your goal in **Tier 1** is to perform a comprehensive structural audit, identify narrative duplication across markdown documents, eliminate historical dead weight, and propose a clean, canonical directory taxonomy.

### Ingestion Instructions
> Paste `abstractx_repo_bundle.txt` along with the following prompt instructions.

### Prompt Instructions (Tier 1)
```markdown
# TASK: Repository Inventory, Deduplication & Structural Refactoring

Please perform a thorough audit of the AbstractX repository structure and documentation suite:

### 1. Markdown Narrative Deduplication Analysis
Inspect all files across `docs/`, `apps/`, and the repository root. Identify:
- Redundant narrative: Which concepts (e.g. 5-flow async coroutines, scheduler comparison, 64-byte TLP headers, primary-paced channel pattern) are re-explained repeatedly across multiple files?
- Historical vs Active Specs: Which documents represent point-in-time whitepapers or historical rationale, and which represent authoritative, living architectural specifications?
- Consolidation Candidates: Which markdown files should be merged, archived into `docs/historical/`, or consolidated into a single authoritative reference?

### 2. Target BSP & Hardware Separation
Audit the concrete silicon target BSPs under `targets/`:
- Are the target BSPs (`pico2w_rp2350`, `esp32p4`, `allwinner_e907`, `linux`) following a uniform layout?
- Are target specifications (`SPECIFICATION.md`, `HOWTO.md`, `io_processor.yaml`) consistent in naming and contract structure?
- How should multi-board configurations (e.g. Raspberry Pi Pico 2 W vs Waveshare ESP32-P4-WiFi6 SKU 32021) be standardized so adding a new silicon target requires zero changes to core code?

### 3. Deliverables Required for Tier 1:
1. **Deduplication Matrix**: Table listing existing documents, overlapping content, and recommended disposition (`Keep As-Is`, `Merge Into [File]`, `Archive`, `Delete`).
2. **Proposed Canonical Directory Tree**: Complete directory layout showing where every file in `docs/`, `include/`, `targets/`, `apps/`, and `tools/` belongs.
3. **Step-by-Step Migration Plan**: Exact `git mv` and file merge commands to execute the structural reorganization without losing git history.
```

---

## ══════════════════════════════════════════════════════════════════════════
## TIER 2 PROMPT: SPECIFICATION-DRIVEN ARCHITECTURE (SSOT) & TRACEABILITY
## ══════════════════════════════════════════════════════════════════════════

### System Role & Context
> In AbstractX, **Markdown drives the code (Single Source of Truth - SSOT)**. Architectural requirements, hardware memory maps, and multi-rate channel contracts are defined in Markdown first with unique tags (`[SPEC-*]`). Freestanding C++20 and SystemVerilog code carry matching implementation annotations (`// @impl [SPEC-*]`). Traceability is audited continuously in CI via `tools/audit_specs.py`.
>
> The codebase strictly enforces freestanding C++20 invariants: **zero dynamic heap (`malloc`, `free`, `new`, `delete` are banned), no STL containers (`std::vector`, `std::string`, `std::map` are banned), and 100% static memory pools**.

### Prompt Instructions (Tier 2)
```markdown
# TASK: Specification-Driven Architecture (SSOT) & In-Code Traceability Review

Review the specification ecosystem (`docs/DESIGN_SPECIFICATION.md`, `apps/gps_imu_app/SPECIFICATION.md`, `targets/*/SPECIFICATION.md`, `tools/audit_specs.py`, and source headers):

### 1. Specification Hierarchy & Tagging Granularity
Evaluate the current specification tagging system:
- Current tag categories: `[SPEC-ARCH-*]`, `[SPEC-HAL-*]`, `[SPEC-TLP-*]`, `[SPEC-TRACE-*]`, `[SPEC-IMU-*]`, `[SPEC-GPS-*]`, `[SPEC-APP-*]`.
- Is the current taxonomy properly decoupled? Are architecture-level invariants cleanly separated from hardware-specific peripheral contracts and application-specific fusion algorithms?
- Where are the blind spots? Are there critical hardware boundaries (e.g. DMA burst boundaries, inter-core doorbells, clock domain crossings, PSRAM cacheline alignment) that currently lack formal `[SPEC-*]` requirements?

### 2. Freestanding C++20 & Hardware Symmetry Audit
Audit the in-code implementations against the core invariants:
- Zero-Heap Compliance: Verify that all coroutine task frames, queues, and rings are statically allocated (`SpscRingBuffer`, `AsyncQueue`, `SpscTlpRing`).
- Symmetrical Offload Model: Verify that hardware drivers in SystemVerilog (`rtl/asp_imu_auto_dma.sv`) and software HAL drivers (`include/abstractx/hal/`) present an identical event-driven asynchronous contract to the application.
- Primary-Paced Channel Pattern: Verify that multi-rate sensor fusion (e.g. 8 kHz IMU, 50 Hz Mag, 10 Hz GPS) adheres to the non-blocking linear coroutine channel pattern without counter prescalers or busy-waits.

### 3. Deliverables Required for Tier 2:
1. **Traceability Architecture Assessment**: Strengths, weaknesses, and concrete gaps in the current `[SPEC-*]` specification suite.
2. **Modular Specification Standard**: A formalized multi-tier specification hierarchy (`SPEC_SYSTEM.md`, `SPEC_HAL.md`, `SPEC_APP.md`).
3. **Authoritative Specification Template (`SPEC_TEMPLATE.md`)**: A reusable markdown template with mandatory sections (Memory Map, Multi-Rate Timing, Invariants, Traceability Tags, and CppUTest Verification Matrix) for scaffolding future drivers and applications.
```

---

## ══════════════════════════════════════════════════════════════════════════
## TIER 3 PROMPT: OBSERVABILITY & ABSTRACTX STUDIO GUI ARCHITECTURE
## ══════════════════════════════════════════════════════════════════════════

### System Role & Context
> AbstractX features an observability framework and real-time visualizer called **AbstractX Studio** ([`tools/visualizer/abstractx_studio.py`](tools/visualizer/abstractx_studio.py)), built with Python and `imgui-bundle` (`Dear ImGui` + `ImPlot` + `HelloImGui`) for zero-latency 120 FPS rendering.
>
> AbstractX Studio follows an extensible **6-Window Docking Suite** architecture:
> - **Window 1 (AbstractX Core Studio)**: Silicon topology, C++20 Coroutine Inspector (state machines, co_await tokens, stall watchdogs), interactive `__FILE__ : __LINE__` source jumping, CPU/SPU load gauges, and continuous static memory footprints via **MemBrowse**.
> - **Window 2 (User Domain Instruments)**: Pluggable user domain visualizers (Primary Flight Display artificial horizon, 3D quadcopter perspective wireframe, Quad-X motor mixer, 8 kHz IMU oscilloscope).
> - **Window 3 (TLP Bus Debugger)**: Low-level 64-byte PCIe-style TLP packet stream table, 20-byte wire header decode, color-coded hex dump, and CRC32 verification.
> - **Window 4 (System Event Log)**: Substring search, severity filtering (`ALL`, `INFO`, `TLP`, `CORO`, `ISR`, `WARN`), auto-scrolling console.
> - **Window 5 (Source Code & Performance Inspector)**: Interactive C++ source code browsing, line-by-line execution latency profiling, deadline budget statuses, overrun warning banners, and concurrency diagnostics.
> - **Window 6 (FPGA & Hardware Peripherals Inspector)**: SPI0 Auto-DMA throughput/clock/saturation metrics, AXI-Stream TLP Crossbar routing latency (150 MHz / 13.3 ns), and DShot motor pulse generation.
> - **Dynamic Multi-Window Management**: Focus presets (`Balanced`, `Flight Focus`, `Core Focus`, `Source Focus`, `FPGA Focus`), Maximize/Restore buttons, and native multi-viewport tear-off (`enable_viewports = True`) for multi-monitor workstations.

### Prompt Instructions (Tier 3)
```markdown
# TASK: Observability Framework, Comparative Profiling & AbstractX Studio GUI Architecture

Review the observability architecture, comparative profiling study (`docs/tier2_contracts/observability/RTOS_VS_CPP20_COROUTINES_OBSERVABILITY.md`), Python visualizer codebase (`tools/visualizer/`), telemetry schema (`trace/barectf_config.yaml`, `trace_schema.json`), and MemBrowse integration:

### 1. Comparative Profiling Benchmark: conventional profiling tools vs. AbstractX Studio vs. AbstractX Studio
Audit the visualizer against modern embedded profiling requirements:
- Evaluate why conventional profiling tools's hierarchical call-stack zones cause cognitive overload for embedded loops and why its memory footprint conflicts with freestanding zero-heap invariants.
- Assess how AbstractX AbstractX Studio's horizontal swimlane and causality dependency model was adapted for AbstractX's dual-core C++20 coroutine and FPGA architecture.
- Evaluate the 6-window HelloImGui docking suite, dynamic multi-window decoupling (native OS floating viewports), and focus layout presets.

### 2. Dynamic CTF 1.8 Telemetry, MemBrowse & Hardware Observability
Audit the runtime decoding, memory auditing, and hardware peripheral pipelines:
- Dynamic Schema Compilation: Review `tools/visualizer/ctf_schema_loader.py`. How effectively does it compile `struct.Struct` decoders at runtime from YAML/JSON without hardcoding payload offsets?
- Continuous Memory Tracking: Review `tools/track_memory_membrowse.py` and `.github/workflows/membrowse.yml`. How does it audit ELF sections (`.bss`, `.data`, `.text`) and enforce zero-heap budgets across targets (`build-pico2w`, `build-e907`, `build-host`)?
- Hardware Peripheral Monitoring: Assess how SPI0 Auto-DMA (10 MHz / 1.25 MB/s) and AXI-Stream Crossbar (150 MHz / 13.3 ns) metrics are exposed in Window 6.
- Source Code Hotspot Profiling: Review Window 5 line-by-line latency profiling, deadline overrun alerts, and concurrency diagnostics.

### 3. Deliverables Required for Tier 3:
1. **Comparative Ergonomics & Feature Recommendations**:
   - Bezier causality curve rendering on `ImDrawList` canvas between triggering ISRs and coroutines.
   - Timeline scrubber hover synchronization across `ImPlot` line charts and event trace tables.
   - Latency jitter distribution histograms (`implot.plot_histogram`) for tail latencies ($p_{95}, p_{99}$).
2. **AbstractX Studio Plugin SDK Specification**: Formal definition of the `AbstractXStudioPlugin` base class in Python, including lifecycle hooks (`on_init`, `on_tlp_packet`, `render_ui`, `render_menu_items`).
3. **Visualizer Directory Refactoring & Standalone Unification**: Proposed folder structure separating Core Studio, Decoders, and Plugin Packages, ensuring standalone visualizers (like `flight_display.py`) instantiate the core studio runtime with their plugin pre-selected.
```

---

## ══════════════════════════════════════════════════════════════════════════
## EXECUTIVE SYNTHESIS PROMPT (RUN AFTER TIERS 1, 2, AND 3)
## ══════════════════════════════════════════════════════════════════════════

```markdown
# TASK: Executive Synthesis & Master Refactoring Blueprint

Based on your findings and outputs from Tier 1 (Taxonomy & Deduplication), Tier 2 (SSOT Traceability & Spec Templates), and Tier 3 (AbstractX Studio GUI SDK):

Produce the final **AbstractX Master Refactoring Roadmap**:
1. **Executive Summary**: Core architectural strengths and top 5 highest-impact refactorings.
2. **Unified Action Plan**: Phase 1 (File merges & folder taxonomy), Phase 2 (Spec modularization & template adoption), Phase 3 (AbstractX Studio SDK unification).
3. **Risk & Regression Mitigation**: How to execute all changes while preserving 100% specification traceability (`tools/audit_specs.py`), zero-heap invariants, and CI/CD green status.
```
