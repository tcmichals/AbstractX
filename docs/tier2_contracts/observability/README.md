# AbstractX Observability, Visualizer Studio & Tooling Guide

This guide explains the end-to-end architecture of the AbstractX observability pipeline: how hardware and software telemetry frames flow from the silicon/simulation level into the dynamic CTF 1.8 schema engine, how the Two-Level Dear ImGui Studio renders them at 120 FPS, how MemBrowse tracks memory footprints over time, and how to set up your local Python environment.

---

## 1. High-Level Architecture: The Three Pillars

AbstractX observability is built upon three decoupled pillars:

```
┌────────────────────────────────────────────────────────────────────────┐
│                   PILLAR 1: UNIFIED 64-BYTE TLP BUS                    │
│   • Hardware: FPGA Auto-DMA IP (asp_imu_auto_dma.sv, asp_router.sv)     │
│   • Software: C++20 SITL & Bare-Metal Tasks (gps_imu_app)              │
│   • Symmetrical Wire Framing: PCIe-style 20B Header + 40B CTF + 4B CRC │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │ UDP Port 9870 / Shared SRAM
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│               PILLAR 2: DYNAMIC CTF 1.8 SCHEMA LOADER                  │
│   • Authoritative Schema: trace/barectf_config.yaml & trace_schema.json│
│   • Loader Engine: tools/visualizer/ctf_schema_loader.py               │
│   • Compiles struct.Struct decoders at runtime (Zero hardcoded offsets)│
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │ Typed Field Dictionaries & Metrics
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│            PILLAR 3: TWO-LEVEL STUDIO GUI (imgui-bundle)               │
│   • Level 1: Core System Platform Observability (abstractx_studio.py)  │
│       - Platform Topology & Interconnect (Cores, SPU/CPU%, SPSC Rings) │
│       - Dual-Plane Execution Timeline (ISR/DMA vs Coroutines + DWARF)  │
│       - Continuous Memory Observability (MemBrowse Section Budgets)    │
│   • Level 2: User / Domain Extensible Plugins (flight_plugin.py)       │
│       - Primary Flight Display (PFD) Artificial Horizon & Pitch Ladder │
│       - 3D Quadcopter Perspective Wireframe (Tait-Bryan rotation)      │
│       - Quad-X Motor Mixer Demands & Navigation Gauges                 │
│       - 8 kHz ICM-42688-P IMU Real-Time Oscilloscope (ImPlot)          │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Python Virtual Environment (`.venv`) Setup

Modern Linux distributions (Ubuntu 24.04/23.10, Debian 12, Fedora) enforce **PEP 668** (`externally-managed-environment`), preventing global `pip install` commands. Using an isolated virtual environment (`.venv`) is the recommended, zero-risk approach.

### Quick Automated Setup
Run the automated environment setup script from the repository root:
```bash
./tools/setup_venv.sh
```

### Manual Setup (Step-by-Step)

1. **Ensure Python 3 venv and build tools are installed**:
   ```bash
   sudo apt-get update
   sudo apt-get install -y python3-venv python3-pip
   ```

2. **Create the virtual environment**:
   ```bash
   python3 -m venv .venv
   ```

3. **Activate the virtual environment**:
   ```bash
   source .venv/bin/activate
   ```
   *(Your terminal prompt will now show `(.venv)`)*

4. **Install required dependencies**:
   ```bash
   pip install --upgrade pip
   pip install -r tools/visualizer/requirements.txt
   ```
   *(Installs `imgui-bundle>=1.5.0` and `numpy>=1.22.0`)*

5. **Verify the installation**:
   ```bash
   python3 -c "import imgui_bundle, numpy; print('Environment ready! imgui-bundle version:', imgui_bundle.__version__)"
   ```

---

## 3. How the Telemetry Pipeline Works

### 1. Symmetrical Telemetry Generation
Hardware and software emit identical binary data planes:
* **FPGA Hardware (Big-Endian)**: The `asp_imu_auto_dma.sv` hardware core latches sensor DRDY interrupt pins, clocks SPI bursts, prepends the 20-byte TLP header with nanosecond timestamps, and streams 64-byte frames across the crossbar router.
* **Processor Firmware (Little-Endian)**: C++20 coroutines in `gps_imu_app` emit 64-byte `Tlp64` structures into lock-free `SpscRingBuffer` channels.

### 2. Runtime Schema Compilation
Instead of hardcoding binary offsets into Python visualizers:
1. [`tools/visualizer/ctf_schema_loader.py`](file:///home/tcmichals/ssdData/projects/home/AbstractX/tools/visualizer/ctf_schema_loader.py) reads [`apps/gps_imu_app/trace_schema.json`](file:///home/tcmichals/ssdData/projects/home/AbstractX/apps/gps_imu_app/trace_schema.json).
2. It compiles Python `struct.Struct` format strings at runtime.
3. It scales raw integers into engineering units (e.g. converting centidegrees to degrees with `scale: 0.01`).
4. It reads UI widget annotations (`artificial_horizon`, `altimeter`, `oscilloscope`, `throttle_bar`) to bind data streams to GUI components.

---

## 4. The Two-Level Studio GUI Architecture

The visualizer studio in [`tools/visualizer/abstractx_studio.py`](file:///home/tcmichals/ssdData/projects/home/AbstractX/tools/visualizer/abstractx_studio.py) cleanly decouples generic system tracing from domain-specific flight instruments.

### Level 1: Core System Platform Observability
Standard across every application built with AbstractX:
* **Tab 1: Platform Topology & Interconnect**: Auto-discovers the target silicon profile (Cores, SPU/CPU roles, clock frequencies, SPSC ring depth, and mailbox doorbell latencies).
* **Tab 1: Dual-Plane Execution Timeline**: Distinguishes Plane 1 (hardware ISRs and DMA bursts) from Plane 2 (C++20 cooperative coroutine tasks). Clicking any task jumps directly to its `__FILE__ : __LINE__` source code location.
* **Tab 1: Processor Utilization**: Tracks system CPU, AbstractX process CPU, coprocessor duty cycle, and FPGA logic LUT saturation.
* **Tab 3: Memory Observability**: Live RAM/Flash budgets and ELF section breakdowns powered by MemBrowse.
* **Tab 4: TLP & CTF Inspector**: Live breakdown of the 64-byte packet header and payload fields.

### Level 2: User & Domain Extensible Plugins
Application-specific views that plug into the studio:
* **[`tools/visualizer/flight_plugin.py`](file:///home/tcmichals/ssdData/projects/home/AbstractX/tools/visualizer/flight_plugin.py)**:
  * **Primary Flight Display (PFD)**: Vector-rendered artificial horizon with sky/ground polygons, roll reticle, and pitch ladder.
  * **3D Quadcopter Perspective Wireframe**: Real-time 3D Tait-Bryan rotation matrix projecting quadcopter arms and spinning motor discs.
  * **Quad-X Motor Mixer Demands**: Real-time M1–M4 throttle levels (100..1000 µs) with a 500 µs hover reference indicator.
  * **Aviation Navigation Gauges**: MSL Altitude, Ground Speed, Course Heading, and multi-rate stream metrics (IMU 8 kHz, Mag 50 Hz, GPS 10 Hz, AHRS 100 Hz).
  * **Sensor Oscilloscope**: ImPlot waveforms for 8 kHz Accel and Gyro data.

---

## 5. Continuous Memory Observability with MemBrowse

Because AbstractX is strictly **Freestanding C++20 with Zero Heap**, memory management is 100% deterministic:
* Zero dynamic allocation: `malloc`, `free`, and `operator new` are prohibited.
* SPSC rings, queues, and task frames are statically allocated in `.bss` and `.data`.

### Workflow
1. **Target Definitions ([`tools/membrowse-targets.json`](file:///home/tcmichals/ssdData/projects/home/AbstractX/tools/membrowse-targets.json))**:
   Specifies target ELF binaries, linker maps, and memory ceilings (e.g. RP2350 520 KB SRAM / 4 MB Flash).
2. **Local Tracker ([`tools/track_memory_membrowse.py`](file:///home/tcmichals/ssdData/projects/home/AbstractX/tools/track_memory_membrowse.py))**:
   Audits ELF sections, verifies zero heap references, and outputs `tools/visualizer/memory_metrics.json`.
3. **Live Studio Visualization**:
   Tab 3 in `abstractx_studio.py` reads `memory_metrics.json` to display live RAM/Flash utilization gauges.
4. **CI/CD Integration ([`.github/workflows/membrowse.yml`](file:///home/tcmichals/ssdData/projects/home/AbstractX/.github/workflows/membrowse.yml))**:
   Automatically runs `membrowse/membrowse-action` on every commit and PR to track memory deltas and enforce budget gates.

---

## 6. How to Run the Tools

Ensure your `.venv` is activated (`source .venv/bin/activate`):

### Option A: Standalone Simulation Mode (No Hardware Needed)
Test all GUI features, 3D attitude motion, PFD horizon tilt, and 8 kHz waveforms immediately:
```bash
# Launch Level 2 Flight Display
python3 apps/gps_imu_app/tools/flight_display.py --sim

# Or launch the full Two-Level Studio
python3 tools/visualizer/abstractx_studio.py --sim
```

### Option B: Software C++20 SITL Live Telemetry
Run the host C++20 simulation and stream to the GUI:
```bash
# Terminal 1: Build & run host SITL node
cmake -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --target gps_imu_app
./build-host/apps/gps_imu_app/gps_imu_app

# Terminal 2: Launch visualizer
python3 tools/visualizer/abstractx_studio.py --port 9870
```

### Option C: Hardware Co-Simulation (Cocotb + Verilator)
Stream FPGA RTL Auto-DMA directly into the visualizer:
```bash
python3 tools/run_sitl_gui_demo.py --mode cocotb-live
```

### Option D: Embedded Hardware Target (Pico 2 W / ESP32-P4)
Flash the target and point the visualizer to the UDP telemetry port:
```bash
python3 tools/visualizer/abstractx_studio.py --port 9870
```
