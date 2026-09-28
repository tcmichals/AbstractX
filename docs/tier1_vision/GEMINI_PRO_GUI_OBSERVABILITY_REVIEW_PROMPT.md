# Gemini Pro Prompt: AbstractX Studio GUI & Observability Architecture Review

## How to Use in Google AI Studio / Gemini Web

1. Open **[Google AI Studio](https://aistudio.google.com)** (or Gemini web interface).
2. Model: Select **Gemini 1.5 Pro** or **Gemini 2.0 Pro**.
3. Upload File: Click **"+" / "Upload File"** and attach:
   📁 `/home/tcmichals/ssdData/projects/home/AbstractX/abstractx_context_for_gemini.txt`
   *(This 1.3 MB bundle contains the complete visualizer codebase, specifications, C++ drivers, target configs, and test suites)*.
4. Copy and paste the entire prompt block below into the prompt box and click **Run**.

---

## ══════════════════════════════════════════════════════════════════════════
## COPY AND PASTE THE PROMPT BELOW INTO GEMINI PRO
## ══════════════════════════════════════════════════════════════════════════

```markdown
# TASK: Expert Architectural & Ergonomic Review of AbstractX Studio GUI

You are a **Principal Embedded Systems Architect & Developer Tooling Specialist** with deep expertise in real-time operating systems (FreeRTOS, Zephyr), bare-metal heterogeneous silicon (dual-core ARM Cortex-M/RISC-V + FPGA), freestanding C++20 stackless coroutines, and modern high-performance GUI profiling tools (**Dear ImGui**, **ImPlot**, and **HelloImGui**).

You are reviewing the architecture, design specifications, and implementation of **AbstractX Studio** (`tools/visualizer/abstractx_studio.py`), the observability workbench for the AbstractX framework.

The relevant files in the attached repository bundle (`abstractx_context_for_gemini.txt`) are:
- `tools/visualizer/SPECIFICATION.md`: The authoritative architectural specification defining requirements `[SPEC-STUDIO-01]` through `[SPEC-STUDIO-12]`.
- `tools/visualizer/abstractx_studio.py`: The complete multi-window HelloImGui docking workbench implementation.
- `docs/tier2_contracts/observability/RTOS_VS_CPP20_COROUTINES_OBSERVABILITY.md`: The comparative architectural study on why C++20 stackless coroutines replace RTOS tasks in AbstractX.
- `tools/visualizer/ctf_schema_loader.py` & `tools/visualizer/flight_plugin.py`: Dynamic CTF 1.8 schema engine and decoupled flight instrument plugin.
- `tests/test_visualizer_sdk.py`: The automated test suite verifying docking layouts, headless execution, and C++20 coroutine inspection logic.

---

### BACKGROUND: THE OBSERVABILITY CHALLENGE IN ABSTRACTX

AbstractX is a hardware-software co-design platform operating under strict real-time invariants:
1. **Freestanding C++20 with Zero Dynamic Heap ($0\text{ B}$)**: Memory must never be allocated via `malloc` or `new` during execution. All allocations are statically bounded (`SpscRingBuffer`, `AsyncQueue`, `SpscTlpRing`).
2. **Simple Cooperative Event Loop & Stackless Coroutines**: Real-time tasks are non-blocking C++20 coroutines resuming cooperatively on Core 1 when pushed by SPSC rings or hardware doorbells.
3. **Hardware FPGA Switch Fabric (SPU)**: An autonomous AXI-Stream crossbar (`asp_router.sv`) routing 64-byte PCIe-style Transaction Layer Packets (TLPs) at 150 MHz between hardware DMA and processor cores.
4. **Dynamic CTF 1.8 Telemetry**: Emitted as 64-byte binary TLPs over UDP/SRAM and decoded dynamically without hardcoded binary struct offsets.

---

### THE ARCHITECTURAL PROBLEM: RTOS TASK TRACING VS. C++20 COROUTINE OBSERVABILITY

Traditional RTOS profilers track thread preemption, task priorities, and mutex contention. In an RTOS, every task has an expensive dedicated call stack (1–4 KB) and blocks silently on semaphores, making it impossible to see *why* a task is waiting or whether an expected ISR was dropped.

AbstractX completely flips the script: **we do not use an RTOS or preemptive tasks**. Instead, AbstractX pairs freestanding C++20 stackless coroutines with synthesizable FPGA Auto-DMA engines. 

However, C++20 coroutines introduce an opacity challenge:
- Coroutines are compiled into opaque compiler-generated state machines.
- Without dedicated tooling, developers cannot see: Is this task suspended? What is it waiting on? Did it leak or hang? Which frame pool slot is it occupying?
- Traditional RTOS timeline tools cannot inspect `co_await` suspension tokens, static frame memory, or AXI crossbar routing.

To solve this opacity, AbstractX Studio implements a purpose-built observability suite centered on:
1. **Coroutine State & Suspension Inspector**: Tracking active coroutine handles, compiler state machines, exact `co_await` tokens, and per-awaiter watchdog budget bars.
2. **Dual-Plane Execution Timeline**: Decoupling physical hardware (ISR/DMA) on Plane 1 from stackless coroutines on Plane 2.
3. **Flow Integrity & Pacing Eye**: Tracking 8 kHz primary-paced jitter, eye diagram safety margins, transit delays, and SPSC lock-free ring saturation.
4. **Cross-Language Source & RTL Inspector**: Instant drill-down from real-time anomalies directly to C++ source lines or Verilog RTL.

---

### YOUR REVIEW MANDATE

Please provide an exhaustive, rigorous review addressing the following five areas:

#### 1. Ergonomic & Usability Audit (Eliminating Visual Clutter)
AbstractX Studio implements a clean multi-window docking architecture:
- **Window 1: AbstractX Core Studio**: Unified 10-tab diagnostic workbench featuring Coroutine Inspector, CPU Gauges & Topology, Dual-Plane Timeline, Flow Integrity & Pacing Eye, Simple Trace Viewer, TLP Bus Debugger, System Event Log, Source Code & RTL Inspector, FPGA Peripherals, and MemBrowse Memory.
- **Window 2: User Domain Flight Instruments**: Vector PFD artificial horizon, 3D attitude wireframe, quad-X motor demands, 8 kHz IMU oscilloscope.
- **Dedicated Decoupled Panes**: TLP Bus Debugger, System Event Log, Source Code & Performance Inspector, FPGA & Hardware Peripherals Inspector.

**Dynamic Window & Resolution Management**:
- Workflow Focus Presets: `Balanced`, `Flight Focus`, `Core Focus`, `Source Focus`, `FPGA Focus`, `Studio Workbench`.
- Window Maximize / Restore: Dedicated buttons on each header for instant layout reorganization.
- Dynamic Window Resizing: Calling `hello_imgui.change_window_size((w, h))` for instant resolution presets (`1280x800`, `1560x920`, `1920x1080`) and continuous width/height adjustment across Windows and Linux.
- Native Multi-Viewport Pop-Out (`enable_viewports = True`): Permitting any window to be torn off into its own independent desktop OS window for multi-monitor workstations.

**Question for Review**:
- How effective is this multi-window and dynamic layout architecture at preventing clutter during intense debugging sessions?
- What ergonomic improvements should be made to the window transitions, tab docking, and header controls?

---

#### 2. C++20 Coroutine State & Suspension Inspector
Evaluate the implementation in `_render_coroutine_inspector()`:
- **Live Coroutine Table**: Inspects `task_id`, name, state (`RUNNING`, `SUSPENDED`), exact `co_await` token (`spi_ring.pop()`, `timer.sleep(20ms)`), duration in state, and source mapping.
- **Per-Awaiter Stall / Deadlock Watchdog**: Rows glow red when duration exceeds allocated deadline budget, alerting developers to missed hardware doorbells or stalled rings.
- **Static Frame Pool Gauge**: Verifies zero dynamic heap allocation by tracking `.bss` pool utilization (`ABSTRACTX_CORO_POOL_SIZE = 60 KB`) with bump-allocator metrics.
- **Spawn Topology Tree**: Displays static parent-child coroutine dependency edges.

**Concrete Technical Questions**:
1. **Dynamic Frame Pool Compaction & Defrag Inspection**: While AbstractX uses static bump allocation, how can the GUI expose frame reuse high-watermarks and fragmentation diagnostics across long multi-hour flight runs?
2. **Watchdog Anomaly Drill-Down**: When a coroutine stalls on `co_await spi_ring.pop()`, how can the UI link directly to the upstream hardware producer in the FPGA Auto-DMA engine?

---

#### 3. High-Rate Telemetry & Statistical Visualizations (`ImPlot`)
AbstractX ingests multi-rate telemetry (8 kHz IMU, 100 Hz attitude, 50 Hz magnetometer, 10 Hz GPS).
- Current implementation uses `implot.plot_line()` for real-time task latencies and SPSC queue depths.

**Concrete Technical Questions**:
1. **Latency Jitter Distribution Histograms**: Provide a Python implementation pattern using `implot.plot_histogram()` or custom bins to visualize task execution time distributions and display tail percentiles ($p_{50}, p_{95}, p_{99}$) alongside deadline thresholds.
2. **Static Ring Buffer Fullness Heatmaps**: How can we render a continuous high-watermark gradient heatmap directly beneath the timelines to visualize SPSC queue saturation over time?
3. **Decimation & Performance**: How to ensure buffering and downsampling of 8 kHz data maintains 120 FPS rendering without dropping samples or lagging the GLFW/OpenGL main thread?

---

#### 4. Source Code Performance & Hotspot Profiler (Window 5)
Evaluate the Source Code & RTL Inspector:
- Displays source code from `apps/gps_imu_app/src/main.cpp`, `include/abstractx/drivers/imu/icm42688p.hpp`, and `rtl/asp_router.sv`.
- Annotates lines with execution latency badges, overrun alerts (`⚠️ OVERRUN`), and concurrency diagnostics.

**Questions**:
- How can this view be enhanced to help embedded firmware engineers immediately spot subtle timing bugs (e.g., unexpected cache misses, SPI bus stalls, priority inversions)?
- How should source file switching and line highlighting interact with clicks from the Coroutine Inspector and Simple Trace event log?

---

#### 5. Plugin SDK Architecture & Standalone Visualizer Unification
AbstractX Studio defines `AbstractXStudioPlugin` in `tools/visualizer/SPECIFICATION.md`:
- Lifecycle hooks: `on_init()`, `on_tlp_packet()`, `render_ui()`, `render_menu_items()`.
- Legacy standalone visualizers (`apps/gps_imu_app/tools/flight_display.py`) currently exist alongside `abstractx_studio.py`.

**Questions**:
- Critique the `AbstractXStudioPlugin` interface. Are there missing lifecycle events (e.g. `on_connect`, `on_disconnect`, `on_layout_reset`, `save_settings`)?
- Outline a clean migration plan to turn `flight_display.py` into a thin launcher that invokes AbstractX Studio with the `FlightPlugin` pre-configured, eliminating duplicated Pygame/OpenGL setup code.

---

### DELIVERABLES REQUIRED

Please format your response into five structured sections:
1. **Executive Evaluation**: High-level verdict on why C++20 stackless coroutines require purpose-built tooling rather than legacy RTOS profilers.
2. **Ergonomic & Dynamic Windowing Critique**: Assessment of the HelloImGui docking suite, dynamic window resizing with `change_window_size`, focus presets, and multi-viewport pop-out.
3. **C++20 Coroutine Observability Code Implementations**:
   - Production-ready Python code for per-coroutine watchdog budget limit bars and stall alerts on `imgui.ImDrawList`.
   - Production-ready Python code for `ImPlot` latency jitter histograms ($p_{95}, p_{99}$).
   - Timeline scrubber cross-window synchronization logic.
4. **Source Code & RTL Hotspot Profiling Recommendations**: UX and diagnostic enhancements for the cross-language inspector.
5. **Plugin SDK & Architecture Refactoring Blueprint**: Complete `AbstractXStudioPlugin` Python specification and standalone unification strategy.
```
