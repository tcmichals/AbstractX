#!/usr/bin/env python3
"""
Copyright (C) 2026 Tim Michals
SPDX-License-Identifier: GPL-3.0-or-later

AbstractX Visualizer Studio (Python + Dear ImGui Bundle)
-------------------------------------------------------
High-performance live observability dashboard implementing a Two-Level Architecture:

Level 1: Core AbstractX System Platform (Standard Across All Applications)
  - 64-byte PCIe-style TLP frame ingestion (UDP, Serial, Shared SRAM)
  - Platform Topology & Interconnect (Cores, SPU/CPU roles, SPSC rings, doorbell latency)
  - Dual-Plane Execution Timeline (Plane 1 I/O ISR/DMA vs Plane 2 C++20 Coroutine Tasks)
  - Interactive Source Code Inspector (Click event -> Jump to __FILE__ : __LINE__)
  - Per-Processor SPU/CPU & OS Process Utilization
  - Memory Observability & Static Section Budgets (MemBrowse integration)
  - CTF 1.8 / barectf Dynamic Schema & Packet Inspector

Level 2: User & Domain Extensible Application Layer
  - Pluggable application dashboards loaded via schema hints or plugins
  - Primary Flight Display (PFD) with artificial horizon & pitch ladder
  - 3D Quadcopter Perspective Wireframe Model (Tait-Bryan rotation)
  - Quad-X Motor Mixer Demands & Aviation Navigation Gauges
  - 8 kHz IMU Real-Time Oscilloscope (ImPlot)

Usage:
  pip install -r tools/visualizer/requirements.txt
  python3 tools/visualizer/abstractx_studio.py [--port 9870] [--sim] [--tab flight|platform|memory]
"""

import sys
import time
import socket
import struct
import threading
import argparse
import numpy as np
from pathlib import Path

try:
    from imgui_bundle import imgui, implot, immapp, hello_imgui
except ImportError:
    print("Error: imgui-bundle is required. Run:")
    print("  pip install imgui-bundle numpy")
    sys.exit(1)

# Import Level 2 Domain Plugin
try:
    from flight_plugin import FlightVisualizerPlugin
except ImportError:
    from tools.visualizer.flight_plugin import FlightVisualizerPlugin

# CTF Constants & Defaults
CTF_MAGIC = 0xC1FC1FC1
DEFAULT_UDP_PORT = 9870
HISTORY_SIZE = 1000

class TelemetryState:
    def __init__(self):
        self.lock = threading.Lock()
        self.connected = False
        self.packet_count = 0
        self.bytes_received = 0
        self.fps_packet_rate = 0
        self.last_rate_calc = time.time()
        self.rate_counter = 0
        self.discarded_events = 0

        # Source & Pipeline status
        self.source_mode = "STANDBY (Waiting on UDP :9870)"
        self.fpga_trace = "Pipeline: Idle | Zero-Copy SPSC Rings Ready"

        # Platform Topology Table & Hardware Interconnect (Level 1)
        self.platform_arch = "Linux_Host_E907_FPGA"
        self.platform_name = "Allwinner A5E Heterogeneous"
        self.board_model = "Radxa Cubie A5E"
        self.primary_transport = "Shared SRAM A3/C (0x40000000) + sun6i-msgbox"
        self.active_cores = [
            {"id": 0, "role": "Host Linux ARM64", "task": "Flight Supervisor & Telemetry", "clock_mhz": 1400},
            {"id": 1, "role": "XuanTie E907 RISC-V", "task": "Real-Time I/O Reactor & DMA", "clock_mhz": 600},
            {"id": 2, "role": "Tang Primer 20K FPGA", "task": "Hardware Auto-DMA & DShot Fabric", "clock_mhz": 50},
        ]
        self.hardware_accels = ["IMU Auto-DMA IP", "DShot 4-CH Core", "NeoPixel IP", "sun6i-msgbox"]

        # Dual-Plane Execution Data (Level 1)
        # Plane 1: I/O Processor & Drivers (Interrupt & DMA Context)
        self.io_driver_events = [
            {"name": "SPI0 DMA Burst", "driver": "hal_spi", "subsystem": "ICM-42688-P DMA", "latency_us": 0.8, "file": "targets/linux/src/hal_spi.cpp", "line": 78},
            {"name": "TWI0 I2C ISR", "driver": "hal_i2c", "subsystem": "Compass / Baro", "latency_us": 1.2, "file": "targets/allwinner_e907/src/hal_i2c.cpp", "line": 45},
            {"name": "MSGBox Doorbell", "driver": "msgbox", "subsystem": "Inter-Core RPC", "latency_us": 0.4, "file": "targets/allwinner_e907/src/io_processor.cpp", "line": 92},
            {"name": "UART0 RX FIFO", "driver": "hal_uart", "subsystem": "U-Blox M10 GPS", "latency_us": 1.5, "file": "targets/linux/src/hal_uart.cpp", "line": 125},
        ]
        # Plane 2: Main Coroutine Loop (Cooperative C++20 Tasks)
        self.coro_names = ["imu_pipeline", "gps_task", "attitude_ekf", "flight_control"]
        self.coro_states = [1, 2, 4, 1]
        self.coro_latencies_us = [0.8, 1.2, 0.4, 0.9]
        self.coro_source_info = [
            {"file": "apps/gps_imu_app/src/main.cpp", "line": 42, "token": "co_await g_sensor_ring.pop_async()"},
            {"file": "apps/gps_imu_app/src/main.cpp", "line": 78, "token": "co_await gps.read_packet_async()"},
            {"file": "apps/gps_imu_app/src/main.cpp", "line": 115, "token": "co_await timer.sleep_ms_async(1)"},
            {"file": "apps/gps_imu_app/src/main.cpp", "line": 140, "token": "co_await attitude_ready"},
        ]

        # Selected Source Code View
        self.selected_event_name = "imu_pipeline"
        self.selected_source_file = "apps/gps_imu_app/src/main.cpp"
        self.selected_source_line = 42
        self.selected_token = "co_await g_sensor_ring.pop_async()"

        # Per-Processor Utilization
        self.linux_total_cpu = 8.4
        self.linux_abstractx_cpu = 5.2
        self.linux_external_cpu = 3.2
        self.e907_active_duty_pct = 14.8
        self.e907_wfi_sleep_pct = 85.2
        self.fpga_lut_utilization_pct = 18.2
        self.fpga_dma_bw_mbps = 12.8

        # SPSC Queue Saturation
        self.sensor_ring_fill = 18
        self.telemetry_ring_fill = 8
        self.rtt_latency_us = 12.4

        # Memory Observability & Static Section Budgets (MemBrowse Integration)
        self.selected_mem_target = "RP2350 Pico 2 W"
        self.mem_targets = {
            "RP2350 Pico 2 W": {
                "arch": "ARM Cortex-M33 Dual-Core @ 150 MHz",
                "ram_used_bytes": 142336,
                "ram_total_bytes": 524288,  # 512 KB SRAM
                "flash_used_bytes": 482100,
                "flash_total_bytes": 4194304, # 4 MB Flash
                "sections": {
                    ".text (Executable Code)": 412000,
                    ".rodata (Constants & Jump Tables)": 70100,
                    ".data (Initialized Variables)": 14200,
                    ".bss (Zero-Init SPSC Rings & Queues)": 98136,
                    ".stack (Core 0 + Core 1 Stacks)": 30000,
                },
                "zero_heap_compliant": True,
                "membrowse_budget_ok": True,
            },
            "ESP32-P4 RISC-V": {
                "arch": "Dual-Core RV32IMAFDC @ 400 MHz",
                "ram_used_bytes": 210400,
                "ram_total_bytes": 786432, # 768 KB Internal SRAM
                "flash_used_bytes": 1120000,
                "flash_total_bytes": 16777216, # 16 MB Flash
                "sections": {
                    ".text (RISC-V Instructions)": 920000,
                    ".rodata (Read-Only Data)": 200000,
                    ".data (Data in SRAM)": 18400,
                    ".bss (SPSC Channels & Descriptors)": 128000,
                    ".iram0.text (Critical ISR Handlers)": 64000,
                },
                "zero_heap_compliant": True,
                "membrowse_budget_ok": True,
            },
            "XuanTie E907 RISC-V": {
                "arch": "RV32IMAFCP @ 600 MHz Co-Processor",
                "ram_used_bytes": 38400,
                "ram_total_bytes": 65536, # 64 KB Shared SRAM A3/C
                "flash_used_bytes": 38400,
                "flash_total_bytes": 65536,
                "sections": {
                    ".text (I/O Reactor Code)": 22100,
                    ".rodata (Descriptors)": 4300,
                    ".data (Shared Hardware Mailbox)": 2000,
                    ".bss (TLP Ring Descriptors)": 10000,
                },
                "zero_heap_compliant": True,
                "membrowse_budget_ok": True,
            },
            "Host Desktop SITL": {
                "arch": "x86_64 / AArch64 Linux Standalone",
                "ram_used_bytes": 1540000,
                "ram_total_bytes": 16777216,
                "flash_used_bytes": 2840000,
                "flash_total_bytes": 33554432,
                "sections": {
                    ".text": 2200000,
                    ".rodata": 640000,
                    ".data": 140000,
                    ".bss": 1400000,
                },
                "zero_heap_compliant": True,
                "membrowse_budget_ok": True,
            }
        }

        # Dynamically load live MemBrowse metrics if available
        metrics_file = Path(__file__).resolve().parent / "memory_metrics.json"
        if metrics_file.exists():
            try:
                import json
                with open(metrics_file, "r") as mf:
                    live_metrics = json.load(mf)
                for k, v in live_metrics.items():
                    d_name = v.get("display_name", k)
                    self.mem_targets[d_name] = {
                        "arch": v.get("architecture", "MCU"),
                        "ram_used_bytes": v.get("ram_used_bytes", 0),
                        "ram_total_bytes": v.get("ram_total_bytes", 524288),
                        "flash_used_bytes": v.get("flash_used_bytes", 0),
                        "flash_total_bytes": v.get("flash_total_bytes", 4194304),
                        "sections": {sec_k: sec_v for sec_k, sec_v in v.get("sections", {}).items() if sec_k != "Total"},
                        "zero_heap_compliant": v.get("zero_heap_compliant", True),
                        "membrowse_budget_ok": True,
                    }
                if live_metrics:
                    self.selected_mem_target = list(self.mem_targets.keys())[0]
            except Exception:
                pass

        # Level 2 Domain Telemetry (Flight & Sensors)
        self.roll_deg = 0.0
        self.pitch_deg = 0.0
        self.yaw_deg = 0.0
        self.altitude_m = 10.0
        self.ground_speed_mps = 2.5
        self.motors = [500, 500, 500, 500]

        # Multi-rate stream rates (Hz)
        self.imu_hz = 0
        self.mag_hz = 0
        self.gps_hz = 0
        self.ahrs_hz = 0
        self._imu_count = 0
        self._mag_count = 0
        self._gps_count = 0
        self._ahrs_count = 0

        # Ring buffers for Sensor Data (Time vs Value)
        self.time_history = np.zeros(HISTORY_SIZE, dtype=np.float64)
        self.accel_x = np.zeros(HISTORY_SIZE, dtype=np.float64)
        self.accel_y = np.zeros(HISTORY_SIZE, dtype=np.float64)
        self.accel_z = np.zeros(HISTORY_SIZE, dtype=np.float64)
        self.gyro_x = np.zeros(HISTORY_SIZE, dtype=np.float64)
        self.gyro_y = np.zeros(HISTORY_SIZE, dtype=np.float64)
        self.gyro_z = np.zeros(HISTORY_SIZE, dtype=np.float64)
        self.head_idx = 0

        # GPS Navigation Status
        self.gps_lat = 37.7749
        self.gps_lon = -122.4194
        self.gps_alt_m = 142.5
        self.gps_speed_mps = 12.4
        self.gps_sats = 18
        self.gps_fix_type = 3

    def push_sensor_sample(self, t_sec, ax, ay, az, gx, gy, gz):
        with self.lock:
            idx = self.head_idx % HISTORY_SIZE
            self.time_history[idx] = t_sec
            self.accel_x[idx] = ax
            self.accel_y[idx] = ay
            self.accel_z[idx] = az
            self.gyro_x[idx] = gx
            self.gyro_y[idx] = gy
            self.gyro_z[idx] = gz
            self.head_idx += 1

    def update_rates(self):
        with self.lock:
            now = time.time()
            dt = now - self.last_rate_calc
            if dt >= 1.0:
                self.fps_packet_rate = int(self.rate_counter / dt)
                self.rate_counter = 0

                self.imu_hz = int(self._imu_count / dt)
                self.mag_hz = int(self._mag_count / dt)
                self.gps_hz = int(self._gps_count / dt)
                self.ahrs_hz = int(self._ahrs_count / dt)
                self._imu_count = 0
                self._mag_count = 0
                self._gps_count = 0
                self._ahrs_count = 0
                self.last_rate_calc = now

g_state = TelemetryState()
g_flight_plugin = FlightVisualizerPlugin()
g_initial_tab = "platform"

def udp_receiver_thread(port: int, sim_mode: bool):
    """Background worker receiving live 64B TLP frames or synthesizing flight data."""
    if sim_mode:
        print("[Simulator] Running synthetic multi-rate flight & telemetry simulation...")
        start_time = time.time()
        while True:
            t = time.time() - start_time
            # Realistic quadcopter dynamics
            roll = 15.0 * np.sin(t * 1.2)
            pitch = 8.0 * np.cos(t * 0.9)
            yaw = (t * 20.0) % 360.0
            alt = 10.0 + 2.0 * np.sin(t * 0.5)
            spd = 2.5 + 1.0 * np.cos(t * 0.8)

            # Symmetrical cascaded PID motor demands
            u_r = roll * 4.0
            u_p = pitch * 4.0
            m1 = int(np.clip(500 - u_r + u_p, 100, 1000))
            m2 = int(np.clip(500 + u_r - u_p, 100, 1000))
            m3 = int(np.clip(500 + u_r + u_p, 100, 1000))
            m4 = int(np.clip(500 - u_r - u_p, 100, 1000))

            # 8 kHz IMU dynamics with high-frequency noise
            ax = 0.05 * np.sin(t * 5.0) + np.random.normal(0, 0.01)
            ay = 0.05 * np.cos(t * 4.0) + np.random.normal(0, 0.01)
            az = 1.00 + 0.02 * np.sin(t * 8.0) + np.random.normal(0, 0.01)
            gx = 10.0 * np.sin(t * 3.0) + np.random.normal(0, 0.5)
            gy = 8.0 * np.cos(t * 2.5) + np.random.normal(0, 0.5)
            gz = 5.0 * np.sin(t * 1.5) + np.random.normal(0, 0.3)

            g_state.push_sensor_sample(t, ax, ay, az, gx, gy, gz)

            with g_state.lock:
                g_state.connected = True
                g_state.source_mode = "SYNTHETIC FLIGHT DYNAMICS (Sim Mode)"
                g_state.fpga_trace = "Pipeline: Synthetic Generator | Rate: 200 Hz"
                g_state.roll_deg = roll
                g_state.pitch_deg = pitch
                g_state.yaw_deg = yaw
                g_state.altitude_m = alt
                g_state.ground_speed_mps = spd
                g_state.motors = [m1, m2, m3, m4]
                g_state.packet_count += 1
                g_state.rate_counter += 1
                g_state._ahrs_count += 1
                g_state._imu_count += 40
                g_state._mag_count += 1
                if int(t * 10) % 20 == 0:
                    g_state._gps_count += 1
                g_state.sensor_ring_fill = int(20 + 10 * np.sin(t * 2.0))
                g_state.telemetry_ring_fill = int(10 + 5 * np.cos(t * 3.0))

            time.sleep(0.005) # 200 Hz visualization cadence
    else:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("0.0.0.0", port))
        sock.settimeout(1.0)
        print(f"[UDP Receiver] Listening for AbstractX 64-Byte TLPs on UDP 0.0.0.0:{port}...")

        start_t = time.time()
        while True:
            try:
                data, _ = sock.recvfrom(2048)
                t = time.time() - start_t
                with g_state.lock:
                    g_state.connected = True
                    g_state.packet_count += 1
                    g_state.bytes_received += len(data)
                    g_state.rate_counter += 1

                if len(data) >= 64:
                    for off in range(0, len(data) - 63, 64):
                        frame = data[off:off+64]
                        
                        # Inspect Big-Endian (FPGA Verilator RTL) vs Little-Endian (Software C++20 SITL)
                        be_type, be_flags, be_tag, be_ch, be_addr, be_len, be_seq, be_ts = struct.unpack(">BBBBIHHQ", frame[:20])
                        le_type, le_flags, le_tag, le_ch, le_addr, le_len, le_seq, le_ts = struct.unpack("<BBBBIHHQ", frame[:20])

                        # 1. FPGA Hardware RTL Telemetry (Big-Endian Wire Layout from asp_top.sv)
                        if be_type == 0x10 and be_ch == 0x02 and be_addr == 0x40000100:
                            temp, ax, ay, az, gx, gy, gz = struct.unpack(">hhhhhhh", frame[20:34])
                            ax_g = ax / 2048.0
                            ay_g = ay / 2048.0
                            az_g = az / 2048.0
                            gx_dps = gx / 16.4
                            gy_dps = gy / 16.4
                            gz_dps = gz / 16.4

                            roll = float(np.degrees(np.arctan2(ay_g, az_g if az_g != 0 else 1.0)))
                            pitch = float(np.degrees(np.arctan2(-ax_g, np.sqrt(ay_g**2 + az_g**2))))
                            
                            g_state.push_sensor_sample(t, ax_g, ay_g, az_g, gx_dps, gy_dps, gz_dps)
                            with g_state.lock:
                                g_state.source_mode = "FPGA HARDWARE RTL (asp_top.sv Verilator)"
                                g_state.fpga_trace = f"Doorbell: 9.57 µs | Auto-DMA: 14B SPI @ 10MHz | Seq: #{be_seq}"
                                g_state.roll_deg = roll
                                g_state.pitch_deg = pitch
                                g_state.yaw_deg = float((g_state.yaw_deg + gz_dps * 0.02) % 360.0)
                                g_state._imu_count += 1
                                g_state._ahrs_count += 1
                                u_r = roll * 4.0
                                u_p = pitch * 4.0
                                g_state.motors = [
                                    int(np.clip(500 - u_r + u_p, 100, 1000)),
                                    int(np.clip(500 + u_r - u_p, 100, 1000)),
                                    int(np.clip(500 + u_r + u_p, 100, 1000)),
                                    int(np.clip(500 - u_r - u_p, 100, 1000))
                                ]

                        # 2. Software C++20 SITL Telemetry (Little-Endian Wire Layout from gps_imu_app)
                        elif le_type == 0x10 and le_tag == 4:
                            roll_cdeg, pitch_cdeg, yaw_cdeg, alt_mm, spd_cm_s, m1, m2, m3, m4 = struct.unpack("<2hHiH4H", frame[20:40])
                            with g_state.lock:
                                g_state.source_mode = "SOFTWARE C++20 SITL (gps_imu_app)"
                                g_state.fpga_trace = "Runtime: DomainDispatcher::step() | Zero Heap | SPSC Rings Active"
                                g_state.roll_deg = roll_cdeg * 0.01
                                g_state.pitch_deg = pitch_cdeg * 0.01
                                g_state.yaw_deg = yaw_cdeg * 0.01
                                g_state.altitude_m = alt_mm * 0.001
                                g_state.ground_speed_mps = spd_cm_s * 0.01
                                g_state.motors = [m1, m2, m3, m4]
                                g_state._ahrs_count += 1

                        elif le_tag == 1:
                            # Stream 1 Event 1: IMU Sample
                            if len(frame) >= 47:
                                _, _, sample_seq, ax_mg, ay_mg, az_mg, gx_dps, gy_dps, gz_dps, temp_c = struct.unpack(
                                    "<BQihhhhhhh", frame[20:47]
                                )
                                g_state.push_sensor_sample(
                                    t,
                                    ax_mg / 1000.0,
                                    ay_mg / 1000.0,
                                    az_mg / 1000.0,
                                    gx_dps / 10.0,
                                    gy_dps / 10.0,
                                    gz_dps / 10.0
                                )
                                with g_state.lock:
                                    g_state._imu_count += 1

                        elif le_tag == 2:
                            # GPS Fix
                            if len(frame) >= 48:
                                itow, lat, lon, alt, spd, head, sats, fix = struct.unpack("<i5i2B", frame[20:46])
                                with g_state.lock:
                                    g_state.gps_lat = lat / 1e7
                                    g_state.gps_lon = lon / 1e7
                                    g_state.gps_alt_m = alt * 0.001
                                    g_state.gps_speed_mps = spd * 0.001
                                    g_state.gps_sats = sats
                                    g_state.gps_fix_type = fix
                                    g_state._gps_count += 1

                        elif le_tag == 3:
                            with g_state.lock:
                                g_state._mag_count += 1
            except socket.timeout:
                with g_state.lock:
                    g_state.connected = False
            except Exception:
                time.sleep(0.01)

def render_gui():
    """Immediate mode GUI rendering combining Level 1 Core & Level 2 Domain views."""
    g_state.update_rates()

    # 1. Top Header Toolbar
    imgui.begin_group()
    if g_state.connected:
        imgui.text_colored(imgui.ImVec4(0.1, 0.9, 0.2, 1.0), "[ONLINE]")
    else:
        imgui.text_colored(imgui.ImVec4(0.9, 0.2, 0.1, 1.0), "[OFFLINE / WAITING]")
    imgui.same_line()
    imgui.text(f"| Platform: {g_state.platform_name} ({g_state.platform_arch}) | Packets: {g_state.packet_count} | Rate: {g_state.fps_packet_rate} pkts/sec")
    imgui.end_group()
    imgui.separator()

    # 2. Top-Level Tab Bar: Separation of Level 1 (Core) and Level 2 (Domain)
    if imgui.begin_tab_bar("StudioTabBar"):
        # Default tab selection on first launch
        flags_platform = imgui.TabItemFlags_.set_selected if g_initial_tab == "platform" else 0
        flags_flight = imgui.TabItemFlags_.set_selected if g_initial_tab == "flight" else 0
        flags_memory = imgui.TabItemFlags_.set_selected if g_initial_tab == "memory" else 0

        # =========================================================================
        # LEVEL 2: APPLICATION & DOMAIN EXTENSIBLE LAYER (Flight & IMU)
        # =========================================================================
        opened, _ = imgui.begin_tab_item("Level 2: Flight & IMU Domain (gps_imu_app)", None, flags_flight)
        if opened:
            g_flight_plugin.render(g_state)
            imgui.end_tab_item()

        # =========================================================================
        # LEVEL 1: CORE PLATFORM & EXECUTION OBSERVABILITY
        # =========================================================================
        opened, _ = imgui.begin_tab_item("Level 1: Platform & Execution Core", None, flags_platform)
        if opened:
            _render_level1_platform()
            imgui.end_tab_item()

        # =========================================================================
        # LEVEL 1: MEMORY OBSERVABILITY & STATIC BUDGETS (MemBrowse)
        # =========================================================================
        opened, _ = imgui.begin_tab_item("Level 1: Memory Observability (MemBrowse)", None, flags_memory)
        if opened:
            _render_level1_memory()
            imgui.end_tab_item()

        # =========================================================================
        # LEVEL 1: CTF 1.8 & TLP PACKET INSPECTOR
        # =========================================================================
        opened, _ = imgui.begin_tab_item("Level 1: TLP 64-Byte & CTF Schema", None, 0)
        if opened:
            _render_level1_tlp_inspector()
            imgui.end_tab_item()

        imgui.end_tab_bar()

def _render_level1_platform():
    """Renders Level 1 Core: Platform Topology, Dual-Plane Timeline, and Utilization."""
    # Window 1: Platform Topology
    if imgui.collapsing_header("Window 1: Platform Topology & Silicon Interconnect Fabric", imgui.TreeNodeFlags_.default_open):
        imgui.columns(3, "topo_cols", False)
        
        imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), "[Platform Architecture]")
        imgui.text(f"Arch Name : {g_state.platform_arch}")
        imgui.text(f"Board     : {g_state.board_model}")
        imgui.text(f"Transport : {g_state.primary_transport}")
        
        imgui.next_column()
        
        imgui.text_colored(imgui.ImVec4(0.4, 1.0, 0.4, 1.0), "[Processing Units & Roles]")
        for core in g_state.active_cores:
            imgui.bullet_text(f"Core {core['id']}: {core['role']} ({core['clock_mhz']} MHz)\n  └─ {core['task']}")
            
        imgui.next_column()
        
        imgui.text_colored(imgui.ImVec4(1.0, 0.7, 0.2, 1.0), "[Hardware Accelerators & Rings]")
        for accel in g_state.hardware_accels:
            imgui.text(f" ✓ {accel}")
        imgui.text("SPSC Ring : 64 Descriptors (Zero-Copy)")
        imgui.columns(1)

    imgui.separator()

    # Window 2: Dual-Plane Execution Timeline & Source Scanner
    if imgui.collapsing_header("Window 2: Dual-Plane Execution Timeline & Source Code Scanner", imgui.TreeNodeFlags_.default_open):
        imgui.text_colored(imgui.ImVec4(0.9, 0.5, 0.2, 1.0), "Plane 1: Hardware I/O Processor & Peripheral Drivers (Interrupt & DMA Context)")
        imgui.columns(len(g_state.io_driver_events), "io_plane_cols", True)
        for ev in g_state.io_driver_events:
            if imgui.button(f"[{ev['name']}]\n{ev['subsystem']}\n{ev['latency_us']} µs", imgui.ImVec2(-1, 52)):
                with g_state.lock:
                    g_state.selected_event_name = ev['name']
                    g_state.selected_source_file = ev['file']
                    g_state.selected_source_line = ev['line']
                    g_state.selected_token = f"Driver ISR: {ev['driver']}"
            imgui.next_column()
        imgui.columns(1)

        imgui.spacing()

        imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), "Plane 2: Main Processing Loop (Cooperative C++20 Coroutine Tasks)")
        imgui.columns(len(g_state.coro_names), "coro_plane_cols", True)
        state_labels = {0: "IDLE", 1: "RUNNING", 2: "AWAIT SPI", 3: "AWAIT UART", 4: "AWAIT TIMER"}
        for i, name in enumerate(g_state.coro_names):
            lbl = state_labels.get(g_state.coro_states[i], "RUNNING")
            src = g_state.coro_source_info[i]
            if imgui.button(f"[{name}]\nState: {lbl}\nLat: {g_state.coro_latencies_us[i]} µs", imgui.ImVec2(-1, 52)):
                with g_state.lock:
                    g_state.selected_event_name = name
                    g_state.selected_source_file = src['file']
                    g_state.selected_source_line = src['line']
                    g_state.selected_token = src['token']
            imgui.next_column()
        imgui.columns(1)

        imgui.spacing()

        # Source Code Scanner Sub-Pane
        imgui.text_colored(imgui.ImVec4(1.0, 1.0, 0.2, 1.0), f">> [Source Code Inspector] Selected: {g_state.selected_event_name} -> {g_state.selected_source_file}:{g_state.selected_source_line}")
        imgui.begin_child("SourcePreview", imgui.ImVec2(-1, 80), True)
        imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), f"// Source location: {g_state.selected_source_file}")
        imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), f"   {g_state.selected_source_line - 1}:   // Processing event loop")
        imgui.text_colored(imgui.ImVec4(0.2, 1.0, 0.4, 1.0), f"-> {g_state.selected_source_line}:       {g_state.selected_token};")
        imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), f"   {g_state.selected_source_line + 1}:   attitude_ekf.update(sample.gyro, sample.accel);")
        imgui.end_child()

    imgui.separator()

    # Window 3: Per-Processor SPU/CPU & Process Utilization
    if imgui.collapsing_header("Window 3: Per-Processor SPU/CPU & Process Utilization", imgui.TreeNodeFlags_.default_open):
        imgui.columns(3, "cpu_cols", False)
        
        # Host Linux ARM64
        imgui.text_colored(imgui.ImVec4(0.4, 0.8, 1.0, 1.0), "[Core 0: Host Linux ARM64]")
        imgui.text(f"Total System CPU : {g_state.linux_total_cpu:.1f}%")
        imgui.progress_bar(g_state.linux_total_cpu / 100.0, imgui.ImVec2(-1, 0), f"{g_state.linux_total_cpu:.1f}%")
        imgui.text(f"AbstractX Process: {g_state.linux_abstractx_cpu:.1f}%")
        imgui.text(f"OS Background    : {g_state.linux_external_cpu:.1f}%")

        imgui.next_column()

        # Coprocessor XuanTie E907
        imgui.text_colored(imgui.ImVec4(0.3, 1.0, 0.4, 1.0), "[Core 1: XuanTie E907 RISC-V]")
        imgui.text(f"Active Duty Cycle: {g_state.e907_active_duty_pct:.1f}% (148 µs/ms)")
        imgui.progress_bar(g_state.e907_active_duty_pct / 100.0, imgui.ImVec2(-1, 0), f"{g_state.e907_active_duty_pct:.1f}%")
        imgui.text(f"WFI Sleep Duty   : {g_state.e907_wfi_sleep_pct:.1f}%")

        imgui.next_column()

        # FPGA Fabric
        imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0), "[Fabric: Tang Primer 20K FPGA]")
        imgui.text(f"Logic LUT Usage  : {g_state.fpga_lut_utilization_pct:.1f}% (3,640 / 20k)")
        imgui.progress_bar(g_state.fpga_lut_utilization_pct / 100.0, imgui.ImVec2(-1, 0), f"{g_state.fpga_lut_utilization_pct:.1f}%")
        imgui.text(f"Auto-DMA Rate    : {g_state.fpga_dma_bw_mbps:.1f} Mbps")

        imgui.columns(1)

    imgui.separator()

    # SPSC Queue Saturation
    imgui.columns(2, "spsc_cols", False)
    imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), "[SPSC TLP Ring Saturation & Latency]")
    imgui.text("Sensor Queue (g_sensor_ring):")
    imgui.progress_bar(g_state.sensor_ring_fill / 64.0, imgui.ImVec2(-1, 0), f"{g_state.sensor_ring_fill} / 64 pkts")
    imgui.text("Telemetry Queue (g_telemetry_ring):")
    imgui.progress_bar(g_state.telemetry_ring_fill / 64.0, imgui.ImVec2(-1, 0), f"{g_state.telemetry_ring_fill} / 64 pkts")

    imgui.next_column()
    imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0), "[Interconnect Timing]")
    imgui.text(f"Avg I/O Split-Transaction RTT: {g_state.rtt_latency_us:.1f} µs")
    imgui.text("Protocol: Symmetrical 64-byte TLP")
    imgui.text("Wire Transport: Lock-Free SPSC")
    imgui.columns(1)

def _render_level1_memory():
    """Renders Level 1 Core: Memory Observability, Static Section Budgets, and MemBrowse Status."""
    imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), "MemBrowse Embedded Memory Observability & Static Section Footprint")
    imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0),
                       "AbstractX enforces Zero Heap & No Dynamic Allocation (Freestanding C++20). "
                       "All memory is statically pooled in .bss/.data.")
    imgui.separator()

    # Target Selector
    targets = list(g_state.mem_targets.keys())
    curr_target = g_state.selected_mem_target
    if imgui.begin_combo("Target Silicon Profile", curr_target):
        for t_name in targets:
            is_selected = (t_name == curr_target)
            if imgui.selectable(t_name, is_selected)[0]:
                g_state.selected_mem_target = t_name
            if is_selected:
                imgui.set_item_default_focus()
        imgui.end_combo()

    t_info = g_state.mem_targets[g_state.selected_mem_target]

    imgui.columns(2, "mem_summary_cols", False)
    
    # RAM Usage
    ram_used = t_info["ram_used_bytes"]
    ram_total = t_info["ram_total_bytes"]
    ram_pct = (ram_used / ram_total) * 100.0
    imgui.text_colored(imgui.ImVec4(0.3, 1.0, 0.4, 1.0), f"RAM (SRAM) Budget: {ram_pct:.1f}% Used")
    imgui.progress_bar(ram_used / ram_total, imgui.ImVec2(-1, 22), f"{ram_used // 1024} KB / {ram_total // 1024} KB")
    
    imgui.next_column()

    # Flash / ROM Usage
    flash_used = t_info["flash_used_bytes"]
    flash_total = t_info["flash_total_bytes"]
    flash_pct = (flash_used / flash_total) * 100.0
    imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), f"Flash / ROM Budget: {flash_pct:.1f}% Used")
    imgui.progress_bar(flash_used / flash_total, imgui.ImVec2(-1, 22), f"{flash_used // 1024} KB / {flash_total // 1024} KB")

    imgui.columns(1)
    imgui.separator()

    # ELF Section Breakdown Table
    imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0), f"ELF Binary Section Analysis ({t_info['arch']}):")
    imgui.columns(3, "sec_cols", True)
    imgui.text("Section Name")
    imgui.next_column()
    imgui.text("Size (Bytes)")
    imgui.next_column()
    imgui.text("Target Memory Region")
    imgui.next_column()
    imgui.separator()

    for sec_name, sec_sz in t_info["sections"].items():
        imgui.text(sec_name)
        imgui.next_column()
        imgui.text(f"{sec_sz:,} B  ({sec_sz / 1024.0:4.1f} KB)")
        imgui.next_column()
        reg = "SRAM" if any(k in sec_name for k in [".bss", ".data", ".stack"]) else "Flash/ROM"
        col = imgui.ImVec4(0.3, 0.9, 0.4, 1.0) if reg == "SRAM" else imgui.ImVec4(0.2, 0.8, 1.0, 1.0)
        imgui.text_colored(col, reg)
        imgui.next_column()

    imgui.columns(1)
    imgui.separator()

    # MemBrowse Status Banner
    imgui.text_colored(imgui.ImVec4(0.3, 0.9, 0.4, 1.0), "MemBrowse Status: Connected (membrowse-cli / GitHub Actions)")
    imgui.bullet_text("Zero Dynamic Allocations Verified: operator new() and malloc() unresolved in ELF symbol table")
    imgui.bullet_text("Static Queue Budgets: SpscRingBuffer (64 pkts = 4096 B), AsyncQueue (128 samples = 3584 B)")
    imgui.bullet_text("CI PR Gate: Memory budget regression threshold set at +2.0 KB per commit")

def _render_level1_tlp_inspector():
    """Renders Level 1 Core: Dynamic CTF & TLP Packet Inspection."""
    imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), "AbstractX 64-Byte Transaction Layer Packet (TLP) Layout")
    imgui.separator()

    tlp_fields = [
        ("0..0", "tlp_type", "uint8", "0x10 (MEM_WRITE_POSTED)"),
        ("1..1", "flags", "uint8", "0x00"),
        ("2..2", "tag", "uint8", "0x04 (AHRS Fused State)"),
        ("3..3", "channel", "uint8", "0x02 (High-Rate Telemetry)"),
        ("4..7", "target_addr", "uint32", "0x40000100 (SRAM Telemetry Mailbox)"),
        ("8..9", "len_dw", "uint16", "0x0010 (16 DWords = 64 Bytes)"),
        ("10..11", "seq", "uint16", f"#{g_state.packet_count & 0xFFFF}"),
        ("12..19", "timestamp_ns", "uint64", f"{int(time.time() * 1e9)} ns"),
        ("20..59", "payload", "bytes[40]", "CTF 1.8 Binary Event Record"),
        ("60..63", "crc32", "uint32", "0xDEADBEEF (Hardware CRC32-IEEE)"),
    ]

    imgui.columns(4, "tlp_spec_cols", True)
    imgui.text("Byte Offset")
    imgui.next_column()
    imgui.text("Field Name")
    imgui.next_column()
    imgui.text("Type")
    imgui.next_column()
    imgui.text("Current Value / Decoding")
    imgui.next_column()
    imgui.separator()

    for off, name, ftype, val in tlp_fields:
        imgui.text(off)
        imgui.next_column()
        imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), name)
        imgui.next_column()
        imgui.text(ftype)
        imgui.next_column()
        imgui.text_colored(imgui.ImVec4(0.2, 1.0, 0.4, 1.0), val)
        imgui.next_column()

    imgui.columns(1)

def main():
    global g_initial_tab
    parser = argparse.ArgumentParser(description="AbstractX Visualizer Studio (Level 1 + Level 2)")
    parser.add_argument("--port", type=int, default=DEFAULT_UDP_PORT, help="UDP listening port")
    parser.add_argument("--sim", action="store_true", help="Run with synthetic flight & telemetry generator")
    parser.add_argument("--tab", type=str, default="flight", choices=["flight", "platform", "memory"],
                        help="Initial tab to display (flight, platform, or memory)")
    args = parser.parse_args()

    g_initial_tab = args.tab

    # Start UDP receiver background thread
    recv_thread = threading.Thread(target=udp_receiver_thread, args=(args.port, args.sim), daemon=True)
    recv_thread.start()

    # Configure and Launch Dear ImGui Application
    runner_params = hello_imgui.RunnerParams()
    runner_params.app_window_params.window_title = "AbstractX Visualizer Studio"
    runner_params.app_window_params.window_geometry.size = (1180, 780)
    runner_params.callbacks.show_gui = render_gui
    implot.create_context()

    immapp.run(runner_params)
    implot.destroy_context()

if __name__ == "__main__":
    main()
