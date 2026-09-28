# Gemini Pro Prompt: AbstractX Studio GUI & Observability Architecture Review

## How to Use in Google AI Studio / Gemini Web

1. Open **[Google AI Studio](https://aistudio.google.com)** (or Gemini web interface).
2. Model: Select **Gemini 1.5 Pro** or **Gemini 2.0 Pro**.
3. Upload File: Click **"+" / "Upload File"** and attach:
   📁 `/home/tcmichals/ssdData/projects/home/AbstractX/abstractx_context_for_gemini.txt`
   *(This 1.3 MB bundle contains the complete visualizer codebase, specifications, comparative profiling study, C++ drivers, target configs, and test suites)*.
4. Copy and paste the entire prompt block below into the prompt box and click **Run**.

---

## ══════════════════════════════════════════════════════════════════════════
## COPY AND PASTE THE PROMPT BELOW INTO GEMINI PRO
## ══════════════════════════════════════════════════════════════════════════

```markdown
# TASK: Expert Architectural & Ergonomic Review of AbstractX Studio GUI

You are a **Principal Embedded Systems Architect & Developer Tooling Specialist** with deep expertise in real-time operating systems (FreeRTOS, Zephyr), bare-metal heterogeneous silicon (dual-core ARM Cortex-M/RISC-V + FPGA), freestanding C++20 stackless coroutines, and modern high-performance GUI profiling tools (**Dear ImGui**, **ImPlot**, **Tracy Profiler**, and **Percepio Tracealyzer**).

You are reviewing the architecture, design specifications, and implementation of **AbstractX Studio** (`tools/visualizer/abstractx_studio.py`), the observability workbench for the AbstractX framework.

The relevant files in the attached repository bundle (`abstractx_context_for_gemini.txt`) are:
- `tools/visualizer/SPECIFICATION.md`: The authoritative architectural specification defining requirements `[SPEC-STUDIO-01]` through `[SPEC-STUDIO-12]`.
- `tools/visualizer/abstractx_studio.py`: The complete 6-window HelloImGui docking workbench implementation.
- `docs/tier2_contracts/observability/COMPARATIVE_STUDY_TRACY_TRACEALYZER_ABSTRACTX.md`: The comparative architectural study across Tracy, Tracealyzer, and AbstractX Studio.
- `tools/visualizer/ctf_schema_loader.py` & `tools/visualizer/flight_plugin.py`: Dynamic CTF 1.8 schema engine and decoupled flight instrument plugin.
- `tests/test_visualizer_sdk.py`: The automated test suite verifying docking layouts, headless execution, and Tracealyzer timeline logic.

---

### BACKGROUND: THE OBSERVABILITY CHALLENGE IN ABSTRACTX

AbstractX is a hardware-software co-design platform operating under strict real-time invariants:
1. **Freestanding C++20 with Zero Dynamic Heap ($0\text{ B}$)**: Memory must never be allocated via `malloc` or `new` during execution. All allocations are statically bounded (`SpscRingBuffer`, `AsyncQueue`, `SpscTlpRing`).
2. **Simple Cooperative Event Loop & Stackless Coroutines**: Real-time tasks are non-blocking C++20 coroutines resuming cooperatively on Core 1 when pushed by SPSC rings or hardware doorbells.
3. **Hardware FPGA Switch Fabric (SPU)**: An autonomous AXI-Stream crossbar (`asp_router.sv`) routing 64-byte PCIe-style Transaction Layer Packets (TLPs) at 150 MHz between hardware DMA and processor cores.
4. **Dynamic CTF 1.8 Telemetry**: Emitted as 64-byte binary TLPs over UDP/SRAM and decoded dynamically without hardcoded binary struct offsets.

---

### THE CORE PROBLEM: TRACY VS. TRACEALYZER VS. ABSTRACTX STUDIO

Firmware engineers previously evaluated existing industry profilers:
- **Tracy Profiler**: Built with Dear ImGui and capable of 120 FPS, but tailored for high-performance desktop C++ and game engines. For bare-metal embedded loops running at 8 kHz:
  - Deep hierarchical call stacks produce overwhelming visual noise and clutter.
  - Complex mouse/keyboard navigation (edge-dragging, nested zone zoom) causes cognitive fatigue.
  - Tracy's target client library requires dynamic memory and socket streaming, directly violating AbstractX's Zero-Heap invariant.
  - Tracy has zero awareness of FPGA crossbars, hardware DMA completions, or 64-byte packet transports.
- **Percepio Tracealyzer**: The de facto RTOS gold standard for embedded software:
  - Mirrors embedded thinking via horizontal task/ISR swimlanes, execution deadlines, and causality predecessor/successor chains.
  - However, Tracealyzer is an expensive, closed-source commercial tool tied to legacy RTOS thread primitives (semaphores, mutexes). It has no awareness of C++20 stackless coroutines (`co_await` suspension reasons), FPGA AXI fabrics, or decoupled live flight instruments.

AbstractX Studio implements a purpose-built hybrid that captures Tracealyzer's swimlane clarity and Tracy's fluid vector plotting while adding native co-design observability.

---

### YOUR REVIEW MANDATE

Please provide an exhaustive, rigorous review addressing the following five areas:

#### 1. Ergonomic & Usability Audit (Eliminating Visual Clutter)
AbstractX Studio has transitioned to a **6-Window Docking Suite**:
- **Window 1: AbstractX Core Studio**: Multi-core silicon topology, Tracealyzer 4-track execution timeline (Core 0, Core 1, SPU, Interrupts), interactive `__FILE__ : __LINE__` source jumping, CPU/SPU load gauges, and MemBrowse static memory budget gauges.
- **Window 2: User Domain Flight Instruments**: Vector PFD artificial horizon, 3D attitude wireframe, quad-X motor demands, 8 kHz IMU oscilloscope.
- **Window 3: 64-Byte TLP Bus Debugger**: Live packet stream table, 20-byte wire header decode, color-coded hex dump, and IEEE CRC32 verification.
- **Window 4: Real-Time System Event Log**: Substring search, severity filtering (`ALL`, `INFO`, `TLP`, `CORO`, `ISR`, `WARN`), auto-scrolling console.
- **Window 5: Source Code Performance & Hotspot Inspector**: Interactive C++ source code browsing, line-by-line execution latencies, timing budget statuses, overrun warnings, and root-cause concurrency diagnostics.
- **Window 6: FPGA & Hardware Peripherals Inspector**: SPI0 Auto-DMA throughput/clock/saturation metrics, AXI-Stream TLP Crossbar routing latency (150 MHz / 13.3 ns), and DShot motor pulse generation.

**Dynamic Window Management**:
- Workflow Focus Presets: `Balanced (4-Pane)`, `Flight Focus`, `Core Focus`, `Source Focus`, `FPGA Focus`.
- Window Maximize / Restore: Dedicated `[⛶ Expand Window]` and `[🗗 Restore All Panes]` buttons on each header.
- Native Multi-Viewport Pop-Out (`enable_viewports = True`): Permitting any window to be torn off into its own independent desktop OS window for multi-monitor workstations.

**Question for Review**:
- How effective is this multi-window and dynamic layout architecture at preventing clutter during intense debugging sessions?
- What ergonomic improvements should be made to the window transitions, tab docking, and header buttons?

---

#### 2. Tracealyzer Timing Diagram & Issue Drill-Down
Evaluate the implementation in `_render_tracealyzer_and_charts()`:
- **4 Silicon Swimlanes**: Core 0 (Linux / M33), Core 1 (Coroutine Engine), SPU (FPGA AXI Crossbar), Interrupts (PLIC ISRs/Doorbells).
- **Microsecond Time Ruler**: Zoom presets (1x, 2x, 5x, 10x), pan scrubber, freeze toggle, and overrun filter.
- **Visual Overrun Alerting**: Glowing coral red spans (`#E06C75`) with red badge indicators when execution exceeds deadline budgets.
- **Issue Drill-Down Inspector**: Computes budget utilization %, traces causality chains (`Predecessor ──▶ Active Task ──▶ Successor`), diagnoses root cause, and provides a direct jump to source code.

**Concrete Technical Questions**:
1. **Bezier Causality Dependency Curves**: How can we draw smooth cubic Bezier dependency arrows on the `imgui.ImDrawList` canvas connecting the triggering ISR span (`spi0_dma_tc_isr`) to the resumed coroutine span (`imu_pipeline`) and downstream consumer (`attitude_ekf`)? Provide Python/ImGui code for calculating control points and arrowheads.
2. **Timeline Scrubber Synchronization**: How can dragging or hovering over the microsecond time ruler scrub and synchronize the cursor line across `ImPlot` line charts and highlight the corresponding row in the Simple Trace event table?

---

#### 3. High-Rate Telemetry & Statistical Visualizations (`ImPlot`)
AbstractX ingests multi-rate telemetry (8 kHz IMU, 100 Hz attitude, 50 Hz magnetometer, 10 Hz GPS).
- Current implementation uses `implot.plot_line()` for real-time task latencies and SPSC queue depths.

**Concrete Technical Questions**:
1. **Latency Jitter Distribution Histograms**: Provide a Python implementation pattern using `implot.plot_histogram()` or custom bins to visualize task execution time distributions and display tail percentiles ($p_{50}, p_{95}, p_{99}$) alongside deadline thresholds.
2. **Static Ring Buffer Fullness Heatmaps**: How can we render a continuous high-watermark gradient heatmap directly beneath the swimlanes to visualize SPSC queue saturation over time?
3. **Decimation & Performance**: How to ensure buffering and downsampling of 8 kHz data maintains 120 FPS rendering without dropping samples or lagging the GLFW/OpenGL main thread?

---

#### 4. Source Code Performance & Hotspot Profiler (Window 5)
Evaluate the new Source Code & Performance Inspector:
- Displays source code from `apps/gps_imu_app/src/main.cpp` and `include/abstractx/hal/async_driver.hpp`.
- Annotates lines with execution latency badges (e.g. `24.5 µs (Budget: 30 µs, 81.7%)`), overrun alerts (`⚠️ OVERRUN`), and concurrency diagnostics.

**Questions**:
- How can this view be enhanced to help embedded firmware engineers immediately spot subtle timing bugs (e.g., unexpected cache misses, SPI bus stalls, priority inversions)?
- How should source file switching and line highlighting interact with clicks from the Tracealyzer timeline and system event log?

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
1. **Executive Evaluation**: High-level verdict on the AbstractX Studio architecture relative to Tracy and Tracealyzer.
2. **Ergonomic & Dynamic Windowing Critique**: Assessment of the 6-window suite, focus presets, maximize/restore behavior, and multi-viewport pop-out.
3. **Tracealyzer Parity Code Implementations**:
   - Production-ready Python code for Bezier causality curves on `imgui.ImDrawList`.
   - Production-ready Python code for `ImPlot` latency jitter histograms ($p_{95}, p_{99}$).
   - Scrubber cross-window synchronization logic.
4. **Source Code Hotspot Profiling Recommendations**: UX and diagnostic enhancements for Window 5.
5. **Plugin SDK & Architecture Refactoring Blueprint**: Complete `AbstractXStudioPlugin` Python specification and standalone unification strategy.
```
