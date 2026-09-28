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
> AbstractX features an observability framework and real-time visualizer called **AbstractX Studio** ([`tools/visualizer/abstractx_studio.py`](tools/visualizer/abstractx_studio.py)), built with Python and `imgui-bundle` (`Dear ImGui` + `ImPlot` + GLFW/OpenGL) for zero-latency 120 FPS rendering.
>
> AbstractX Studio follows a **Two-Level Architecture**:
> - **Level 1 (Core System Observability)**: Standard across all AbstractX applications. Discovers target topology, visualizes the Dual-Plane execution timeline (Plane 1 ISR/DMA vs Plane 2 Coroutines) with interactive source-code jumping (`__FILE__ : __LINE__`), tracks CPU/SPU load, monitors continuous static memory footprints via **MemBrowse**, and decodes 64-byte CTF 1.8 TLPs via dynamic schemas.
> - **Level 2 (Extensible Application Plugins)**: Pluggable user domain visualizers (e.g. Primary Flight Display artificial horizon, 3D quadcopter perspective wireframe, Quad-X motor mixer, 8 kHz sensor oscilloscope, or ESC configurators).

### Prompt Instructions (Tier 3)
```markdown
# TASK: Observability Framework & AbstractX Studio GUI Architecture

Review the observability architecture, Python visualizer codebase (`tools/visualizer/`), telemetry schema (`trace/barectf_config.yaml`, `trace_schema.json`), and MemBrowse integration:

### 1. The Two-Level Studio GUI Architecture Review
Audit the separation of concerns in `tools/visualizer/`:
- How cleanly is Level 1 (Core System Telemetry) decoupled from Level 2 (Application Domain Plugins like `flight_plugin.py`)?
- Does `abstractx_studio.py` duplicate logic found in standalone visualizers (`apps/gps_imu_app/tools/flight_display.py`)?
- How should the plugin interface be formalized so that third-party developers can create domain visualizers (e.g. rover navigation, robotic gimbal, ESC configurator) without touching core studio code?

### 2. Dynamic CTF 1.8 Telemetry & MemBrowse Memory Integration
Audit the runtime decoding and memory auditing pipelines:
- Dynamic Schema Compilation: Review `tools/visualizer/ctf_schema_loader.py`. How effectively does it compile `struct.Struct` decoders at runtime from YAML/JSON without hardcoding payload offsets?
- Continuous Memory Tracking: Review `tools/track_memory_membrowse.py` and `.github/workflows/membrowse.yml`. How does it audit ELF sections (`.bss`, `.data`, `.text`) and enforce zero-heap budgets across targets (`build-pico2w`, `build-e907`, `build-host`)?
- High-Rate Waveform Plotting: Assess how 8 kHz IMU samples and high-rate telemetry are buffered and downsampled for rendering in `ImPlot` without dropping GUI frames.

### 3. Deliverables Required for Tier 3:
1. **AbstractX Studio Plugin SDK Specification**: Formal definition of the `AbstractXStudioPlugin` base class in Python, including lifecycle hooks:
   - `on_init(self, context)`
   - `on_tlp_packet(self, tlp_header, payload_dict)`
   - `render_ui(self, delta_time)`
   - `render_menu_items(self)`
2. **Visualizer Directory Refactoring**: Proposed folder structure for `tools/visualizer/` / `sdk/gui/` separating Core Studio, Decoders, and Plugin Packages.
3. **Standalone vs Studio Unification Plan**: Architectural recipe ensuring standalone visualizers (like `flight_display.py`) simply instantiate the core studio runtime with their plugin pre-selected, eliminating 100% of duplicated GUI setup code.
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
