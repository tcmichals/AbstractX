# AbstractX Studio User Domain Flight & Navigation Plugin Specification
# (`tools/visualizer/USER_PLUGIN_SPECIFICATION.md`)

This document is the **authoritative architectural design specification** for the **AbstractX Studio User Domain Flight & Navigation Plugin (`tools/visualizer/flight_plugin.py`)**. It defines the telemetry ingestion contracts, the iNav-inspired auto-baudrate scanning and UBX binary protocol engines, simultaneous peripheral speed maximization, vector magnetic fusion, and the 60 FPS vector-rendered Compass Rose & Horizontal Situation Indicator (HSI).

---

## 1. Executive Summary & Architectural Invariants

AbstractX strictly partitions responsibilities across the heterogeneous compute boundary:
1. **Co-Processor / MCU Silicon Responsibility (XuanTie E906 / Pico 2 W)**:
   - Performs hard real-time, deterministic physical I/O ingestion.
   - Ingests raw, unprocessed 6-byte magnetometer readings from I2C1 (QMC5883L) and raw byte bursts from UART3 (u-blox NEO-M8N).
   - Forwards raw bytes directly into zero-copy RPMsg / TLP64 ring buffers without string allocation, NMEA tokenization, or floating-point trigonometry.
2. **Host Studio Responsibility (AbstractX Studio User Plugin)**:
   - Receives raw telemetry streams asynchronously over `/dev/rpmsg0` / UDP port 9870 / shared memory.
   - Implements the **iNav-standard Auto-Baudrate Manager** (`9600 -> 19200 -> 38400 -> 57600 -> 115200 baud`) and binary `UBX-NAV-PVT` / `UBX-NAV-DOP` state machine with Fletcher-16 CRC, with fallback to NMEA 0183 (`$GxRMC`, `$GxGGA`).
   - Performs magnetic field vector decomposition ($X, Y, Z$), hard-iron/soft-iron offset calibration, and magnetic heading derivation $\psi \in [0^\circ, 359^\circ]$.
   - Computes fused navigation vectors: Course Over Ground (COG), Ground Speed (SOG), and dynamic Wind Drift / crab angle ($\delta = \text{COG} - \psi$).
   - Renders a 60 FPS vector-based **Compass Rose & Heading Dial** instrument using the `imgui-bundle` `ImDrawList` (`imgui.get_window_draw_list()`) API.
3. **Simultaneous High-Speed Peripheral Maximization**:
   - Commands the GPS receiver to maximum operational baud rate (115,200 baud standard, up to 460,800 / 921,600 baud) via `UBX-CFG-PRT` and elevates navigation solution rate up to 10 Hz / 18 Hz via `UBX-CFG-RATE`.
   - Commands the electronic compass to maximum continuous Output Data Rate (200 Hz ODR) on 400 kHz Fast-Mode I2C via `REG_CONTROL_1` (`0x1D`).
   - Executes both peripheral configuration sequences concurrently during plugin initialization or upon operator command.

---

## 2. End-to-End System Dataflow

```mermaid
flowchart TD
    subgraph HARDWARE_LAYER["1. Physical Peripherals & Transports"]
        GPS_HW["<b>u-blox NEO-M8N GPS</b><br/>UART3 (PI11/PI12)<br/>115,200 / 460,800 Baud<br/>10 Hz Solution Rate"]
        MAG_HW["<b>QMC5883L Compass</b><br/>TWI1 I2C (PB4/PB5)<br/>400 kHz Fast-Mode<br/>200 Hz Continuous ODR"]
    end

    subgraph COPROCESSOR_LAYER["2. XuanTie E906 / MCU Silicon Layer"]
        E906_IO["<b>Raw Sensor Streamer</b><br/>Deterministic Non-Blocking I/O<br/>Zero Dynamic Heap Allocations<br/>Zero NMEA String Parsing"]
        RPMSG_SRAM["<b>VirtIO RPMsg Ring Buffer</b><br/>Shared SRAM (0x072E0000)<br/>Raw Binary / Byte Packets"]
    end

    subgraph HOST_STUDIO["3. AbstractX Studio Workbench"]
        RPMSG_RX["<b>Host Telemetry Ingress</b><br/>/dev/rpmsg0 & UDP :9870<br/>TelemetryState Thread-Safe Lock"]
        
        subgraph USER_PLUGIN["4. User Flight & Navigation Plugin (flight_plugin.py)"]
            AUTO_BAUD["<b>iNav Auto-Baudrate Manager</b><br/>9600 -> 19200 -> 38400 -> 57600 -> 115200<br/>UBX-CFG-PRT & UBX-CFG-RATE Serializer"]
            UBX_DEC["<b>UBX & NMEA FSM Parser</b><br/>Sync1/Sync2 -> Class/Id -> Payload -> Fletcher-16<br/>NAV-PVT (84B/92B) & NAV-DOP Decoders"]
            MAG_FUS["<b>Vector Magnetometer Fusion</b><br/>milliGauss / µT Normalization<br/>Hard/Soft-Iron Calibration<br/>Magnetic Heading psi & Wind Drift delta"]
            COMPASS_UI["<b>Vector Compass Rose (ImGui DrawList)</b><br/>Rotating 360° Heading Dial<br/>Cardinal Labels & 10° Ticks<br/>Fixed Lubber Line & Aircraft Reticle<br/>Magenta GPS Track Bug (COG)<br/>Dynamic Digital Readouts"]
        end
    end

    GPS_HW -->|Raw Serial Chunks| E906_IO
    MAG_HW -->|Raw 6B I2C Reads| E906_IO
    E906_IO --> RPMSG_SRAM
    RPMSG_SRAM --> RPMSG_RX
    RPMSG_RX --> AUTO_BAUD
    RPMSG_RX --> UBX_DEC
    RPMSG_RX --> MAG_FUS
    AUTO_BAUD -.->|UBX-CFG-PRT (115200) & RATE (10Hz)| GPS_HW
    AUTO_BAUD -.->|REG_CONTROL_1 (200Hz ODR)| MAG_HW
    UBX_DEC --> COMPASS_UI
    MAG_FUS --> COMPASS_UI

    classDef hwStyle fill:#1e1b4b,stroke:#818cf8,stroke-width:2px,color:#ffffff;
    classDef e906Style fill:#0f172a,stroke:#38bdf8,stroke-width:2px,color:#ffffff;
    classDef hostStyle fill:#064e3b,stroke:#34d399,stroke-width:2px,color:#ffffff;
    classDef uiStyle fill:#312e81,stroke:#a5b4fc,stroke-width:2px,color:#ffffff;

    class GPS_HW,MAG_HW hwStyle;
    class E906_IO,RPMSG_SRAM e906Style;
    class RPMSG_RX,AUTO_BAUD,UBX_DEC,MAG_FUS hostStyle;
    class COMPASS_UI uiStyle;
```

---

## 3. iNav-Inspired GPS Driver & Auto-Baudrate Architecture

The plugin adopts the proven architecture from `tcmichals/inav` (`src/drivers/gps/gps_driver.hpp` and `ubx_parser.hpp`):

### 3.1 Auto-Baudrate Scanning FSM
When communicating with a GPS receiver of unknown initial configuration:
1. **Baudrate Sequence**: The driver cycles through the standard iNav baudrate sequence:
   $$\text{Baudrates} = [9600, 19200, 38400, 57600, 115200]$$
2. **State Machine Transitions**:
   - `ScanningBaud`: The driver listens for valid preamble bytes (`0xB5 0x62` for UBX or `$` for NMEA) at the active baudrate for a 500 ms window. If no valid packet is decoded, it cycles to `next_baudrate()`.
   - `SendingConfig`: Once communication is locked, the driver emits:
     - `UBX-CFG-PRT`: Configures UART1 to **115,200 baud** (or up to 460,800 baud), 8N1, input protocols UBX+NMEA+RTCM, output protocols UBX+NMEA.
     - `UBX-CFG-RATE`: Sets measurement period to **100 ms** ($10\ \text{Hz}$ solution rate).
     - `UBX-CFG-MSG`: Disables verbose NMEA sentences (`$GxGSV`, `$GxGSA`, `$GxGLL`, `$GxVTG`), leaving only `$GxRMC` / `$GxGGA` and enabling binary `UBX-NAV-PVT`.
   - `Configured`: Stream is operating at peak 115,200 baud and 10 Hz navigation fixes.

### 3.2 UBX Protocol FSM & Fletcher-16 Checksum
The UBX stream parser is implemented as an explicit state machine:
$$\text{Sync1}\ (0\text{xB5}) \longrightarrow \text{Sync2}\ (0\text{x62}) \longrightarrow \text{Class} \longrightarrow \text{ID} \longrightarrow \text{LenLo} \longrightarrow \text{LenHi} \longrightarrow \text{Payload} \longrightarrow \text{CK\_A} \longrightarrow \text{CK\_B}$$

* **Fletcher-16 Checksum Algorithm**:
  ```python
  def ubx_fletcher16(data: bytes) -> tuple[int, int]:
      ck_a = 0
      ck_b = 0
      for byte in data:
          ck_a = (ck_a + byte) & 0xFF
          ck_b = (ck_b + ck_a) & 0xFF
      return (ck_a, ck_b)
  ```
* **`UBX-NAV-PVT` (Class `0x01`, ID `0x07`, Length 84/92 Bytes)**:
  Directly extracts:
  - `lon_1e7`, `lat_1e7`: WGS84 coordinates ($10^{-7}\ ^\circ$).
  - `h_msl_mm`: MSL Altitude ($1\ \text{mm} \rightarrow 0.001\ \text{m}$).
  - `vel_n_mms`, `vel_e_mms`, `vel_d_mms`: NED velocity components ($1\ \text{mm/s}$).
  - `gSpeed_mms`: Ground speed ($1\ \text{mm/s} \rightarrow \text{knots}$ and $\text{m/s}$).
  - `headMot_1e5`: Course Over Ground ($10^{-5}\ ^\circ \rightarrow 0.1^\circ$).
  - `numSV`: Satellites in solution.
  - `fixType`: `NoFix` (0), `Fix2D` (2), `Fix3D` (3), `Fix3DDGPS` (4/5).

---

## 4. QMC5883L Compass Ingestion & 200 Hz Maximization

### 4.1 Co-Processor Hardware Ingestion
* **Physical Bus**: TWI1 I2C @ 400 kHz Fast-Mode.
* **Continuous ODR Configuration**: `REG_CONTROL_1 = 0x1D` (Mode: Continuous, ODR: 200 Hz, RNG: 8 Gauss, OSR: 512).
* **Zero-Conversion Ingestion**: The co-processor reads 6 contiguous bytes (`0x00`..`0x05`):
  $$[X_L, X_H, Y_L, Y_H, Z_L, Z_H]$$
  and immediately pushes them into `/dev/rpmsg0` with zero CPU conversion.

### 4.2 Studio Vector Calibration & Heading Derivation
Inside the Studio plugin:
1. **Component Extraction**:
   $$X = \text{int16}(X_L \mid (X_H \ll 8)), \quad Y = \text{int16}(Y_L \mid (Y_H \ll 8)), \quad Z = \text{int16}(Z_L \mid (Z_H \ll 8))$$
2. **Hard-Iron Offset Calibration**:
   $$X_{\text{cal}} = X - X_{\text{offset}}, \quad Y_{\text{cal}} = Y - Y_{\text{offset}}$$
3. **Magnetic Heading Angle ($\psi$)**:
   $$\psi_{\text{mag}} = \left(\text{atan2}(Y_{\text{cal}}, X_{\text{cal}}) \cdot \frac{180}{\pi} + 360\right) \pmod{360}$$
4. **True Heading with Local Declination ($\Delta_{\text{decl}}$)**:
   $$\psi_{\text{true}} = (\psi_{\text{mag}} + \Delta_{\text{decl}}) \pmod{360}$$
5. **Dynamic Wind Drift / Crab Angle ($\delta_{\text{drift}}$)**:
   $$\delta_{\text{drift}} = \text{normalize\_angle}(\text{COG} - \psi_{\text{true}})$$

---

## 5. Vector-Rendered Compass Rose & HSI Graphics

The instrument is rendered using `imgui-bundle` `ImDrawList` primitives (matching professional aviation Primary Flight Display / HSI standards):

<!-- ascii-ok: sketch of the round compass rose instrument face, not a block diagram -->
```
                     [ 000° N ]  <-- Digital Heading Box
                         ▲ Fixed Lubber Line
                   /     |     \
                 /  NW   |   NE  \
                |        |        |
        [270° W]---------+---------[090° E]  <-- Rotating 360° Compass Rose
                |        |        |
                 \  SW   |   SE  /
                   \     |     /
                         ▼
                     [ 180° S ]
              ◆ Magenta Bug: GPS Ground Track (COG)
              --- Dashed Cyan: Magnetic Heading Bug
              --- Green Vector: Fused Ground Velocity
```

### 5.1 Graphic Components:
1. **Outer Rotating Rose**: 360° circular dial rotating smoothly with $-\psi_{\text{true}}$:
   - Major ticks every $30^\circ$ with numeric degree labels (`030`, `060`, `120`, etc.).
   - Minor ticks every $10^\circ$ and $5^\circ$.
   - Distinct cardinal markers: `N` (bright coral red `#E06C75`), `E`, `S`, `W` (crisp white `#FFFFFF`), and intercardinals `NE`, `SE`, `SW`, `NW` (slate blue `#61AFEF`).
2. **Fixed Aircraft Reticle & Lubber Line**:
   - Amber top-dead-center lubber line triangle pointing down at the current heading.
   - Central miniature aircraft silhouette reticle crosshairs.
3. **GPS Ground Track Bug (COG)**:
   - Magenta diamond bug positioned along the dial perimeter at $\text{COG} - \psi_{\text{true}}$.
   - Immediately reveals wind drift / crab angle: when ground track aligns with heading, $\delta_{\text{drift}} = 0^\circ$; crosswinds push the magenta bug left or right of the top lubber line.
4. **Dynamic Placards & Readouts**:
   - Digital Heading box with 3-digit zero-padded readout (e.g. `042°`).
   - Cardinal indicator (e.g. `NE`).
   - Ground Speed in knots and m/s (`SOG`).
   - Altitude MSL in meters and feet.
   - Raw Magnetometer $X, Y, Z$ counts and magnetic field magnitude $|\vec{B}|$ in $\mu\text{T}$.
   - GPS satellite count and 3D fix status badge.

---

## 6. Normative Specification Requirements Matrix

### `[SPEC-USER-PLUGIN-01]` Raw Telemetry Ingestion Contract & Packet Decoupling
* **Description**: The plugin MUST consume raw, unprocessed sensor packets forwarded from the co-processor over RPMsg or UDP. The co-processor firmware MUST NOT perform NMEA string parsing, floating-point trigonometry, or heading calculations; all parsing, floating-point calibration, and vector fusion MUST execute host-side inside the plugin.
* **Method**: Demonstration
* **Refines**: `[SPEC-STUDIO-03]`, `[SPEC-ARCH-08]`
* **Verified by**: `host-tests`
* **Implementation Target**: `tools/visualizer/flight_plugin.py`

### `[SPEC-USER-PLUGIN-02]` Simultaneous High-Speed Peripheral Configuration
* **Description**: The plugin MUST provide an automated configuration interface to command both peripherals to their maximum operational speeds simultaneously:
  1. u-blox NEO-M8N GPS elevated from 9,600 baud to $\ge 115,200\ \text{baud}$ via `UBX-CFG-PRT`, with navigation solution rate elevated to $10\ \text{Hz}$ via `UBX-CFG-RATE`, and unneeded sentences (`GSV`, `GSA`) suppressed.
  2. QMC5883L Compass elevated to $200\ \text{Hz}$ continuous Output Data Rate (ODR) on a $400\ \text{kHz}$ Fast-Mode I2C bus via `REG_CONTROL_1` (`0x1D`).
* **Method**: Test
* **Refines**: `[SPEC-GPS-01]`, `[SPEC-MAG-01]`
* **Verified by**: `host-tests`
* **Implementation Target**: `tools/visualizer/flight_plugin.py`

### `[SPEC-USER-PLUGIN-03]` NMEA 0183 & UBX Protocol Parser Engine
* **Description**: The plugin MUST include a high-throughput, error-tolerant stream decoder supporting NMEA 0183 sentences (`$GxRMC`, `$GxGGA`, `$GxVTG`) and UBX binary packets (`NAV-PVT` 84/92-byte and `NAV-DOP`) with Fletcher-16 checksum verification. It MUST extract latitude, longitude, altitude MSL, Ground Speed (SOG), Course Over Ground (COG), fix validity, and satellite count without blocking the 60 FPS UI rendering thread.
* **Method**: Test
* **Refines**: `[SPEC-STUDIO-07]`
* **Verified by**: `host-tests`
* **Implementation Target**: `tools/visualizer/flight_plugin.py`

### `[SPEC-USER-PLUGIN-04]` Magnetic Field Vector Calibration & Heading Derivation
* **Description**: The plugin MUST extract 16-bit signed magnetometer components ($X, Y, Z$) from raw 6-byte packets, apply hard-iron offset corrections, compute total field magnitude $|\vec{B}|$, compute calibrated magnetic heading $\psi \in [0^\circ, 359^\circ]$, and determine 8-point cardinal orientation.
* **Method**: Analysis
* **Refines**: `[SPEC-STUDIO-03]`
* **Verified by**: `host-tests`
* **Implementation Target**: `tools/visualizer/flight_plugin.py`

### `[SPEC-USER-PLUGIN-05]` Vector-Rendered Rotating Compass Rose & Heading Dial
* **Description**: The plugin MUST render a high-precision, anti-aliased circular Compass Rose using `imgui-bundle` `ImDrawList`. The dial face MUST rotate smoothly with magnetic heading, displaying major ticks every $30^\circ$, minor ticks every $10^\circ$, cardinal markers (`N`, `E`, `S`, `W`), and a top-referenced lubber line with digital heading placard.
* **Method**: Demonstration
* **Refines**: `[SPEC-STUDIO-03]`
* **Verified by**: `host-tests`
* **Implementation Target**: `tools/visualizer/flight_plugin.py`

### `[SPEC-USER-PLUGIN-06]` GPS Track Bug (COG) & Fused Wind Drift Overlay
* **Description**: The Compass Rose MUST overlay a magenta GPS Ground Track bug (Course Over Ground) along the dial perimeter and compute the dynamic wind drift / crab angle ($\delta = \text{COG} - \psi$). When GPS speed is under $0.5\ \text{m/s}$, the bug MUST indicate stationary mode.
* **Method**: Demonstration
* **Refines**: `[SPEC-STUDIO-03]`
* **Verified by**: `host-tests`
* **Implementation Target**: `tools/visualizer/flight_plugin.py`

### `[SPEC-USER-PLUGIN-07]` AbstractX Studio Plugin SDK Integration & Canvas Docking
* **Description**: The plugin MUST inherit from `AbstractXStudioPlugin`, implement `render_ui(delta_time, state)` cleanly, synchronize state updates using `TelemetryState.lock`, and support rendering within the User Domain Instruments window (`MainDockSpace`) as well as popping out into an independent floating canvas window.
* **Method**: Demonstration
* **Refines**: `[SPEC-STUDIO-06]`
* **Verified by**: `host-tests`
* **Implementation Target**: `tools/visualizer/flight_plugin.py`
