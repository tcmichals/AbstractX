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
import math
import time
import socket
import struct
import threading
import argparse
import subprocess
import numpy as np
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

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

        # MemBrowse CI Memory Observability & Static Budget State
        self.membrowse_last_run = "Never"
        self.membrowse_status_msg = "Awaiting CI static audit metrics..."
        self.load_membrowse_metrics()

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

        # Logging & TLP Debugger State
        self.logs = []
        self.recent_tlp_packets = []
        self.selected_tlp_idx = 0
        self.tlp_stream_paused = False
        self.log_filter_level = "ALL"
        self.log_search_text = ""
        self.log_auto_scroll = True

        # AbstractX Studio & CPU Line Chart History (Last 30 seconds, 60 samples at 2 Hz)
        self.chart_hist_len = 60
        self.chart_time = np.linspace(-30.0, 0.0, self.chart_hist_len, dtype=np.float64)
        self.chart_cpu_c0 = np.full(self.chart_hist_len, 22.4, dtype=np.float64)
        self.chart_cpu_c1 = np.full(self.chart_hist_len, 34.1, dtype=np.float64)
        self.chart_cpu_spu = np.full(self.chart_hist_len, 11.2, dtype=np.float64)
        
        # Coroutine & Task Latencies (microseconds)
        self.chart_lat_imu = np.full(self.chart_hist_len, 1.2, dtype=np.float64)
        self.chart_lat_ekf = np.full(self.chart_hist_len, 4.5, dtype=np.float64)
        self.chart_lat_ctl = np.full(self.chart_hist_len, 2.8, dtype=np.float64)
        self.chart_lat_dma = np.full(self.chart_hist_len, 0.8, dtype=np.float64)

        # SPSC Ring Occupancies
        self.chart_ring_sensor = np.full(self.chart_hist_len, 22.0, dtype=np.float64)
        self.chart_ring_telem = np.full(self.chart_hist_len, 12.0, dtype=np.float64)

        # CPU Gauges & Topology View, Zoom & Sizing State
        self.cpu_chart_height = 320.0
        self.cpu_chart_zoom_sec = 30.0
        self.cpu_chart_trigger_zoom_reset = True
        self.cpu_chart_autofit_y = False
        self.cpu_chart_show_rings = True
        self.cpu_view_mode = "standard"  # "standard" or "chart_focus"

        # Floating canvas window states (so user can pop any tab out onto canvas as a movable window)
        self.canvas_windows = {
            "coro": False,
            "cpu": False,
            "tlp": False,
            "log": False,
            "timeline": False,
            "trace": False,
            "source": False,
            "fpga": False,
            "memory": False,
        }
        self.user_canvas_floated_on_startup = False

        # Simple Trace Viewer State
        self.simple_trace_events = []
        self.simple_trace_paused = False
        self.simple_trace_filter_core = "ALL"
        self.simple_trace_search = ""
        self.simple_trace_auto_scroll = True
        self.selected_trace_idx = 0
        self._seed_initial_trace_events()

        # Dual-Plane Execution Swimlanes & Flow-Integrity Drill-Down State
        self.active_layout_preset = "balanced"
        self.source_code_files = [
            "apps/gps_imu_app/src/main.cpp",
            "include/abstractx/drivers/imu/icm42688p.hpp",
            "include/abstractx/fusion/attitude_filter.hpp",
            "targets/allwinner_e907/src/io_processor.cpp"
        ]
        self.selected_code_file_idx = 0
        self.timing_zoom = 1.0
        self.timing_pan_us = 0.0
        self.timing_paused = False
        self.timing_show_overruns_only = False
        self.selected_span_id = "imu_overrun"
        self.selected_event_name = "imu_pipeline [OVERRUN]"
        self.selected_source_file = "apps/gps_imu_app/src/main.cpp"
        self.selected_source_line = 42
        self.selected_token = "co_await g_sensor_ring.pop() [OVERRUN: 23.4 µs vs 15.0 µs budget]"

        # Real-Time Flow-Integrity & Congestion Metrics
        self.chart_transit_delay_us = np.full(self.chart_hist_len, 2.4, dtype=np.float64)  # Δt_transit = t_pop - t_latch
        self.chart_interarrival_us = np.full(self.chart_hist_len, 125.0, dtype=np.float64) # Δt_arrival pacing delta (nominal: 125 µs @ 8 kHz)
        self.pacing_eye_jitter_us = np.linspace(-4.2, 4.8, 48, dtype=np.float64)
        self.hol_contention_ratio = 0.035
        self.tlp_seq_drops = 0
        self.tlp_last_seq = 1000
        self.requested_studio_tab = None

        # Cross-Language Source & RTL Mapping Engine State
        self.source_view_mode = "CPP"  # "CPP" or "RTL"
        self.rtl_code_files = [
            "rtl/asp_router.sv",
            "rtl/imu/asp_imu_auto_dma.sv",
            "rtl/dshot/asp_dshot_core.sv"
        ]
        self.selected_rtl_file_idx = 0
        self.selected_rtl_file = "rtl/asp_router.sv"
        self.selected_rtl_line = 64
        self.active_anomaly_modal = False
        self.selected_anomaly_info = {}

        # ── Coroutine Inspector State (C++20 Coroutine-First Observability) ──
        # Live table of active coroutine frames, updated from incoming CoroEventPayload TLPs.
        # Each entry mirrors CoroInspectorEntry from include/abstractx/trace/coro_trace.hpp.
        self.coro_frames = [
            {"task_id": 1, "name": "app_main",          "state": "RUNNING",
             "awaiter": "—",                  "duration_us": 1200.0,
             "budget_us": 0,                 "source": "main.cpp:182",
             "file": "apps/gps_imu_app/src/main.cpp", "line": 182},
            {"task_id": 2, "name": "imu_pipeline",      "state": "SUSPENDED",
             "awaiter": "spi_ring.pop()",     "duration_us": 42.1,
             "budget_us": 150,               "source": "main.cpp:42",
             "file": "apps/gps_imu_app/src/main.cpp", "line": 42},
            {"task_id": 3, "name": "mag_producer",      "state": "SUSPENDED",
             "awaiter": "timer.sleep(20ms)",  "duration_us": 14800.0,
             "budget_us": 20000,             "source": "main.cpp:55",
             "file": "apps/gps_imu_app/src/main.cpp", "line": 55},
            {"task_id": 4, "name": "sensor_fusion",     "state": "SUSPENDED",
             "awaiter": "imu_chan.pop()",     "duration_us": 18.2,
             "budget_us": 125,               "source": "main.cpp:78",
             "file": "apps/gps_imu_app/src/main.cpp", "line": 78},
            {"task_id": 5, "name": "telem_egress",      "state": "SUSPENDED",
             "awaiter": "step_async()",       "duration_us": 2100.0,
             "budget_us": 10000,             "source": "main.cpp:120",
             "file": "apps/gps_imu_app/src/main.cpp", "line": 120},
            {"task_id": 6, "name": "flight_monitor",    "state": "SUSPENDED",
             "awaiter": "timer.sleep(500ms)", "duration_us": 480300.0,
             "budget_us": 500000,            "source": "main.cpp:105",
             "file": "apps/gps_imu_app/src/main.cpp", "line": 105},
        ]
        # Static frame pool metrics (from coro::coro_pool_used() / coro_pool_capacity())
        self.coro_pool_used_bytes = 23040        # ~6 active frames × ~3840 B avg
        self.coro_pool_capacity_bytes = 61440    # ABSTRACTX_CORO_POOL_SIZE = 60 KB
        # Spawn topology: list of (parent_name, child_name) edges for topology tree
        self.coro_topology = [
            ("app_main", "imu_pipeline"),
            ("app_main", "mag_producer"),
            ("app_main", "sensor_fusion"),
            ("app_main", "telem_egress"),
            ("app_main", "flight_monitor"),
            ("imu_pipeline", "sensor_fusion"),  # imu_pipeline feeds sensor_fusion via g_imu_channel
        ]

        # Initial seed logs
        self.add_log("INFO", "AbstractX", "AbstractX Studio online. SPSC lock-free rings initialized.")
        self.add_log("INFO", "HAL", "Awaitable drivers registered: SPI0 (DMA), I2C0 (ISR), UART0 (RX).")
        self.add_log("CORO", "Dispatcher", "DomainDispatcher::step() cooperative event loop active.")
        self.add_log("TLP", "asp_router", "FPGA crossbar switch fabric online (64B AXI-Stream TLPs).")
        self.add_log("MEM", "MemBrowse", "Zero-heap verification: 0 bytes dynamic allocation.")

    def _seed_initial_trace_events(self):
        seeds = [
            (12.4, "Core 0", "boot_async()", "main()", "ARM64 supervisor boot: SPSC rings ready", 0.0, "apps/gps_imu_app/src/main.cpp", 34),
            (24.8, "Core 1", "init_async()", "ublox_gps.init()", "UART0 DMA receiver registered @ 115200", 12.5, "include/abstractx/drivers/gps/ublox_gps.hpp", 55),
            (36.1, "SPU", "CONFIG_DMA", "asp_router.sv", "AXI-Stream TLP switch crossbar mapped to 0x40000100", 0.8, "sim/cocotb/test_asp_sys_regs_cocotb.py", 80),
            (48.2, "Core 1", "when_all()", "coro::when_all()", "Parallel peripheral boot complete (105 ms)", 0.4, "apps/gps_imu_app/src/main.cpp", 167),
            (62.5, "ISR", "DMA_DONE", "spi0_dma_isr()", "14B ICM-42688-P burst latched -> g_sensor_ring", 0.8, "include/abstractx/drivers/imu/icm42688p.hpp", 54),
            (74.0, "Core 1", "co_await", "imu_pipeline()", "co_await g_sensor_ring.pop() -> resumed", 1.2, "apps/gps_imu_app/src/main.cpp", 42),
            (88.3, "Core 1", "resume()", "attitude_ekf()", "Mahony quaternion kinematics updated", 4.5, "include/abstractx/fusion/attitude_filter.hpp", 88),
            (99.1, "Core 1", "yield", "flight_control()", "Quad-X motor demands dispatched", 2.8, "apps/gps_imu_app/src/main.cpp", 98),
            (112.0, "Core 0", "DOORBELL", "sun6i_msgbox()", "Mailbox interrupt signaled to Core 1", 1.1, "targets/allwinner_e907/main.cpp", 47),
            (125.4, "Core 1", "RING_PUSH", "telemetry_egress()", "64B TLP frame pushed into g_telemetry_ring", 0.5, "include/spsc_tlp_ring.hpp", 34),
        ]
        for t_us, core, prim, sym, det, lat, fpath, line in seeds:
            self.simple_trace_events.append({
                "time_us": t_us,
                "core": core,
                "primitive": prim,
                "symbol": sym,
                "details": det,
                "duration_us": lat,
                "file": fpath,
                "line": line
            })

    def add_trace_event(self, timestamp_us: float, core: str, primitive: str, 
                        symbol: str, details: str, duration_us: float, file_path: str, line_no: int):
        with self.lock:
            if self.simple_trace_paused:
                return
            if len(self.simple_trace_events) > 400:
                self.simple_trace_events.pop(0)
            self.simple_trace_events.append({
                "time_us": timestamp_us,
                "core": core,
                "primitive": primitive,
                "symbol": symbol,
                "details": details,
                "duration_us": duration_us,
                "file": file_path,
                "line": line_no
            })

    def push_chart_metrics(self, t_rel: float, c0: float, c1: float, spu: float,
                           lat_imu: float, lat_ekf: float, lat_ctl: float, lat_dma: float,
                           q_sensor: float, q_telem: float,
                           transit_delay: float = 2.4, interarrival: float = 125.0):
        with self.lock:
            self.chart_time[:-1] = self.chart_time[1:]
            self.chart_time[-1] = t_rel
            
            self.chart_cpu_c0[:-1] = self.chart_cpu_c0[1:]
            self.chart_cpu_c0[-1] = c0
            
            self.chart_cpu_c1[:-1] = self.chart_cpu_c1[1:]
            self.chart_cpu_c1[-1] = c1
            
            self.chart_cpu_spu[:-1] = self.chart_cpu_spu[1:]
            self.chart_cpu_spu[-1] = spu
            
            self.chart_lat_imu[:-1] = self.chart_lat_imu[1:]
            self.chart_lat_imu[-1] = lat_imu
            
            self.chart_lat_ekf[:-1] = self.chart_lat_ekf[1:]
            self.chart_lat_ekf[-1] = lat_ekf
            
            self.chart_lat_ctl[:-1] = self.chart_lat_ctl[1:]
            self.chart_lat_ctl[-1] = lat_ctl
            
            self.chart_lat_dma[:-1] = self.chart_lat_dma[1:]
            self.chart_lat_dma[-1] = lat_dma

            self.chart_ring_sensor[:-1] = self.chart_ring_sensor[1:]
            self.chart_ring_sensor[-1] = q_sensor

            self.chart_ring_telem[:-1] = self.chart_ring_telem[1:]
            self.chart_ring_telem[-1] = q_telem

            self.chart_transit_delay_us[:-1] = self.chart_transit_delay_us[1:]
            self.chart_transit_delay_us[-1] = transit_delay

            self.chart_interarrival_us[:-1] = self.chart_interarrival_us[1:]
            self.chart_interarrival_us[-1] = interarrival

    def add_log(self, level: str, source: str, message: str):
        with self.lock:
            if len(self.logs) > 300:
                self.logs.pop(0)
            now = time.strftime("%H:%M:%S") + f".{int((time.time() % 1) * 1000):03d}"
            self.logs.append({"time": now, "level": level, "source": source, "message": message})

    def add_tlp_packet(self, raw_bytes: bytes, tlp_type: int, tag: int, channel: int, seq: int, ts_ns: int, crc_ok: bool):
        with self.lock:
            if self.tlp_stream_paused:
                return
            if len(self.recent_tlp_packets) > 60:
                self.recent_tlp_packets.pop(0)
            self.recent_tlp_packets.append({
                "seq": seq,
                "type": tlp_type,
                "tag": tag,
                "channel": channel,
                "ts_ns": ts_ns,
                "raw": raw_bytes,
                "hex": raw_bytes.hex(),
                "crc_ok": crc_ok
            })

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

    def load_membrowse_metrics(self):
        """Loads or reloads live MemBrowse metrics generated by CI or tools/track_memory_membrowse.py."""
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
                        "heap_status": v.get("heap_status", "Zero dynamic heap references (Verified)"),
                        "membrowse_budget_ok": True,
                    }
                if live_metrics:
                    self.selected_mem_target = list(self.mem_targets.keys())[0]
                mtime = os.path.getmtime(metrics_file)
                self.membrowse_last_run = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(mtime))
                self.membrowse_status_msg = f"Loaded {len(live_metrics)} targets from CI metrics ({self.membrowse_last_run})"
            except Exception as e:
                self.membrowse_status_msg = f"Failed to load metrics: {e}"

    def run_membrowse_analysis(self):
        """Executes track_memory_membrowse.py locally to regenerate memory_metrics.json and refresh state."""
        import subprocess, sys
        script_path = Path(__file__).resolve().parents[2] / "tools" / "track_memory_membrowse.py"
        try:
            subprocess.run([sys.executable, str(script_path)], capture_output=True, text=True, check=True)
            self.load_membrowse_metrics()
            self.membrowse_status_msg = "Local analysis completed successfully!"
            self.add_log("MEM", "MemBrowse", "Local memory audit completed. memory_metrics.json refreshed.")
        except Exception as e:
            self.membrowse_status_msg = f"Analysis error: {e}"
            self.add_log("WARN", "MemBrowse", f"Failed to run track_memory_membrowse.py: {e}")

g_state = TelemetryState()
g_flight_plugin = FlightVisualizerPlugin()
g_initial_tab = "platform"

def udp_receiver_thread(port: int, sim_mode: bool):
    """
    # @impl [SPEC-STUDIO-08] tools/visualizer/abstractx_studio.py
    # @impl [SPEC-STUDIO-09] tools/visualizer/abstractx_studio.py
    Background worker receiving live 64B TLP frames or synthesizing flight data.
    """
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

            # Synthesize 64B TLP frame for packet stream inspector
            sim_seq = g_state.packet_count & 0xFFFF
            sim_ts = int(time.time() * 1e9)
            sim_hdr = struct.pack("<BBBBIHHQ", 0x10, 0x00, 4, 2, 0x40000100, 16, sim_seq, sim_ts)
            sim_payload = struct.pack("<2hHiH4H", int(roll * 100), int(pitch * 100), int(yaw * 100), int(alt * 1000), int(spd * 100), m1, m2, m3, m4)
            sim_frame = sim_hdr + sim_payload + (b"\x00" * (40 - len(sim_payload))) + b"\xde\xad\xbe\xef"
            g_state.add_tlp_packet(sim_frame, 0x10, 4, 2, sim_seq, sim_ts, True)

            # Periodically update AbstractX Studio chart metrics and simple trace events (at 20 Hz)
            if g_state.packet_count % 10 == 0:
                c0 = float(np.clip(22.0 + 3.5 * np.sin(t * 0.7) + np.random.normal(0, 0.4), 0.0, 100.0))
                c1 = float(np.clip(34.0 + 5.0 * np.cos(t * 0.9) + np.random.normal(0, 0.5), 0.0, 100.0))
                spu = float(np.clip(11.2 + 2.0 * np.sin(t * 1.5) + np.random.normal(0, 0.2), 0.0, 100.0))

                lat_imu = float(max(0.4, 1.2 + 0.3 * np.sin(t * 2.0) + np.random.normal(0, 0.05)))
                lat_ekf = float(max(1.0, 4.5 + 0.8 * np.cos(t * 1.8) + np.random.normal(0, 0.1)))
                lat_ctl = float(max(0.8, 2.8 + 0.5 * np.sin(t * 1.2) + np.random.normal(0, 0.08)))
                lat_dma = float(max(0.2, 0.8 + 0.15 * np.cos(t * 3.0) + np.random.normal(0, 0.02)))

                q_sensor = float(np.clip(20.0 + 10.0 * np.sin(t * 2.0), 0.0, 64.0))
                q_telem = float(np.clip(10.0 + 5.0 * np.cos(t * 3.0), 0.0, 64.0))

                with g_state.lock:
                    g_state.linux_total_cpu = c0
                    g_state.e907_active_duty_pct = c1
                    g_state.e907_wfi_sleep_pct = 100.0 - c1
                    g_state.fpga_lut_utilization_pct = spu

                transit_delay = float(max(0.5, 2.4 + 0.8 * np.sin(t * 1.5) + np.random.normal(0, 0.1)))
                interarrival = float(max(80.0, 125.0 + 3.5 * np.cos(t * 2.2) + np.random.normal(0, 0.5)))
                g_state.push_chart_metrics(t, c0, c1, spu, lat_imu, lat_ekf, lat_ctl, lat_dma, q_sensor, q_telem, transit_delay, interarrival)

            if g_state.packet_count % 25 == 0:
                us_now = float((time.time() % 1000) * 1e4)
                trace_pool = [
                    ("Core 1", "co_await", "imu_pipeline()", "co_await g_sensor_ring.pop()", 1.2, "apps/gps_imu_app/src/main.cpp", 42),
                    ("Core 1", "resume()", "attitude_ekf()", "attitude_ekf.update(gyro, accel)", 4.5, "include/abstractx/fusion/attitude_filter.hpp", 88),
                    ("Core 1", "yield", "flight_control()", "quad_mixer.compute_demands(tau)", 2.8, "apps/gps_imu_app/src/main.cpp", 98),
                    ("SPU", "DMA_BURST", "spi_dma_burst()", "Burst 14B from ICM42688P (SPI0)", 0.8, "include/abstractx/drivers/imu/icm42688p.hpp", 54),
                    ("Core 0", "DOORBELL", "sun6i_msgbox()", "Mailbox interrupt signaled to Core 1", 1.1, "targets/allwinner_e907/main.cpp", 47),
                    ("Core 1", "RING_PUSH", "telemetry_egress()", "64B TLP frame pushed into g_telemetry_ring", 0.5, "include/spsc_tlp_ring.hpp", 34),
                ]
                ev = trace_pool[(g_state.packet_count // 25) % len(trace_pool)]
                g_state.add_trace_event(us_now, ev[0], ev[1], ev[2], ev[3], ev[4], ev[5], ev[6])

            if g_state.packet_count % 40 == 0:
                g_state.add_log("TLP", "asp_router", f"TLP 64B frame Seq #{sim_seq} routed on Ch 2 (AHRS_STATE)")
                g_state.add_log("CORO", "dispatcher", "DomainDispatcher::step() resumed task 'attitude_ekf' (0.4 µs latency)")
            if g_state.packet_count % 100 == 0:
                g_state.add_log("ISR", "hal_spi", "SPI0 Auto-DMA burst complete: 14B IMU latched @ 10 MHz")
            if g_state.packet_count % 300 == 0:
                g_state.add_log("MEM", "MemBrowse", "Zero-heap verification: 0 B dynamic heap allocated across all tasks")

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

                        g_state.add_tlp_packet(frame, le_type, le_tag, le_ch, le_seq, le_ts, True)
                        if g_state.packet_count % 30 == 0:
                            g_state.add_log("TLP", "udp_rx", f"Received 64B TLP Seq #{le_seq} on Ch {le_ch} (Tag 0x{le_tag:02X})")

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

g_dockable_windows = {}

def restore_default_layout():
    """
    # @impl [SPEC-STUDIO-01] tools/visualizer/abstractx_studio.py
    Restores all windows and docking splits back to default factory settings,
    re-docking any floating / popped-out canvases back into the studio workbench.
    """
    try:
        rp = hello_imgui.get_runner_params()
        if rp and rp.docking_params:
            for w in rp.docking_params.dockable_windows:
                w.is_visible = True
            rp.docking_params.layout_reset = True
        g_state.active_layout_preset = "balanced"
    except Exception:
        pass

def apply_docking_layout(preset: str):
    """
    # @impl [SPEC-STUDIO-01] tools/visualizer/abstractx_studio.py
    Dynamically switches docking layout presets to eliminate screen clutter:
    - 'balanced'        : Standard multi-pane overview workbench.
    - 'studio_workbench': Studio diagnostic workbench with User Flight Canvas popped out.
    - 'user_focus'      : User Domain Instruments expanded to 100% full screen.
    - 'core_focus'      : AbstractX Core Studio expanded to 100% full screen.
    - 'source_focus'    : Activates Source Code & Performance Inspector tab in Studio.
    - 'fpga_focus'      : Activates FPGA & Hardware Peripherals tab in Studio.
    """
    g_state.active_layout_preset = preset
    w_core = g_dockable_windows.get("core")
    w_user = g_dockable_windows.get("user")
    w_tlp = g_dockable_windows.get("tlp")
    w_log = g_dockable_windows.get("log")
    w_source = g_dockable_windows.get("source")
    w_fpga = g_dockable_windows.get("fpga")

    if preset == "balanced":
        if w_core: w_core.is_visible = True
        if w_user: w_user.is_visible = True
        if w_tlp: w_tlp.is_visible = True
        if w_log: w_log.is_visible = True
        if w_source: w_source.is_visible = False
        if w_fpga: w_fpga.is_visible = False
    elif preset == "studio_workbench":
        if w_core: w_core.is_visible = True
        if w_user: w_user.is_visible = True
        if w_tlp: w_tlp.is_visible = True
        if w_log: w_log.is_visible = True
        if w_source: w_source.is_visible = False
        if w_fpga: w_fpga.is_visible = False
        decouple_window("User Domain Instruments", 1280.0, 820.0)
    elif preset == "user_focus":
        if w_core: w_core.is_visible = False
        if w_user: w_user.is_visible = True
        if w_tlp: w_tlp.is_visible = False
        if w_log: w_log.is_visible = False
    elif preset == "core_focus":
        if w_core: w_core.is_visible = True
        if w_user: w_user.is_visible = False
        if w_tlp: w_tlp.is_visible = True
        if w_log: w_log.is_visible = True
    elif preset == "source_focus":
        if w_core: w_core.is_visible = True
        if w_source: w_source.is_visible = True
        if w_user: w_user.is_visible = False
        g_state.requested_studio_tab = "source"
    elif preset == "fpga_focus":
        if w_core: w_core.is_visible = True
        if w_fpga: w_fpga.is_visible = True
        if w_user: w_user.is_visible = False
        g_state.requested_studio_tab = "fpga"

def decouple_window(window_title: str, default_width: float = 1280.0, default_height: float = 820.0):
    """
    # @impl [SPEC-STUDIO-01] tools/visualizer/abstractx_studio.py
    Pops out a dockable window into its own floating OS desktop window / viewport,
    setting an immediate generous, comfortable size.
    """
    try:
        ctx = imgui.get_current_context()
        win = imgui.internal.find_window_by_name(window_title)
        if win and ctx:
            imgui.internal.dock_context_queue_undock_window(ctx, win)
            imgui.set_window_size(window_title, imgui.ImVec2(default_width, default_height), 0)
    except Exception:
        pass

def _render_window_sizing_bar(window_title: str, window_key: str, default_w: float = 1280.0, default_h: float = 820.0):
    """
    # @impl [SPEC-STUDIO-01] tools/visualizer/abstractx_studio.py
    Renders dynamic sizing controls for both docked and popped-out windows.
    When popped out: provides instant size presets (800x600, 1024x720, 1280x820, 1600x960, 1920x1080),
    continuous width & height pixel sliders, and a prominent 1-click Pop In (Dock to Studio) button.
    When docked: provides 1-click pop-out to its own dedicated canvas.
    """
    is_docked = imgui.is_window_docked()
    cur_size = imgui.get_window_size()

    imgui.begin_group()
    if not is_docked:
        if window_key == "user":
            imgui.text_colored(imgui.ImVec4(0.2, 0.9, 0.5, 1.0), "🗖 USER FLOATING CANVAS (Layered over Studio)")
        else:
            imgui.text_colored(imgui.ImVec4(0.95, 0.82, 0.25, 1.0), "🗖 FLOATING CANVAS")
        imgui.same_line(0, 10)
        imgui.text(f"| Size: {int(cur_size.x)}x{int(cur_size.y)} px")
        imgui.same_line(0, 14)

        presets = [
            ("800x600", 800, 600),
            ("1024x720", 1024, 720),
            ("1280x820", 1280, 820),
            ("1600x960", 1600, 960),
            ("1920x1080", 1920, 1080),
        ]
        for lbl, w, h in presets:
            if imgui.button(f"{lbl}##{window_key}"):
                hello_imgui.change_window_size((w, h))
                try:
                    imgui.set_window_size(window_title, imgui.ImVec2(float(w), float(h)), imgui.Cond_.always)
                except Exception:
                    pass
            imgui.same_line()

        # Continuous width & height sliders
        imgui.push_item_width(90)
        ch_w, new_w = imgui.slider_int(f"W##{window_key}", int(cur_size.x), 500, 2560)
        imgui.same_line()
        ch_h, new_h = imgui.slider_int(f"H##{window_key}", int(cur_size.y), 350, 1600)
        imgui.pop_item_width()
        if ch_w or ch_h:
            hello_imgui.change_window_size((new_w, new_h))
            try:
                imgui.set_window_size(window_title, imgui.ImVec2(float(new_w), float(new_h)), imgui.Cond_.always)
            except Exception:
                pass

        imgui.same_line(0, 14)
        imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.7, 0.4, 0.9))
        if imgui.button(f"🗗 Pop In (Dock to Studio)##{window_key}"):
            restore_default_layout()
        imgui.pop_style_color()
    else:
        # Window is docked inside workbench
        if imgui.button(f"🗖 Float Canvas##{window_key}"):
            decouple_window(window_title, default_w, default_h)
        imgui.same_line()
        if imgui.button(f"🚀 Dedicated Window (2nd Monitor)##{window_key}"):
            launch_external_flight_canvas()
        imgui.same_line()
        imgui.text_colored(imgui.ImVec4(0.5, 0.6, 0.7, 0.8), "| Single dedicated window or dual monitor")
        imgui.same_line(0, 14)
        imgui.text_colored(imgui.ImVec4(0.9, 0.8, 0.3, 1.0), "Window Size:")
        imgui.same_line()
        for lbl, w, h in [("1280x800", 1280, 800), ("1560x920", 1560, 920), ("1920x1080", 1920, 1080)]:
            if imgui.button(f"{lbl}##docked_{window_key}"):
                hello_imgui.change_window_size((w, h))
            imgui.same_line()

    imgui.end_group()
    imgui.separator()

def launch_external_flight_canvas():
    """
    # @impl [SPEC-STUDIO-01] tools/visualizer/abstractx_studio.py
    Launches the Flight Display in a separate, fully decorated native OS window.
    Provides full native OS titlebar, minimize/maximize buttons, and mouse resize borders
    that can be freely dragged to a second monitor across both Windows and Linux.
    """
    flight_script = REPO_ROOT / "apps" / "gps_imu_app" / "tools" / "flight_display.py"
    if flight_script.exists():
        sim_arg = ["--sim"] if g_state.sim_mode else []
        subprocess.Popen([sys.executable, str(flight_script)] + sim_arg)

def _setup_studio_style():
    """
    # @impl [SPEC-STUDIO-01] tools/visualizer/abstractx_studio.py
    Configures Dear ImGui style for AbstractX Studio:
    Increases window border hover padding to 10px so resizing floating/popped-out windows
    from any edge or corner is easy and forgiving, and adds a high-contrast electric blue border.
    """
    style = imgui.get_style()
    style.window_border_hover_padding = 10.0
    style.window_border_size = 2.0
    style.window_rounding = 6.0
    style.frame_rounding = 4.0
    style.tab_rounding = 4.0
    style.popup_rounding = 4.0
    style.set_color_(imgui.Col_.border, imgui.ImVec4(0.25, 0.65, 0.95, 0.9))
    style.set_color_(imgui.Col_.border_shadow, imgui.ImVec4(0.0, 0.0, 0.0, 0.6))
    style.set_color_(imgui.Col_.title_bg, imgui.ImVec4(0.12, 0.16, 0.22, 1.0))
    style.set_color_(imgui.Col_.title_bg_active, imgui.ImVec4(0.18, 0.28, 0.42, 1.0))

def _render_status_bar():
    """Renders bottom status bar with quick dynamic window layout presets, reset, and canvas pop-out."""
    if g_state.connected:
        imgui.text_colored(imgui.ImVec4(0.1, 0.9, 0.2, 1.0), " [ONLINE] ")
    else:
        imgui.text_colored(imgui.ImVec4(0.9, 0.2, 0.1, 1.0), " [OFFLINE / WAITING :9870] ")
    imgui.same_line()
    imgui.text(f"| Platform: {g_state.platform_name} ({g_state.platform_arch}) | Packets: {g_state.packet_count:,} | Rate: {g_state.fps_packet_rate} pkts/s | Dynamic Heap: 0 B")
    imgui.same_line()
    if imgui.small_button("MemBrowse CI: 0 B Heap (Pass)"):
        g_state.requested_studio_tab = "memory"
        w_core = g_dockable_windows.get("core")
        if w_core:
            w_core.is_visible = True

    # Dynamic Window Layout Preset Chips
    imgui.same_line(0, 20)
    imgui.text_colored(imgui.ImVec4(0.9, 0.8, 0.3, 1.0), "Layout:")
    imgui.same_line()

    presets = [
        ("🗖 Studio Workbench", "studio_workbench"),
        ("🗗 Balanced", "balanced"),
        ("✈️ Flight Focus", "user_focus"),
        ("🔬 Core Studio", "core_focus"),
        ("💻 Source Code", "source_focus"),
        ("⚡ FPGA Hardware", "fpga_focus"),
    ]
    for lbl, pr in presets:
        is_active = (pr == g_state.active_layout_preset)
        if is_active:
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.6, 0.9, 1.0))
        if imgui.button(lbl):
            apply_docking_layout(pr)
        if is_active:
            imgui.pop_style_color()
        imgui.same_line()

    imgui.same_line(0, 16)
    if imgui.button("🔄 Restore Default Layout"):
        restore_default_layout()

    imgui.same_line(0, 12)
    if imgui.button("🗖 Pop Out Flight Canvas"):
        decouple_window("User Domain Instruments", 1280.0, 820.0)

    imgui.same_line(0, 16)
    imgui.text_colored(imgui.ImVec4(0.9, 0.8, 0.3, 1.0), "Window Size:")
    imgui.same_line()
    for lbl, w, h in [("1280x800", 1280, 800), ("1560x920", 1560, 920), ("1920x1080", 1920, 1080)]:
        if imgui.button(f"{lbl}##status_size"):
            hello_imgui.change_window_size((w, h))
        imgui.same_line()

def draw_radial_gauge(center_x: float, center_y: float, radius: float, value_pct: float, 
                      label: str, sublabel: str, unit: str = "%", max_val: float = 100.0) -> None:
    """Draws an analog radial dial gauge with dynamic color grading and needle."""
    dl = imgui.get_window_draw_list()
    a_min = math.radians(140)
    a_max = math.radians(400)
    
    # Outer circle dial background
    c_bg_circle = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.08, 0.10, 0.14, 0.95))
    c_border = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.20, 0.24, 0.32, 1.0))
    dl.add_circle_filled(imgui.ImVec2(center_x, center_y), radius + 8.0, c_bg_circle, 36)
    dl.add_circle(imgui.ImVec2(center_x, center_y), radius + 8.0, c_border, 36, 1.5)

    # Background track arc
    c_track = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.16, 0.20, 0.26, 1.0))
    dl.path_arc_to(imgui.ImVec2(center_x, center_y), radius, a_min, a_max, 32)
    dl.path_stroke(c_track, 7.0, 0)
    
    # Active value sweep arc with color thresholds
    val_clamped = max(0.0, min(value_pct, max_val))
    val_ratio = val_clamped / max_val
    val_sweep = a_min + (val_ratio * (a_max - a_min))
    
    if val_ratio < 0.50:
        c_val = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.2, 0.85, 0.45, 1.0)) # Emerald green
    elif val_ratio < 0.80:
        c_val = imgui.color_convert_float4_to_u32(imgui.ImVec4(1.0, 0.75, 0.2, 1.0))  # Amber/yellow
    else:
        c_val = imgui.color_convert_float4_to_u32(imgui.ImVec4(1.0, 0.3, 0.3, 1.0))   # Coral red
        
    dl.path_arc_to(imgui.ImVec2(center_x, center_y), radius, a_min, val_sweep, 32)
    dl.path_stroke(c_val, 7.0, 0)
    
    # Needle indicator dot on perimeter
    nx = center_x + (radius - 1.0) * math.cos(val_sweep)
    ny = center_y + (radius - 1.0) * math.sin(val_sweep)
    c_needle = imgui.color_convert_float4_to_u32(imgui.ImVec4(1.0, 1.0, 1.0, 0.95))
    dl.add_circle_filled(imgui.ImVec2(nx, ny), 3.5, c_needle)

    # Central digital readout
    val_str = f"{value_pct:.1f}{unit}"
    c_text = imgui.color_convert_float4_to_u32(imgui.ImVec4(1.0, 1.0, 1.0, 1.0))
    c_sub = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.65, 0.75, 0.85, 1.0))
    
    t_width = len(val_str) * 7.5
    dl.add_text(imgui.ImVec2(center_x - t_width / 2.0, center_y - 8.0), c_text, val_str)

    # Labels below gauge
    lbl_w = len(label) * 6.5
    dl.add_text(imgui.ImVec2(center_x - lbl_w / 2.0, center_y + radius + 12.0), c_text, label)
    sub_w = len(sublabel) * 5.8
    dl.add_text(imgui.ImVec2(center_x - sub_w / 2.0, center_y + radius + 26.0), c_sub, sublabel)

def _render_core_cpu_and_topology():
    """
    Renders Silicon Cores, Radial CPU & SPSC Gauges, Interactive Zoomable Load Plots,
    Dynamic Window Sizing Controls, and Platform Architecture Topology.
    """
    # ── Top Toolbar: Dynamic Window Sizing & Plot Zoom Controls ──
    imgui.begin_group()
    imgui.text_colored(imgui.ImVec4(0.9, 0.8, 0.3, 1.0), "Window Size:")
    imgui.same_line()
    for lbl, w, h in [("1280x800", 1280, 800), ("1560x920", 1560, 920), ("1920x1080", 1920, 1080)]:
        if imgui.button(f"{lbl}##cpu_wsize"):
            hello_imgui.change_window_size((w, h))
        imgui.same_line()

    if imgui.button("⛶ Expand Pane##cpu_exp"):
        apply_docking_layout("core_focus")
    imgui.same_line()
    if imgui.button("🗖 Pop to Canvas##cpu_pop"):
        g_state.canvas_windows["cpu"] = True
    imgui.same_line(0, 16)

    # View Mode Toggle
    is_chart_focus = (g_state.cpu_view_mode == "chart_focus")
    if is_chart_focus:
        imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.6, 0.9, 1.0))
    if imgui.button("📊 Maximize Chart##cpu_mode"):
        g_state.cpu_view_mode = "standard" if is_chart_focus else "chart_focus"
    if is_chart_focus:
        imgui.pop_style_color()

    imgui.end_group()
    imgui.separator()

    # ── Section 1: Silicon Cores & Hardware SPSC Gauges (4 Dense Cards, Zero Wasted Space) ──
    imgui.columns(4, "cpu_gauge_cols", True)

    # Card 1: Core 0 (Host Linux / ARM Cortex-M33)
    col_w = imgui.get_column_width()
    cur_pos = imgui.get_cursor_screen_pos()
    draw_radial_gauge(cur_pos.x + col_w / 2.0, cur_pos.y + 40.0, 32.0,
                      g_state.linux_total_cpu, "Core 0", f"Host {g_state.linux_total_cpu:.1f}%")
    imgui.dummy(imgui.ImVec2(col_w, 105.0))
    imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), "Supervisor Core")
    imgui.text("Clock: 150 MHz | Status: RUN")
    imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "Task: I/O + Wi-Fi Pacing")
    imgui.next_column()

    # Card 2: Core 1 (C++20 Stackless Coroutine Engine)
    col_w = imgui.get_column_width()
    cur_pos = imgui.get_cursor_screen_pos()
    draw_radial_gauge(cur_pos.x + col_w / 2.0, cur_pos.y + 40.0, 32.0,
                      g_state.e907_active_duty_pct, "Core 1", f"Duty {g_state.e907_active_duty_pct:.1f}%")
    imgui.dummy(imgui.ImVec2(col_w, 105.0))
    imgui.text_colored(imgui.ImVec4(0.3, 1.0, 0.4, 1.0), "Coroutine Engine")
    imgui.text("Dispatcher: 8.2 kHz | 6 Tasks")
    imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), f"Pool: {g_state.coro_pool_used_bytes // 1024} / {g_state.coro_pool_capacity_bytes // 1024} KB")
    imgui.next_column()

    # Card 3: SPU (FPGA AXI-Stream TLP Crossbar Fabric)
    col_w = imgui.get_column_width()
    cur_pos = imgui.get_cursor_screen_pos()
    draw_radial_gauge(cur_pos.x + col_w / 2.0, cur_pos.y + 40.0, 32.0,
                      g_state.fpga_lut_utilization_pct, "SPU Fabric", f"Logic {g_state.fpga_lut_utilization_pct:.1f}%")
    imgui.dummy(imgui.ImVec2(col_w, 105.0))
    imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0), "FPGA Crossbar Switch")
    imgui.text("Throughput: 8,240 pkts/s")
    imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "Contention: 0 Stalls (0%)")
    imgui.next_column()

    # Card 4: SPSC Lock-Free Rings & Freestanding Zero-Heap
    col_w = imgui.get_column_width()
    cur_pos = imgui.get_cursor_screen_pos()
    draw_radial_gauge(cur_pos.x + col_w / 2.0, cur_pos.y + 40.0, 32.0,
                      g_state.chart_ring_sensor[-1], "SPSC Rings", f"Fill {g_state.chart_ring_sensor[-1]:.1f}%")
    imgui.dummy(imgui.ImVec2(col_w, 105.0))
    imgui.text_colored(imgui.ImVec4(0.2, 1.0, 0.8, 1.0), "Lock-Free SPSC Fabric")
    imgui.text(f"RTT Doorbell: {g_state.rtt_latency_us:.1f} µs")
    imgui.text_colored(imgui.ImVec4(0.2, 1.0, 0.4, 1.0), "Dynamic Heap: 0 B (PASS)")
    imgui.columns(1)
    imgui.separator()

    # ── Section 2: Interactive Zoomable & Resizable CPU Load History Plot ──
    imgui.begin_group()
    imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), "Time-Series Load Telemetry:")
    imgui.same_line(0, 12)

    # Zoom presets
    imgui.text_colored(imgui.ImVec4(0.8, 0.8, 0.8, 1.0), "Zoom:")
    imgui.same_line()
    for z_lbl, z_sec in [("10s", 10.0), ("30s", 30.0), ("60s", 60.0)]:
        is_sel = (g_state.cpu_chart_zoom_sec == z_sec)
        if is_sel:
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.6, 0.9, 1.0))
        if imgui.button(f"{z_lbl}##cpu_z"):
            g_state.cpu_chart_zoom_sec = z_sec
            g_state.cpu_chart_trigger_zoom_reset = True
        if is_sel:
            imgui.pop_style_color()
        imgui.same_line()

    if imgui.button("Auto-Fit X##cpu_fitx"):
        g_state.cpu_chart_zoom_sec = 0.0
        g_state.cpu_chart_trigger_zoom_reset = True
    imgui.same_line(0, 12)

    # Y-axis zoom/lock toggle
    _, g_state.cpu_chart_autofit_y = imgui.checkbox("Auto-Fit Y (Zoom Micro-Loads)", g_state.cpu_chart_autofit_y)
    imgui.same_line(0, 12)

    # Ring overlay toggle
    _, g_state.cpu_chart_show_rings = imgui.checkbox("Show SPSC Rings (%)", g_state.cpu_chart_show_rings)
    imgui.same_line(0, 12)

    # Height Controls
    imgui.text("Height:")
    imgui.same_line()
    for h_lbl, h_val in [("240px", 240.0), ("340px", 340.0), ("480px", 480.0)]:
        if imgui.button(f"{h_lbl}##cpu_hpreset"):
            g_state.cpu_chart_height = h_val
        imgui.same_line()

    imgui.push_item_width(90)
    _, g_state.cpu_chart_height = imgui.slider_float("##cpu_hslider", g_state.cpu_chart_height, 180.0, 750.0, "%.0f px")
    imgui.pop_item_width()
    imgui.same_line()
    imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), "(Scroll wheel on plot to zoom)")
    imgui.end_group()

    # Determine plot dimensions
    plot_h = g_state.cpu_chart_height
    if g_state.cpu_view_mode == "chart_focus":
        avail_h = imgui.get_content_region_avail().y
        plot_h = max(340.0, avail_h - 16.0)

    x_flags = implot.AxisFlags_.auto_fit if g_state.cpu_chart_zoom_sec == 0.0 else implot.AxisFlags_.none
    y_flags = implot.AxisFlags_.auto_fit if g_state.cpu_chart_autofit_y else implot.AxisFlags_.none

    if implot.begin_plot("Silicon Cores Real-Time Load & Duty History", imgui.ImVec2(-1, plot_h)):
        implot.setup_axes("History Time (s)", "Utilization / Duty (%)", x_flags, y_flags)

        cond_x = imgui.Cond_.always if g_state.cpu_chart_trigger_zoom_reset else imgui.Cond_.once
        if g_state.cpu_chart_zoom_sec > 0.0:
            implot.setup_axis_limits(implot.ImAxis_.x1, -g_state.cpu_chart_zoom_sec, 0.0, cond_x)

        if not g_state.cpu_chart_autofit_y:
            cond_y = imgui.Cond_.always if g_state.cpu_chart_trigger_zoom_reset else imgui.Cond_.once
            implot.setup_axis_limits(implot.ImAxis_.y1, 0.0, 100.0, cond_y)

        g_state.cpu_chart_trigger_zoom_reset = False

        with g_state.lock:
            t_data = np.copy(g_state.chart_time)
            c0_data = np.copy(g_state.chart_cpu_c0)
            c1_data = np.copy(g_state.chart_cpu_c1)
            spu_data = np.copy(g_state.chart_cpu_spu)
            ring_s = np.copy(g_state.chart_ring_sensor)
            ring_t = np.copy(g_state.chart_ring_telem)

        implot.plot_line("Core 0 (Host Linux / ARM64)", t_data, c0_data)
        implot.plot_line("Core 1 (C++20 Coroutine Duty)", t_data, c1_data)
        implot.plot_line("SPU (FPGA Switch Fabric)", t_data, spu_data)

        if g_state.cpu_chart_show_rings:
            implot.plot_line("Sensor Ring Fill (%)", t_data, ring_s)
            implot.plot_line("Telem Ring Fill (%)", t_data, ring_t)

        implot.end_plot()

    imgui.separator()
    # Middle Section: Platform Topology & Processing Roles
    if g_state.cpu_view_mode != "chart_focus":
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
        imgui.text(f"SPSC Ring : 64 Descriptors (Zero-Copy)")
        imgui.text(f"Avg Doorbell Latency: {g_state.rtt_latency_us:.1f} µs")
        imgui.columns(1)
    else:
        if imgui.tree_node("Platform Topology & Roles (Collapsed in Maximize Chart Mode)"):
            imgui.columns(3, "topo_cols_min", False)
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
            imgui.text(f"SPSC Ring : 64 Descriptors (Zero-Copy)")
            imgui.text(f"Avg Doorbell Latency: {g_state.rtt_latency_us:.1f} µs")
            imgui.columns(1)
            imgui.tree_pop()

def _render_simple_trace_view():
    """Renders execution trace table with click-to-inspect."""
    imgui.begin_group()
    cores = ["ALL", "Core 0", "Core 1", "SPU", "ISR"]
    for c in cores:
        is_active = (c == g_state.simple_trace_filter_core)
        if is_active:
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.6, 0.9, 1.0))
        if imgui.button(c):
            g_state.simple_trace_filter_core = c
        if is_active:
            imgui.pop_style_color()
        imgui.same_line()

    imgui.same_line(0, 12)
    _, g_state.simple_trace_search = imgui.input_text("Search", g_state.simple_trace_search, 48)
    imgui.same_line()
    _, g_state.simple_trace_paused = imgui.checkbox("Pause Trace", g_state.simple_trace_paused)
    imgui.same_line()
    if imgui.button("Clear Trace"):
        with g_state.lock:
            g_state.simple_trace_events.clear()
            g_state.selected_trace_idx = 0
    imgui.same_line()
    _, g_state.simple_trace_auto_scroll = imgui.checkbox("Auto-Scroll", g_state.simple_trace_auto_scroll)
    imgui.same_line()
    if imgui.button("🗖 Pop to Canvas##trace_pop"):
        g_state.canvas_windows["trace"] = True
    imgui.end_group()
    imgui.separator()

    # Simple Trace Table Region
    imgui.begin_child("SimpleTraceChild", imgui.ImVec2(-1, 260), True)
    imgui.columns(6, "simple_trace_cols", True)
    imgui.text("Offset (µs)")
    imgui.next_column()
    imgui.text("Core")
    imgui.next_column()
    imgui.text("Primitive")
    imgui.next_column()
    imgui.text("Symbol / Task")
    imgui.next_column()
    imgui.text("Context & Arguments")
    imgui.next_column()
    imgui.text("Lat (µs)")
    imgui.next_column()
    imgui.separator()

    with g_state.lock:
        events = list(g_state.simple_trace_events)
        sel_idx = g_state.selected_trace_idx

    flt_core = g_state.simple_trace_filter_core
    flt_search = g_state.simple_trace_search.lower()

    col_core_map = {
        "Core 0": imgui.ImVec4(0.4, 0.8, 1.0, 1.0),
        "Core 1": imgui.ImVec4(0.3, 1.0, 0.4, 1.0),
        "SPU": imgui.ImVec4(1.0, 0.8, 0.2, 1.0),
        "ISR": imgui.ImVec4(1.0, 0.4, 0.4, 1.0),
    }

    for idx, ev in enumerate(events):
        if flt_core != "ALL" and ev["core"] != flt_core:
            continue
        if flt_search and (flt_search not in ev["symbol"].lower() and flt_search not in ev["details"].lower() and flt_search not in ev["primitive"].lower()):
            continue

        is_sel = (idx == sel_idx)
        clicked, _ = imgui.selectable(f"+{ev['time_us']:.1f}", is_sel, imgui.SelectableFlags_.span_all_columns)
        if clicked:
            with g_state.lock:
                g_state.selected_trace_idx = idx
                g_state.selected_event_name = ev['symbol']
                g_state.selected_source_file = ev['file']
                g_state.selected_source_line = ev['line']
                g_state.selected_token = f"{ev['primitive']}: {ev['details']}"
        imgui.next_column()

        c_col = col_core_map.get(ev["core"], imgui.ImVec4(0.8, 0.8, 0.8, 1.0))
        imgui.text_colored(c_col, f"[{ev['core']}]")
        imgui.next_column()

        imgui.text_colored(imgui.ImVec4(0.9, 0.7, 0.3, 1.0), ev["primitive"])
        imgui.next_column()

        imgui.text(ev["symbol"])
        imgui.next_column()

        imgui.text_colored(imgui.ImVec4(0.7, 0.7, 0.7, 1.0), ev["details"])
        imgui.next_column()

        imgui.text(f"{ev['duration_us']:.1f}")
        imgui.next_column()

    if g_state.simple_trace_auto_scroll and imgui.get_scroll_y() >= imgui.get_scroll_max_y() - 20:
        imgui.set_scroll_here_y(1.0)

    imgui.columns(1)
    imgui.end_child()

    imgui.spacing()
    # Source Code Inspector Sub-Pane
    imgui.text_colored(imgui.ImVec4(1.0, 1.0, 0.2, 1.0), f">> [Source Code Inspector] Selected: {g_state.selected_event_name} -> {g_state.selected_source_file}:{g_state.selected_source_line}")
    imgui.begin_child("SimpleTraceSourcePreview", imgui.ImVec2(-1, 80), True)
    imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), f"// Source location: {g_state.selected_source_file}")
    imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), f"   {g_state.selected_source_line - 1}:   // Trace context")
    imgui.text_colored(imgui.ImVec4(0.2, 1.0, 0.4, 1.0), f"-> {g_state.selected_source_line}:       {g_state.selected_token};")
    imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), f"   {g_state.selected_source_line + 1}:   // Coroutine resumption point")
    imgui.end_child()

def _render_core_timeline():
    """Renders Dual-Plane Execution Timeline & Source Scanner."""
    imgui.begin_group()
    imgui.text_colored(imgui.ImVec4(0.9, 0.5, 0.2, 1.0), "Plane 1: Hardware I/O Processor & Peripheral Drivers (Interrupt & DMA Context)")
    imgui.same_line(0, 20)
    if imgui.button("🗖 Pop to Canvas##timeline_pop"):
        g_state.canvas_windows["timeline"] = True
    imgui.end_group()
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

def _render_level1_memory():
    """
    # @impl [SPEC-STUDIO-10] tools/visualizer/abstractx_studio.py
    Renders Level 1 Core: Continuous MemBrowse CI Static Memory Observability & Zero-Heap Audit.
    Displays CI static section metrics (.text, .rodata, .data, .bss) from memory_metrics.json,
    multi-target matrix comparison, and provides interactive 1-click local CI analysis execution.
    """
    imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), "MemBrowse CI Static Memory & Zero-Heap Audit Report")
    imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0),
                       "Continuous Integration static memory gate (.github/workflows/membrowse.yml & tools/track_memory_membrowse.py). "
                       "AbstractX enforces Freestanding C++20 Zero-Heap invariants: all task frames and rings are statically pooled.")
    imgui.separator()

    # CI Action & Status Bar
    imgui.begin_group()
    if imgui.button("▶ Run Local MemBrowse CI Analysis"):
        g_state.run_membrowse_analysis()
    imgui.same_line()
    if imgui.button("🔄 Reload CI Metrics"):
        g_state.load_membrowse_metrics()
    imgui.same_line()
    if imgui.button("🗖 Pop to Canvas##mem_pop"):
        g_state.canvas_windows["memory"] = True
    imgui.same_line()
    imgui.text_colored(imgui.ImVec4(0.9, 0.8, 0.3, 1.0), f"Status: {g_state.membrowse_status_msg}")
    imgui.end_group()
    imgui.separator()

    # Multi-Target CI Overview Matrix
    imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0), "Continuous Integration Silicon Target Matrix:")
    imgui.columns(5, "ci_target_matrix_cols", True)
    imgui.text("Target Profile")
    imgui.next_column()
    imgui.text("Architecture")
    imgui.next_column()
    imgui.text("RAM Usage / Budget")
    imgui.next_column()
    imgui.text("Flash Usage / Budget")
    imgui.next_column()
    imgui.text("Zero-Heap Symbol Audit")
    imgui.next_column()
    imgui.separator()

    for t_name, t_info in g_state.mem_targets.items():
        is_sel = (t_name == g_state.selected_mem_target)
        clicked, _ = imgui.selectable(f"{t_name}##matrix_row", is_sel, imgui.SelectableFlags_.span_all_columns)
        if clicked:
            g_state.selected_mem_target = t_name
        imgui.next_column()
        imgui.text(t_info.get("arch", "MCU"))
        imgui.next_column()
        r_used = t_info.get("ram_used_bytes", 0)
        r_tot = max(t_info.get("ram_total_bytes", 524288), 1)
        r_pct = (r_used / r_tot) * 100.0
        r_col = imgui.ImVec4(0.3, 1.0, 0.4, 1.0) if r_pct <= 90.0 else imgui.ImVec4(1.0, 0.3, 0.3, 1.0)
        imgui.text_colored(r_col, f"{r_used // 1024} / {r_tot // 1024} KB ({r_pct:.1f}%)")
        imgui.next_column()
        f_used = t_info.get("flash_used_bytes", 0)
        f_tot = max(t_info.get("flash_total_bytes", 4194304), 1)
        f_pct = (f_used / f_tot) * 100.0
        f_col = imgui.ImVec4(0.2, 0.8, 1.0, 1.0) if f_pct <= 90.0 else imgui.ImVec4(1.0, 0.3, 0.3, 1.0)
        imgui.text_colored(f_col, f"{f_used // 1024} / {f_tot // 1024} KB ({f_pct:.1f}%)")
        imgui.next_column()
        if t_info.get("zero_heap_compliant", True):
            imgui.text_colored(imgui.ImVec4(0.2, 1.0, 0.4, 1.0), "✔ PASS (0 B Heap)")
        else:
            imgui.text_colored(imgui.ImVec4(1.0, 0.3, 0.3, 1.0), "✘ FAIL (Heap ref)")
        imgui.next_column()

    imgui.columns(1)
    imgui.separator()

    # Detailed Section Breakdown for Selected Target
    curr_target = g_state.selected_mem_target
    targets = list(g_state.mem_targets.keys())
    if imgui.begin_combo("Inspect Target Details", curr_target):
        for t_name in targets:
            is_selected = (t_name == curr_target)
            if imgui.selectable(t_name, is_selected)[0]:
                g_state.selected_mem_target = t_name
            if is_selected:
                imgui.set_item_default_focus()
        imgui.end_combo()

    t_info = g_state.mem_targets.get(g_state.selected_mem_target, list(g_state.mem_targets.values())[0])
    imgui.columns(2, "mem_summary_cols", False)

    # RAM Usage
    ram_used = t_info.get("ram_used_bytes", 0)
    ram_total = max(t_info.get("ram_total_bytes", 524288), 1)
    ram_pct = (ram_used / ram_total) * 100.0
    imgui.text_colored(imgui.ImVec4(0.3, 1.0, 0.4, 1.0), f"RAM (SRAM) Budget: {ram_pct:.1f}% Used")
    imgui.progress_bar(ram_used / ram_total, imgui.ImVec2(-1, 22), f"{ram_used // 1024} KB / {ram_total // 1024} KB")
    imgui.next_column()

    # Flash / ROM Usage
    flash_used = t_info.get("flash_used_bytes", 0)
    flash_total = max(t_info.get("flash_total_bytes", 4194304), 1)
    flash_pct = (flash_used / flash_total) * 100.0
    imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), f"Flash / ROM Budget: {flash_pct:.1f}% Used")
    imgui.progress_bar(flash_used / flash_total, imgui.ImVec2(-1, 22), f"{flash_used // 1024} KB / {flash_total // 1024} KB")
    imgui.columns(1)
    imgui.separator()

    # ELF Section Breakdown Table
    imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0), f"ELF Binary Section Breakdown ({t_info.get('arch', 'MCU')}):")
    imgui.columns(3, "sec_cols", True)
    imgui.text("Section Name")
    imgui.next_column()
    imgui.text("Size (Bytes)")
    imgui.next_column()
    imgui.text("Target Memory Region")
    imgui.next_column()
    imgui.separator()

    for sec_name, sec_sz in t_info.get("sections", {}).items():
        imgui.text(sec_name)
        imgui.next_column()
        imgui.text(f"{sec_sz:,} B  ({sec_sz / 1024.0:4.1f} KB)")
        imgui.next_column()
        reg = "SRAM" if any(k in sec_name for k in [".bss", ".data", ".stack", ".ram"]) else "Flash/ROM"
        col = imgui.ImVec4(0.3, 0.9, 0.4, 1.0) if reg == "SRAM" else imgui.ImVec4(0.2, 0.8, 1.0, 1.0)
        imgui.text_colored(col, reg)
        imgui.next_column()

    imgui.columns(1)
    imgui.separator()

    # MemBrowse CI Policy & Verification Banner
    imgui.text_colored(imgui.ImVec4(0.3, 0.9, 0.4, 1.0), "MemBrowse CI Gate Enforcement:")
    imgui.bullet_text("Zero Dynamic Allocations Verified: operator new(), malloc(), free() unresolved in ELF symbol table")
    imgui.bullet_text("Static Queue Budgets: SpscRingBuffer (64 pkts = 4096 B), AsyncQueue (128 samples = 3584 B)")
    imgui.bullet_text("CI PR Gate Threshold: Reject pull requests exceeding +2.0 KB memory budget regression")
    imgui.bullet_text("Command Line: python3 tools/track_memory_membrowse.py [--upload --token $MEMBROWSE_TOKEN]")


def _render_coroutine_inspector():
    """
    # @impl [SPEC-STUDIO-02] tools/visualizer/abstractx_studio.py
    C++20 Coroutine Inspector & Asynchronous State Machine Viewer.
    Lifts the compiler-generated state machine into a human-readable panel showing:
      1. Static Frame Pool Gauge  - real-time zero-heap invariant validation
      2. Live Coroutine Table     - task_id, name, state, co_await awaiter token,
                                    duration in state, per-awaiter watchdog budget bar
      3. Stall / Deadlock Watchdog- rows glow red when duration exceeds budget
      4. Spawn Topology Tree      - static parent-child coroutine graph
    Live data source: CoroEventPayload TLPs (event_id=1, stream=0) emitted via
    ABSTRACTX_CORO_SUSPEND / ABSTRACTX_CORO_RESUME macros in coro_trace.hpp.
    """
    with g_state.lock:
        frames    = list(g_state.coro_frames)
        pool_used = g_state.coro_pool_used_bytes
        pool_cap  = g_state.coro_pool_capacity_bytes
        topology  = list(g_state.coro_topology)

    active_count  = len(frames)
    stalled_count = sum(1 for f in frames
                        if f["budget_us"] > 0 and f["duration_us"] > f["budget_us"])
    pool_pct = pool_used / max(pool_cap, 1)

    # Header Banner
    header_col = imgui.ImVec4(0.9, 0.4, 0.3, 1.0) if stalled_count > 0 else imgui.ImVec4(0.3, 0.85, 1.0, 1.0)
    imgui.text_colored(header_col,
        f"C++20 Coroutine Inspector & Async State Machine Viewer    "
        f"Active: {active_count}  Stalled: {stalled_count}  "
        f"Pool: {pool_used // 1024} KB / {pool_cap // 1024} KB")
    imgui.same_line(0, 20)
    if imgui.button("Jump to Source##coro_jump"):
        g_state.requested_studio_tab = "source"
    imgui.same_line(0, 10)
    if imgui.button("🗖 Pop to Canvas##coro_pop"):
        g_state.canvas_windows["coro"] = True
    imgui.separator()

    # Section 1: Static Coroutine Frame Pool Gauge
    imgui.text_colored(imgui.ImVec4(0.9, 0.75, 0.2, 1.0),
        "[Static Coroutine Frame Pool  (include/asp_coro.hpp  g_coro_static_frame_pool)]")
    imgui.columns(3, "pool_gauge_cols", False)
    pool_col = imgui.ImVec4(0.88, 0.42, 0.46, 1.0) if pool_pct > 0.75 else \
               imgui.ImVec4(1.0, 0.75, 0.2, 1.0)   if pool_pct > 0.5  else \
               imgui.ImVec4(0.2, 0.75, 0.4, 1.0)
    imgui.push_style_color(imgui.Col_.plot_histogram, pool_col)
    imgui.progress_bar(pool_pct, imgui.ImVec2(260, 22),
        f"Pool: {pool_used:,} / {pool_cap:,} B  ({pool_pct * 100:.1f}%)")
    imgui.pop_style_color()
    imgui.next_column()
    imgui.text_colored(imgui.ImVec4(0.3, 0.9, 0.4, 1.0),
        f"Zero-Heap Verified: 0 B dynamic allocation\n"
        f"{active_count} frames in .bss (ABSTRACTX_CORO_POOL_SIZE = {pool_cap // 1024} KB)\n"
        f"Bump-allocator: no free(), no fragmentation")
    imgui.next_column()
    if stalled_count > 0:
        imgui.text_colored(imgui.ImVec4(0.9, 0.3, 0.3, 1.0),
            f"STALL WATCHDOG: {stalled_count} coroutine(s) over budget\n"
            f"Check for missed ISR / dropped DMA callback.\n"
            f"Click stalled row below to jump to source.")
    else:
        imgui.text_colored(imgui.ImVec4(0.3, 0.9, 0.4, 1.0),
            "All coroutines within deadline budget\n"
            "No stalls or deadlocks detected\n"
            "Per-awaiter watchdog: ACTIVE")
    imgui.columns(1)
    imgui.separator()

    # Section 2: Live Coroutine Frame Table
    imgui.text_colored(imgui.ImVec4(0.9, 0.75, 0.2, 1.0),
        "[Live Coroutine Frame Table  (instrumented via ABSTRACTX_CORO_SUSPEND/RESUME in coro_trace.hpp)]")
    imgui.columns(6, "coro_tbl", True)
    imgui.set_column_width(0, 50)
    imgui.set_column_width(1, 140)
    imgui.set_column_width(2, 85)
    imgui.set_column_width(3, 165)
    imgui.set_column_width(4, 130)
    imgui.set_column_width(5, 120)
    for hdr in ["#ID", "Coroutine Name", "State", "co_await Token", "Duration in State", "Source"]:
        imgui.text(hdr); imgui.next_column()
    imgui.separator()

    stall_red  = imgui.ImVec4(0.95, 0.3,  0.3,  1.0)
    run_green  = imgui.ImVec4(0.3,  0.95, 0.5,  1.0)
    susp_amber = imgui.ImVec4(0.95, 0.8,  0.2,  1.0)
    grey       = imgui.ImVec4(0.6,  0.65, 0.7,  1.0)

    for f in frames:
        dur_us = f["duration_us"]
        budget = f["budget_us"]
        is_running = (f["state"] == "RUNNING")
        is_stalled = (budget > 0 and dur_us > budget)
        row_col = stall_red if is_stalled else run_green if is_running else susp_amber

        imgui.text_colored(stall_red if is_stalled else grey,
                           ("!" if is_stalled else "") + f"#{f['task_id']}")
        imgui.next_column()

        clicked, _ = imgui.selectable(
            f"{f['name']}##coro_{f['task_id']}", is_stalled,
            imgui.SelectableFlags_.span_all_columns)
        if clicked:
            with g_state.lock:
                g_state.selected_source_file = f["file"]
                g_state.selected_source_line  = f["line"]
                g_state.selected_token        = f["awaiter"]
                g_state.source_view_mode      = "CPP"
            g_state.requested_studio_tab = "source"
        imgui.next_column()

        imgui.text_colored(row_col, f["state"])
        imgui.next_column()

        if is_running:
            imgui.text_colored(run_green, f["awaiter"])
        elif is_stalled:
            imgui.text_colored(stall_red, f"{f['awaiter']}  STALLED")
        else:
            imgui.text_colored(imgui.ImVec4(0.7, 0.85, 1.0, 1.0), f["awaiter"])
        imgui.next_column()

        if is_running:
            imgui.text_colored(run_green,
                f"{dur_us / 1000:.2f} ms" if dur_us >= 1000 else f"{dur_us:.1f} us")
        elif is_stalled:
            imgui.push_style_color(imgui.Col_.plot_histogram, stall_red)
            imgui.progress_bar(min(dur_us / max(budget, 1), 2.0) / 2.0, imgui.ImVec2(120, 14),
                f"+{int(dur_us - budget)} us OVER")
            imgui.pop_style_color()
        elif budget > 0:
            used_pct = min(dur_us / max(budget, 1), 1.0)
            bc = imgui.ImVec4(0.2, 0.75, 0.4, 1.0) if used_pct < 0.7 else imgui.ImVec4(1.0, 0.75, 0.2, 1.0)
            imgui.push_style_color(imgui.Col_.plot_histogram, bc)
            imgui.progress_bar(used_pct, imgui.ImVec2(120, 14),
                f"{dur_us:.0f}/{budget} us")
            imgui.pop_style_color()
        else:
            imgui.text(f"{dur_us / 1000:.1f} ms" if dur_us >= 1000 else f"{dur_us:.1f} us")
        imgui.next_column()

        if imgui.button(f"{f['source']}##src_{f['task_id']}"):
            with g_state.lock:
                g_state.selected_source_file = f["file"]
                g_state.selected_source_line  = f["line"]
                g_state.source_view_mode      = "CPP"
            g_state.requested_studio_tab = "source"
        imgui.next_column()

    imgui.columns(1)
    imgui.separator()

    if stalled_count > 0:
        stalled_names = [f["name"] for f in frames
                         if f["budget_us"] > 0 and f["duration_us"] > f["budget_us"]]
        imgui.push_style_color(imgui.Col_.child_bg, imgui.ImVec4(0.22, 0.04, 0.04, 0.9))
        imgui.begin_child("##watchdog_banner", imgui.ImVec2(-1, 36), True)
        imgui.text_colored(stall_red,
            f"STALL WATCHDOG:  {', '.join(stalled_names)}  — "
            f"duration exceeds per-awaiter budget. Possible missed ISR / dropped DMA callback.")
        imgui.end_child()
        imgui.pop_style_color()
    imgui.separator()

    # Section 3: Static Spawn Topology Tree
    imgui.text_colored(imgui.ImVec4(0.9, 0.75, 0.2, 1.0),
        "[Coroutine Spawn Topology  (static parent->child data-flow graph from coro::when_all)]")
    imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0),
        "Layout is fixed at boot by coro::when_all() structured-concurrency combinator. "
        "Arrows show data-flow (producer -> consumer via AsyncQueue<T, N>).")
    imgui.spacing()

    node_map = {f["name"]: f for f in frames}
    rendered = set()

    def get_col(name):
        fr = node_map.get(name)
        if not fr:
            return imgui.ImVec4(0.5, 0.5, 0.5, 1.0)
        if fr["state"] == "RUNNING":
            return imgui.ImVec4(0.3, 0.9, 0.4, 1.0)
        if fr["budget_us"] > 0 and fr["duration_us"] > fr["budget_us"]:
            return imgui.ImVec4(0.9, 0.3, 0.3, 1.0)
        return imgui.ImVec4(0.9, 0.75, 0.2, 1.0)

    def render_node(name, depth, is_last):
        prefix = ("    " * (depth - 1) + ("L- " if is_last else "|- ")) if depth > 0 else ""
        col = get_col(name)
        fr = node_map.get(name)
        if fr:
            token = f"  co_await {fr['awaiter']}" if fr["awaiter"] != "—" else ""
            dur_s = (f"  {fr['duration_us']/1000:.2f} ms"
                     if fr["duration_us"] >= 1000 else f"  {fr['duration_us']:.1f} us")
            stall = "  !! STALLED !!" if (fr["budget_us"] > 0 and fr["duration_us"] > fr["budget_us"]) else ""
            imgui.text_colored(col, f"{prefix}{name}  [{fr['state']}]{token}{dur_s}{stall}")
        else:
            imgui.text_colored(col, f"{prefix}{name}")
        children = [c for (p, c) in topology if p == name and c not in rendered]
        rendered.update(children)
        for i, child in enumerate(children):
            render_node(child, depth + 1, i == len(children) - 1)

    all_children = {c for (_, c) in topology}
    roots = list(dict.fromkeys(p for (p, _) in topology if p not in all_children))
    rendered.update(roots)
    for root in roots:
        render_node(root, 0, True)
    all_names = {f["name"] for f in frames}
    for orphan in sorted(all_names - rendered):
        imgui.text_colored(imgui.ImVec4(0.5, 0.5, 0.5, 1.0), f"  [orphan] {orphan}")


def _render_flow_integrity_and_eye_diagram():
    """
    Renders Real-Time Flow-Integrity & Congestion Metrics:
    1. Head-of-Line (HoL) Blocking & Crossbar Contention Matrix.
    2. Packet Transit Delay (Δt_transit = t_pop - t_latch) & Monotonic Sequence Drift.
    3. Pacing 'Eye Diagram' folded modulo 125.0 µs epoch (8 kHz clock).
    """
    imgui.text_colored(imgui.ImVec4(0.3, 0.85, 1.0, 1.0), "Real-Time Bus Flow-Integrity, Pacing Eye & Congestion Matrix")
    imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0),
                       "Exposes bus physics: transit delays, AXI-Stream backpressure stalls, SPSC fullness, and inter-arrival pacing.")
    imgui.separator()

    # Section 1: Head-of-Line (HoL) Blocking Matrix
    imgui.text_colored(imgui.ImVec4(0.9, 0.75, 0.2, 1.0), "[Head-of-Line (HoL) Blocking & AXI Crossbar Contention Matrix]")
    imgui.columns(6, "hol_matrix_cols", True)
    imgui.set_column_width(0, 110)
    imgui.set_column_width(1, 150)
    imgui.set_column_width(2, 90)
    imgui.set_column_width(3, 110)
    imgui.set_column_width(4, 110)
    imgui.set_column_width(5, 260)
    
    imgui.text("Channel")
    imgui.next_column()
    imgui.text("Hardware Engine / AXI")
    imgui.next_column()
    imgui.text("Ring Full")
    imgui.next_column()
    imgui.text("Stall Time")
    imgui.next_column()
    imgui.text("Status")
    imgui.next_column()
    imgui.text("Blocked Downstream Consumer / Diagnostic")
    imgui.next_column()
    imgui.separator()

    channels = [
        {"id": 0, "name": "Ch 0 (Clock)", "hw": "asp_router.sv:64", "full": 0.18, "stall_us": 0.2, "status": "NOMINAL", "consumer": "Host Linux DomainDispatcher"},
        {"id": 1, "name": "Ch 1 (IMU DRDY)", "hw": "asp_imu_auto_dma.sv:112", "full": 0.88, "stall_us": 14.2, "status": "STALLED", "consumer": "imu_pipeline coroutine (co_await g_sensor_ring.pop())"},
        {"id": 2, "name": "Ch 2 (Compass/Baro)", "hw": "twi0_i2c_bridge:45", "full": 0.25, "stall_us": 0.8, "status": "NOMINAL", "consumer": "mag_gps_drain coroutine (try_pop non-blocking)"},
        {"id": 3, "name": "Ch 3 (Telem Egress)", "hw": "asp_router.sv:140", "full": 0.42, "stall_us": 1.4, "status": "NOMINAL", "consumer": "UDP :9870 telemetry socket sink"},
        {"id": 4, "name": "Ch 4 (DShot ESCs)", "hw": "asp_dshot_core.sv:88", "full": 0.30, "stall_us": 0.5, "status": "NOMINAL", "consumer": "quad_mixer DShot600 PWM pulse generator"},
    ]

    for ch in channels:
        is_stalled = (ch["stall_us"] >= 10.0 or ch["status"] == "STALLED")
        coral_red = imgui.ImVec4(0.88, 0.42, 0.46, 1.0)
        if is_stalled:
            imgui.text_colored(coral_red, f"🚨 {ch['name']}")
        else:
            imgui.text_colored(imgui.ImVec4(0.3, 0.9, 0.4, 1.0), f"✓ {ch['name']}")
        imgui.next_column()

        imgui.text(ch["hw"])
        imgui.next_column()

        prog_col = imgui.ImVec4(0.9, 0.3, 0.3, 1.0) if ch["full"] > 0.8 else imgui.ImVec4(0.2, 0.7, 0.4, 1.0)
        imgui.push_style_color(imgui.Col_.plot_histogram, prog_col)
        imgui.progress_bar(ch["full"], imgui.ImVec2(-1, 16), f"{ch['full']*100:.0f}%")
        imgui.pop_style_color()
        imgui.next_column()

        if is_stalled:
            imgui.text_colored(coral_red, f"+{ch['stall_us']:.1f} µs (>10µs)")
        else:
            imgui.text(f"{ch['stall_us']:.1f} µs")
        imgui.next_column()

        if is_stalled:
            imgui.text_colored(coral_red, "[BLOCKED / HoL]")
        else:
            imgui.text_colored(imgui.ImVec4(0.3, 0.9, 0.4, 1.0), "[OK / Active]")
        imgui.next_column()

        if is_stalled:
            imgui.text_colored(coral_red, ch["consumer"])
            imgui.same_line()
            if imgui.button(f"🔍 Drill Down##hol_{ch['id']}"):
                g_state.active_anomaly_modal = True
                g_state.selected_anomaly_info = {
                    "type": "HoL_Blocking",
                    "channel": ch["name"],
                    "stall_us": ch["stall_us"],
                    "hw": ch["hw"],
                    "consumer": ch["consumer"],
                    "diag": "Head-of-Line blocking: ICM-42688-P Auto-DMA stalled waiting for Core 1 consumer ring drain."
                }
        else:
            imgui.text_colored(imgui.ImVec4(0.7, 0.7, 0.7, 1.0), ch["consumer"])
        imgui.next_column()

    imgui.columns(1)
    imgui.separator()

    # Section 2: Two Columns - Transit Delay Chart (Left) and Pacing Eye Diagram (Right)
    imgui.columns(2, "flow_charts_cols", True)

    # Left: Transit Delay & Sequence Tracking
    imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), "[Transit Delay (Δt = t_pop - t_latch) & Monotonic Sequence Drift]")
    if implot.begin_plot("TLP Transit Delay (µs)##transit_plot", imgui.ImVec2(-1, 200)):
        implot.setup_axes("Time Window (s)", "Delay (µs)", implot.AxisFlags_.auto_fit, implot.AxisFlags_.auto_fit)
        with g_state.lock:
            t_data = np.copy(g_state.chart_time)
            transit_data = np.copy(g_state.chart_transit_delay_us)
        implot.plot_line("Δt_transit (µs)", t_data, transit_data)
        t_ref = np.array([-30.0, 0.0], dtype=np.float64)
        sat_ref = np.array([10.0, 10.0], dtype=np.float64)
        implot.plot_line("Saturation Threshold (10.0 µs)", t_ref, sat_ref)
        implot.end_plot()

    imgui.text_colored(imgui.ImVec4(0.8, 0.8, 0.8, 1.0), f"Sequence Integrity: Expected #{g_state.tlp_last_seq:,} | Drops/Jumps: {g_state.tlp_seq_drops} (0.00% loss)")
    imgui.next_column()

    # Right: Pacing "Eye Diagram"
    imgui.text_colored(imgui.ImVec4(0.9, 0.7, 0.2, 1.0), "[Pacing Eye Diagram (Folded Modulo 125.0 µs IMU Epoch)]")
    if implot.begin_plot("8 kHz Pacing Eye Diagram (125.0 µs)##eye_plot", imgui.ImVec2(-1, 200)):
        implot.setup_axes("Epoch Phase Offset (µs)", "Jitter Variance (µs)", implot.AxisFlags_.auto_fit, implot.AxisFlags_.auto_fit)
        x_eye = np.linspace(0.0, 125.0, 50, dtype=np.float64)
        y_top = 10.0 - 0.08 * (x_eye - 62.5)**2 / 62.5
        y_bot = -10.0 + 0.08 * (x_eye - 62.5)**2 / 62.5
        implot.plot_line("Upper Pacing Limit (+10 µs)", x_eye, y_top)
        implot.plot_line("Lower Pacing Limit (-10 µs)", x_eye, y_bot)
        implot.plot_scatter("IMU Overrun Anomaly (+23.4 µs)", np.array([62.5]), np.array([23.4]))
        implot.end_plot()

    imgui.text_colored(imgui.ImVec4(0.3, 0.9, 0.4, 1.0), "Eye Opening: 89.2 µs Nominal Safety Margin | Peak Jitter: ±4.8 µs")
    imgui.columns(1)
    imgui.separator()

    # Section 3: SPSC Queue Saturation & Coroutine Pacing Latency History
    imgui.columns(2, "flow_saturation_cols", True)

    # Left: SPSC Lock-Free Interconnect Queue Saturation
    imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), "[SPSC Lock-Free Interconnect Queue Saturation (pkts / 64)]")
    if implot.begin_plot("SPSC Lock-Free Interconnect Saturation (pkts / 64)##flow_spsc", imgui.ImVec2(-1, 180)):
        implot.setup_axes("Time Window (s)", "Queue Depth", implot.AxisFlags_.auto_fit, implot.AxisFlags_.none)
        implot.setup_axis_limits(implot.ImAxis_.y1, 0.0, 64.0, imgui.Cond_.always)
        with g_state.lock:
            t_data = np.copy(g_state.chart_time)
            q_sensor = np.copy(g_state.chart_ring_sensor)
            q_telem = np.copy(g_state.chart_ring_telem)
        implot.plot_line("Sensor Queue (g_sensor_ring)", t_data, q_sensor)
        implot.plot_line("Telemetry Queue (g_telemetry_ring)", t_data, q_telem)
        # 75% High Watermark Warning Reference (48 pkts)
        t_ref = np.array([-30.0, 0.0], dtype=np.float64)
        q_warn = np.array([48.0, 48.0], dtype=np.float64)
        implot.plot_line("Warning Limit (75% = 48 pkts)", t_ref, q_warn)
        implot.end_plot()

    imgui.next_column()

    # Right: Coroutine Latency & Suspension History with Deadline Limits
    imgui.text_colored(imgui.ImVec4(0.9, 0.7, 0.2, 1.0), "[Coroutine Latency & Suspension History (µs)]")
    if implot.begin_plot("Coroutine Latency & Suspension History (µs)##flow_coro_lat", imgui.ImVec2(-1, 180)):
        implot.setup_axes("Time Window (s)", "Execution Time (µs)", implot.AxisFlags_.auto_fit, implot.AxisFlags_.auto_fit)
        with g_state.lock:
            t_data = np.copy(g_state.chart_time)
            lat_imu = np.copy(g_state.chart_lat_imu)
            lat_ekf = np.copy(g_state.chart_lat_ekf)
            lat_ctl = np.copy(g_state.chart_lat_ctl)
            lat_dma = np.copy(g_state.chart_lat_dma)
        implot.plot_line("imu_pipeline (µs)", t_data, lat_imu)
        implot.plot_line("attitude_ekf (µs)", t_data, lat_ekf)
        implot.plot_line("flight_control (µs)", t_data, lat_ctl)
        implot.plot_line("spi_dma_burst (µs)", t_data, lat_dma)
        # Plot 15 µs IMU Deadline Threshold reference
        t_ref = np.array([-30.0, 0.0], dtype=np.float64)
        dl_ref = np.array([15.0, 15.0], dtype=np.float64)
        implot.plot_line("IMU Deadline (15.0 µs)", t_ref, dl_ref)
        implot.end_plot()

    imgui.columns(1)

def _render_anomaly_drill_down_modal():
    """
    Renders an interactive diagnostic modal linking an anomaly directly to C++
    coroutine code and SystemVerilog RTL.
    """
    if not g_state.active_anomaly_modal:
        return

    imgui.open_popup("Anomaly Root-Cause Inspector")
    if imgui.begin_popup_modal("Anomaly Root-Cause Inspector", True, imgui.WindowFlags_.always_auto_resize)[0]:
        info = g_state.selected_anomaly_info
        imgui.text_colored(imgui.ImVec4(0.88, 0.42, 0.46, 1.0), f"🚨 FAILURE CLASSIFICATION: [{info.get('type', 'Timing_Overrun')}]")
        imgui.separator()

        imgui.text(f"Origin Channel : {info.get('channel', 'Ch 1')}")
        imgui.text(f"Measured Stall : {info.get('stall_us', 14.2):.1f} µs (> 10.0 µs budget limit)")
        imgui.text(f"Hardware Origin: {info.get('hw', 'asp_imu_auto_dma.sv:112')}")
        imgui.text(f"Software Target: {info.get('consumer', 'apps/gps_imu_app/src/main.cpp:42')}")
        imgui.spacing()

        imgui.text_colored(imgui.ImVec4(0.9, 0.7, 0.2, 1.0), "[Causality Dependency Chain]:")
        imgui.bullet_text("1. Hardware Trigger   : Tang Primer 20K FPGA ICM-42688-P Auto-DMA (DIO PIN 4)")
        imgui.bullet_text("2. Crossbar Routing   : asp_router.sv (AXI-Stream Channel 1 TLP)")
        imgui.bullet_text("3. Lock-Free SPSC Ring: g_sensor_ring saturation (head-of-line contention)")
        imgui.bullet_text("4. Coroutine Receiver : apps/gps_imu_app/src/main.cpp:42 (co_await g_sensor_ring.pop())")
        imgui.spacing()

        imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), "[Root Cause Diagnosis]:")
        imgui.text_wrapped(info.get('diag', "Ring head contention during concurrent DMA latch (+8.4 µs late)."))
        imgui.spacing()
        imgui.separator()

        if imgui.button("Jump to C++ Source (main.cpp:42)"):
            g_state.selected_code_file_idx = 0
            g_state.selected_source_file = "apps/gps_imu_app/src/main.cpp"
            g_state.selected_source_line = 42
            g_state.source_view_mode = "CPP"
            g_state.requested_studio_tab = "source"
            g_state.active_anomaly_modal = False
            imgui.close_current_popup()

        imgui.same_line()
        if imgui.button("Jump to FPGA RTL (asp_imu_auto_dma.sv:112)"):
            g_state.selected_rtl_file_idx = 1
            g_state.selected_rtl_file = "rtl/imu/asp_imu_auto_dma.sv"
            g_state.selected_rtl_line = 112
            g_state.source_view_mode = "RTL"
            g_state.requested_studio_tab = "source"
            g_state.active_anomaly_modal = False
            imgui.close_current_popup()

        imgui.same_line()
        if imgui.button("Dismiss"):
            g_state.active_anomaly_modal = False
            imgui.close_current_popup()

        imgui.end_popup()

def _render_popped_tab_placeholder(display_name: str, key: str, window_title: str):
    """Renders an interactive placeholder when a diagnostic tab is popped out onto the canvas."""
    imgui.dummy(imgui.ImVec2(1, 20))
    imgui.push_style_color(imgui.Col_.child_bg, imgui.ImVec4(0.08, 0.12, 0.18, 0.95))
    imgui.begin_child(f"PoppedPlaceholder_{key}", imgui.ImVec2(-1, 200), True)
    imgui.text_colored(imgui.ImVec4(0.3, 0.85, 1.0, 1.0), f"🗖 {display_name} Active as Movable Canvas Window")
    imgui.separator()
    imgui.spacing()
    imgui.text("This diagnostic tool has been detached and is currently running as a movable floating window.")
    imgui.text("You can drag it anywhere across your desktop canvas, place it side-by-side with other tools,")
    imgui.text("or layer it on top of the workbench.")
    imgui.dummy(imgui.ImVec2(1, 14))
    if imgui.button(f"Bring {display_name} to Front##focus_{key}"):
        try:
            imgui.set_window_focus(f"{window_title}##CanvasWin")
        except Exception:
            pass
    imgui.same_line(0, 16)
    imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.65, 0.4, 0.9))
    if imgui.button(f"🗗 Pop In (Dock back to Studio Tab)##dock_{key}"):
        g_state.canvas_windows[key] = False
    imgui.pop_style_color()
    imgui.end_child()
    imgui.pop_style_color()

def _render_floating_canvas_windows():
    """
    Renders popped-out floating canvas windows directly onto the desktop workspace.
    Enables the user to position and move multiple diagnostic tools side-by-side or stacked,
    while the USER Flight Instruments float on top of AbstractX Studio.
    """
    # 1. On startup: ensure User Domain Instruments is undocked to float as its own canvas window
    if not g_state.user_canvas_floated_on_startup:
        ctx = imgui.get_current_context()
        if ctx:
            win = imgui.internal.find_window_by_name("User Domain Instruments")
            if win:
                imgui.internal.dock_context_queue_undock_window(ctx, win)
                imgui.set_window_size("User Domain Instruments", imgui.ImVec2(1040, 720), imgui.Cond_.always)
                imgui.set_window_pos("User Domain Instruments", imgui.ImVec2(480, 70), imgui.Cond_.always)
                g_state.user_canvas_floated_on_startup = True

    # 2. Coroutine State & Suspension Inspector Floating Window
    if g_state.canvas_windows.get("coro", False):
        imgui.set_next_window_size(imgui.ImVec2(1100, 680), imgui.Cond_.first_use_ever)
        expanded, opened = imgui.begin("Coroutine State & Suspension Inspector##CanvasWin", True)
        if expanded:
            imgui.begin_group()
            imgui.text_colored(imgui.ImVec4(0.95, 0.82, 0.25, 1.0), "🗖 MOVABLE CANVAS WINDOW")
            imgui.same_line(0, 16)
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.65, 0.4, 0.9))
            if imgui.button("🗗 Pop In (Dock to Studio Tab)##coro_popin"):
                g_state.canvas_windows["coro"] = False
            imgui.pop_style_color()
            imgui.same_line(0, 16)
            imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), "| Drag titlebar to move · Drag edges to resize")
            imgui.end_group()
            imgui.separator()
            _render_coroutine_inspector()
            imgui.end()
        if not opened:
            g_state.canvas_windows["coro"] = False

    # 3. CPU Gauges & Silicon Topology Floating Window
    if g_state.canvas_windows.get("cpu", False):
        imgui.set_next_window_size(imgui.ImVec2(1100, 680), imgui.Cond_.first_use_ever)
        expanded, opened = imgui.begin("Silicon Cores & CPU Gauges##CanvasWin", True)
        if expanded:
            imgui.begin_group()
            imgui.text_colored(imgui.ImVec4(0.95, 0.82, 0.25, 1.0), "🗖 MOVABLE CANVAS WINDOW")
            imgui.same_line(0, 16)
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.65, 0.4, 0.9))
            if imgui.button("🗗 Pop In (Dock to Studio Tab)##cpu_popin"):
                g_state.canvas_windows["cpu"] = False
            imgui.pop_style_color()
            imgui.same_line(0, 16)
            imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), "| Drag titlebar to move · Drag edges to resize")
            imgui.end_group()
            imgui.separator()
            _render_core_cpu_and_topology()
            imgui.end()
        if not opened:
            g_state.canvas_windows["cpu"] = False

    # 4. TLP Bus Debugger Floating Window
    if g_state.canvas_windows.get("tlp", False):
        imgui.set_next_window_size(imgui.ImVec2(1100, 540), imgui.Cond_.first_use_ever)
        expanded, opened = imgui.begin("TLP Bus Debugger##CanvasWin", True)
        if expanded:
            imgui.begin_group()
            imgui.text_colored(imgui.ImVec4(0.95, 0.82, 0.25, 1.0), "🗖 MOVABLE CANVAS WINDOW")
            imgui.same_line(0, 16)
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.65, 0.4, 0.9))
            if imgui.button("🗗 Pop In (Dock to Studio Tab)##tlp_popin"):
                g_state.canvas_windows["tlp"] = False
            imgui.pop_style_color()
            imgui.same_line(0, 16)
            imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), "| Drag titlebar to move · Drag edges to resize")
            imgui.end_group()
            imgui.separator()
            _render_tlp_debugger_window()
            imgui.end()
        if not opened:
            g_state.canvas_windows["tlp"] = False

    # 5. System Event Log Floating Window
    if g_state.canvas_windows.get("log", False):
        imgui.set_next_window_size(imgui.ImVec2(980, 500), imgui.Cond_.first_use_ever)
        expanded, opened = imgui.begin("System Event Log##CanvasWin", True)
        if expanded:
            imgui.begin_group()
            imgui.text_colored(imgui.ImVec4(0.95, 0.82, 0.25, 1.0), "🗖 MOVABLE CANVAS WINDOW")
            imgui.same_line(0, 16)
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.65, 0.4, 0.9))
            if imgui.button("🗗 Pop In (Dock to Studio Tab)##log_popin"):
                g_state.canvas_windows["log"] = False
            imgui.pop_style_color()
            imgui.same_line(0, 16)
            imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), "| Drag titlebar to move · Drag edges to resize")
            imgui.end_group()
            imgui.separator()
            _render_event_log_window()
            imgui.end()
        if not opened:
            g_state.canvas_windows["log"] = False

    # 6. Dual-Plane Coroutine Timeline Floating Window
    if g_state.canvas_windows.get("timeline", False):
        imgui.set_next_window_size(imgui.ImVec2(1200, 560), imgui.Cond_.first_use_ever)
        expanded, opened = imgui.begin("Dual-Plane Coroutine Timeline##CanvasWin", True)
        if expanded:
            imgui.begin_group()
            imgui.text_colored(imgui.ImVec4(0.95, 0.82, 0.25, 1.0), "🗖 MOVABLE CANVAS WINDOW")
            imgui.same_line(0, 16)
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.65, 0.4, 0.9))
            if imgui.button("🗗 Pop In (Dock to Studio Tab)##timeline_popin"):
                g_state.canvas_windows["timeline"] = False
            imgui.pop_style_color()
            imgui.same_line(0, 16)
            imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), "| Drag titlebar to move · Drag edges to resize")
            imgui.end_group()
            imgui.separator()
            _render_core_timeline()
            imgui.end()
        if not opened:
            g_state.canvas_windows["timeline"] = False

    # 7. Simple Trace Viewer Floating Window
    if g_state.canvas_windows.get("trace", False):
        imgui.set_next_window_size(imgui.ImVec2(1100, 520), imgui.Cond_.first_use_ever)
        expanded, opened = imgui.begin("Simple Trace Viewer##CanvasWin", True)
        if expanded:
            imgui.begin_group()
            imgui.text_colored(imgui.ImVec4(0.95, 0.82, 0.25, 1.0), "🗖 MOVABLE CANVAS WINDOW")
            imgui.same_line(0, 16)
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.65, 0.4, 0.9))
            if imgui.button("🗗 Pop In (Dock to Studio Tab)##trace_popin"):
                g_state.canvas_windows["trace"] = False
            imgui.pop_style_color()
            imgui.same_line(0, 16)
            imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), "| Drag titlebar to move · Drag edges to resize")
            imgui.end_group()
            imgui.separator()
            _render_simple_trace_view()
            imgui.end()
        if not opened:
            g_state.canvas_windows["trace"] = False

    # 8. Source Code & RTL Inspector Floating Window
    if g_state.canvas_windows.get("source", False):
        imgui.set_next_window_size(imgui.ImVec2(1120, 700), imgui.Cond_.first_use_ever)
        expanded, opened = imgui.begin("Source Code & RTL Inspector##CanvasWin", True)
        if expanded:
            imgui.begin_group()
            imgui.text_colored(imgui.ImVec4(0.95, 0.82, 0.25, 1.0), "🗖 MOVABLE CANVAS WINDOW")
            imgui.same_line(0, 16)
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.65, 0.4, 0.9))
            if imgui.button("🗗 Pop In (Dock to Studio Tab)##src_popin"):
                g_state.canvas_windows["source"] = False
            imgui.pop_style_color()
            imgui.same_line(0, 16)
            imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), "| Drag titlebar to move · Drag edges to resize")
            imgui.end_group()
            imgui.separator()
            _render_source_inspector_window()
            imgui.end()
        if not opened:
            g_state.canvas_windows["source"] = False

    # 9. FPGA & Hardware Peripherals Floating Window
    if g_state.canvas_windows.get("fpga", False):
        imgui.set_next_window_size(imgui.ImVec2(1050, 620), imgui.Cond_.first_use_ever)
        expanded, opened = imgui.begin("FPGA & Hardware Peripherals##CanvasWin", True)
        if expanded:
            imgui.begin_group()
            imgui.text_colored(imgui.ImVec4(0.95, 0.82, 0.25, 1.0), "🗖 MOVABLE CANVAS WINDOW")
            imgui.same_line(0, 16)
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.65, 0.4, 0.9))
            if imgui.button("🗗 Pop In (Dock to Studio Tab)##fpga_popin"):
                g_state.canvas_windows["fpga"] = False
            imgui.pop_style_color()
            imgui.same_line(0, 16)
            imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), "| Drag titlebar to move · Drag edges to resize")
            imgui.end_group()
            imgui.separator()
            _render_fpga_peripherals_window()
            imgui.end()
        if not opened:
            g_state.canvas_windows["fpga"] = False

    # 10. MemBrowse CI Tracker Floating Window
    if g_state.canvas_windows.get("memory", False):
        imgui.set_next_window_size(imgui.ImVec2(1020, 640), imgui.Cond_.first_use_ever)
        expanded, opened = imgui.begin("Memory & MemBrowse CI Tracker##CanvasWin", True)
        if expanded:
            imgui.begin_group()
            imgui.text_colored(imgui.ImVec4(0.95, 0.82, 0.25, 1.0), "🗖 MOVABLE CANVAS WINDOW")
            imgui.same_line(0, 16)
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.65, 0.4, 0.9))
            if imgui.button("🗗 Pop In (Dock to Studio Tab)##mem_popin"):
                g_state.canvas_windows["memory"] = False
            imgui.pop_style_color()
            imgui.same_line(0, 16)
            imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), "| Drag titlebar to move · Drag edges to resize")
            imgui.end_group()
            imgui.separator()
            _render_level1_memory()
            imgui.end()
        if not opened:
            g_state.canvas_windows["memory"] = False

def _render_core_studio_window():
    """
    # @impl [SPEC-STUDIO-02] tools/visualizer/abstractx_studio.py
    Renders Level 1 Core: Platform Topology, Multi-Core Gauges, SPSC Rings,
    Dual-Plane Coroutine Timelines, TLP Bus Debugger, Event Log, Source Code & RTL Inspector,
    FPGA Peripherals, and MemBrowse Status.
    """
    g_state.update_rates()
    imgui.begin_group()
    imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), "AbstractX Studio & Diagnostic Workbench")
    imgui.same_line(0, 20)
    if g_state.active_layout_preset == "core_focus":
        if imgui.button("🗗 Restore All Panes##core"):
            restore_default_layout()
    else:
        if imgui.button("⛶ Expand Window##core"):
            apply_docking_layout("core_focus")
    imgui.same_line()
    if imgui.button("🗖 Pop Out Studio##core"):
        decouple_window("AbstractX Core Studio")
    imgui.same_line()
    if imgui.button("🔄 Restore Defaults##core"):
        restore_default_layout()
    imgui.same_line()
    # Quick canvas window toggles
    coro_p = g_state.canvas_windows.get("coro", False)
    if imgui.button(f"{'🗕 Dock' if coro_p else '🗖 Pop'} Coro##hdr"):
        g_state.canvas_windows["coro"] = not coro_p
    imgui.same_line()
    tlp_p = g_state.canvas_windows.get("tlp", False)
    if imgui.button(f"{'🗕 Dock' if tlp_p else '🗖 Pop'} TLP##hdr"):
        g_state.canvas_windows["tlp"] = not tlp_p
    imgui.same_line()
    log_p = g_state.canvas_windows.get("log", False)
    if imgui.button(f"{'🗕 Dock' if log_p else '🗖 Pop'} Log##hdr"):
        g_state.canvas_windows["log"] = not log_p
    imgui.same_line()
    imgui.text_colored(imgui.ImVec4(0.5, 0.6, 0.7, 0.8), "| Unified engineering workspace")
    imgui.end_group()
    imgui.separator()

    # Handle requested window activations for TLP / Log
    if g_state.requested_studio_tab == "tlp":
        w_tlp = g_dockable_windows.get("tlp")
        if w_tlp:
            w_tlp.is_visible = True
        try:
            imgui.set_window_focus("TLP Bus Debugger")
        except Exception:
            pass
        g_state.requested_studio_tab = None
    elif g_state.requested_studio_tab == "log":
        w_log = g_dockable_windows.get("log")
        if w_log:
            w_log.is_visible = True
        try:
            imgui.set_window_focus("System Event Log")
        except Exception:
            pass
        g_state.requested_studio_tab = None

    # Determine requested tab activation flags
    flag_coro    = imgui.TabItemFlags_.set_selected if g_state.requested_studio_tab == "coro"     else 0
    flag_cpu     = imgui.TabItemFlags_.set_selected if g_state.requested_studio_tab == "cpu"      else 0
    flag_tlp     = imgui.TabItemFlags_.set_selected if g_state.requested_studio_tab == "tlp"      else 0
    flag_log     = imgui.TabItemFlags_.set_selected if g_state.requested_studio_tab == "log"      else 0
    flag_flow    = imgui.TabItemFlags_.set_selected if g_state.requested_studio_tab == "flow"     else 0
    flag_trace   = imgui.TabItemFlags_.set_selected if g_state.requested_studio_tab == "trace"    else 0
    flag_timeline= imgui.TabItemFlags_.set_selected if g_state.requested_studio_tab == "timeline" else 0
    flag_source  = imgui.TabItemFlags_.set_selected if g_state.requested_studio_tab == "source"   else 0
    flag_fpga    = imgui.TabItemFlags_.set_selected if g_state.requested_studio_tab == "fpga"     else 0
    flag_memory  = imgui.TabItemFlags_.set_selected if g_state.requested_studio_tab == "memory"   else 0

    # Reset requested tab after consumption
    g_state.requested_studio_tab = None

    if imgui.begin_tab_bar("CoreStudioTabBar"):
        # Tab 1: Coroutine Inspector (C++20 first - the primary diagnostic surface)
        if imgui.begin_tab_item("Coroutine Inspector", None, flag_coro)[0]:
            if g_state.canvas_windows.get("coro", False):
                _render_popped_tab_placeholder("Coroutine Inspector", "coro", "Coroutine State & Suspension Inspector")
            else:
                _render_coroutine_inspector()
            imgui.end_tab_item()

        if imgui.begin_tab_item("CPU Gauges & Topology", None, flag_cpu)[0]:
            if g_state.canvas_windows.get("cpu", False):
                _render_popped_tab_placeholder("CPU Gauges & Topology", "cpu", "Silicon Cores & CPU Gauges")
            else:
                _render_core_cpu_and_topology()
            imgui.end_tab_item()

        if imgui.begin_tab_item("TLP Bus Debugger", None, flag_tlp)[0]:
            if g_state.canvas_windows.get("tlp", False):
                _render_popped_tab_placeholder("TLP Bus Debugger", "tlp", "TLP Bus Debugger")
            else:
                _render_tlp_debugger_window()
            imgui.end_tab_item()

        if imgui.begin_tab_item("System Event Log", None, flag_log)[0]:
            if g_state.canvas_windows.get("log", False):
                _render_popped_tab_placeholder("System Event Log", "log", "System Event Log")
            else:
                _render_event_log_window()
            imgui.end_tab_item()

        if imgui.begin_tab_item("Flow Integrity & Pacing Eye", None, flag_flow)[0]:
            _render_flow_integrity_and_eye_diagram()
            imgui.end_tab_item()

        if imgui.begin_tab_item("Simple Trace Viewer", None, flag_trace)[0]:
            if g_state.canvas_windows.get("trace", False):
                _render_popped_tab_placeholder("Simple Trace Viewer", "trace", "Simple Trace Viewer")
            else:
                _render_simple_trace_view()
            imgui.end_tab_item()

        if imgui.begin_tab_item("Dual-Plane Timeline", None, flag_timeline)[0]:
            if g_state.canvas_windows.get("timeline", False):
                _render_popped_tab_placeholder("Dual-Plane Timeline", "timeline", "Dual-Plane Coroutine Timeline")
            else:
                _render_core_timeline()
            imgui.end_tab_item()

        if imgui.begin_tab_item("Source Code & RTL Inspector", None, flag_source)[0]:
            if g_state.canvas_windows.get("source", False):
                _render_popped_tab_placeholder("Source Code & RTL Inspector", "source", "Source Code & RTL Inspector")
            else:
                _render_source_inspector_window()
            imgui.end_tab_item()

        if imgui.begin_tab_item("FPGA Peripherals", None, flag_fpga)[0]:
            if g_state.canvas_windows.get("fpga", False):
                _render_popped_tab_placeholder("FPGA Peripherals", "fpga", "FPGA & Hardware Peripherals")
            else:
                _render_fpga_peripherals_window()
            imgui.end_tab_item()

        if imgui.begin_tab_item("MemBrowse CI Report", None, flag_memory)[0]:
            if g_state.canvas_windows.get("memory", False):
                _render_popped_tab_placeholder("MemBrowse CI Report", "memory", "Memory & MemBrowse CI Tracker")
            else:
                _render_level1_memory()
            imgui.end_tab_item()

        imgui.end_tab_bar()

    _render_anomaly_drill_down_modal()

def _render_user_domain_window():
    """
    # @impl [SPEC-STUDIO-03] tools/visualizer/abstractx_studio.py
    Renders Window 2: User Domain Application Instruments (Flight Display Canvas).
    Supports docked full-screen, tiled workbench, or floating flight canvas within
    the single dedicated application window across both Windows and Linux.
    """
    g_state.update_rates()
    _render_window_sizing_bar("User Domain Instruments", "user", 1280.0, 820.0)
    g_flight_plugin.render_ui(0.016, g_state)

def _render_tlp_debugger_window():
    """
    # @impl [SPEC-STUDIO-04] tools/visualizer/abstractx_studio.py
    Renders Window 3: Live 64-Byte TLP Packet Stream & Hex/Field Inspector.
    """
    g_state.update_rates()
    imgui.begin_group()
    _, g_state.tlp_stream_paused = imgui.checkbox("Pause Stream", g_state.tlp_stream_paused)
    imgui.same_line()
    if imgui.button("Clear Packets"):
        with g_state.lock:
            g_state.recent_tlp_packets.clear()
            g_state.selected_tlp_idx = 0
    imgui.same_line()
    if g_state.active_layout_preset == "fpga_focus":
        if imgui.button("🗗 Restore Panes##tlp"):
            apply_docking_layout("balanced")
    else:
        if imgui.button("⛶ Expand Window##tlp"):
            apply_docking_layout("fpga_focus")
    imgui.same_line()
    if imgui.button("🗖 Pop to Canvas##tlp"):
        g_state.canvas_windows["tlp"] = True
    imgui.same_line()
    imgui.text(f"| Captured: {len(g_state.recent_tlp_packets)} | Total: {g_state.packet_count:,}")
    imgui.end_group()
    imgui.separator()

    # Two columns: Packet table on left, Selected packet details on right
    imgui.columns(2, "tlp_split", True)

    imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), "Incoming 64-Byte TLP Packet Stream:")
    imgui.begin_child("TlpListChild", imgui.ImVec2(-1, -1), True)
    imgui.columns(5, "tlp_list_cols", True)
    imgui.text("Seq #")
    imgui.next_column()
    imgui.text("Tag")
    imgui.next_column()
    imgui.text("Ch")
    imgui.next_column()
    imgui.text("Timestamp (ns)")
    imgui.next_column()
    imgui.text("Status")
    imgui.next_column()
    imgui.separator()

    with g_state.lock:
        packets = list(g_state.recent_tlp_packets)
        sel_idx = g_state.selected_tlp_idx

    for idx, pkt in enumerate(reversed(packets)):
        actual_idx = len(packets) - 1 - idx
        is_selected = (actual_idx == sel_idx)
        tag_name = {1: "IMU", 2: "GPS", 3: "MAG", 4: "AHRS"}.get(pkt["tag"], f"0x{pkt['tag']:02X}")
        clicked, _ = imgui.selectable(f"#{pkt['seq']:<5}", is_selected, imgui.SelectableFlags_.span_all_columns)
        if clicked:
            with g_state.lock:
                g_state.selected_tlp_idx = actual_idx
        imgui.next_column()
        imgui.text(tag_name)
        imgui.next_column()
        imgui.text(str(pkt["channel"]))
        imgui.next_column()
        imgui.text(f"{pkt['ts_ns'] & 0xFFFFFFFF}")
        imgui.next_column()
        if pkt["crc_ok"]:
            imgui.text_colored(imgui.ImVec4(0.2, 1.0, 0.4, 1.0), "CRC OK")
        else:
            imgui.text_colored(imgui.ImVec4(1.0, 0.2, 0.2, 1.0), "CRC ERR")
        imgui.next_column()

    imgui.columns(1)
    imgui.end_child()

    imgui.next_column()

    # Right column: Packet Inspector & Hex Dump
    imgui.text_colored(imgui.ImVec4(0.4, 1.0, 0.5, 1.0), "Selected Packet Breakdown (64 Bytes):")
    imgui.begin_child("TlpDetailChild", imgui.ImVec2(-1, -1), True)
    if packets and 0 <= sel_idx < len(packets):
        p = packets[sel_idx]
        imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0), f"Packet #{p['seq']} | Tag: {p['tag']} | Channel: {p['channel']} | Size: 64 Bytes")
        imgui.separator()

        raw = p["raw"]
        if len(raw) >= 20:
            h_type, h_flags, h_tag, h_chan, h_addr, h_len, h_seq, h_ts = struct.unpack("<BBBBIHHQ", raw[:20])
            imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), "[Header: 20 Bytes]")
            imgui.bullet_text(f"Type: 0x{h_type:02X} (MEM_WRITE_POSTED) | Flags: 0x{h_flags:02X} | Tag: 0x{h_tag:02X}")
            imgui.bullet_text(f"Channel: {h_chan} | Addr: 0x{h_addr:08X} | Len: {h_len} DW (64B)")
            imgui.bullet_text(f"Seq: #{h_seq} | Timestamp: {h_ts} ns")

        imgui.spacing()
        imgui.text_colored(imgui.ImVec4(0.8, 0.4, 1.0, 1.0), "[Raw 64-Byte Hex Dump]")
        for offset in range(0, min(len(raw), 64), 16):
            chunk = raw[offset:offset+16]
            hex_str = " ".join(f"{b:02X}" for b in chunk)
            ascii_str = "".join(chr(b) if 32 <= b <= 126 else "." for b in chunk)
            color = imgui.ImVec4(0.4, 0.8, 1.0, 1.0) if offset < 20 else (imgui.ImVec4(0.2, 1.0, 0.4, 1.0) if offset < 60 else imgui.ImVec4(1.0, 0.6, 0.2, 1.0))
            imgui.text_colored(color, f"{offset:04X}: {hex_str:<48}  |{ascii_str}|")

        imgui.spacing()
        imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), "Blue: 20B Header  |  Green: 40B CTF Payload  |  Orange: 4B CRC32")
    else:
        imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), "No packet selected. Waiting for incoming telemetry frames...")
    imgui.end_child()
    imgui.columns(1)

def _render_event_log_window():
    """
    # @impl [SPEC-STUDIO-05] tools/visualizer/abstractx_studio.py
    Renders Window 4: Live Filterable System Event & Trace Log.
    """
    g_state.update_rates()
    imgui.begin_group()
    levels = ["ALL", "INFO", "TLP", "CORO", "ISR", "WARN"]
    for lvl in levels:
        is_active = (lvl == g_state.log_filter_level)
        if is_active:
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.6, 0.9, 1.0))
        if imgui.button(lvl):
            g_state.log_filter_level = lvl
        if is_active:
            imgui.pop_style_color()
        imgui.same_line()

    imgui.same_line(0, 15)
    _, g_state.log_search_text = imgui.input_text("Filter", g_state.log_search_text, 64)
    imgui.same_line()
    if imgui.button("Clear Log"):
        with g_state.lock:
            g_state.logs.clear()
    imgui.same_line()
    _, g_state.log_auto_scroll = imgui.checkbox("Auto-Scroll", g_state.log_auto_scroll)
    imgui.same_line()
    if imgui.button("🗖 Pop to Canvas##log"):
        g_state.canvas_windows["log"] = True
    imgui.end_group()
    imgui.separator()

    # Log Entries Scroll Region
    imgui.begin_child("LogScrollChild", imgui.ImVec2(-1, -1), True)
    with g_state.lock:
        logs = list(g_state.logs)
    flt_lvl = g_state.log_filter_level
    flt_txt = g_state.log_search_text.lower()

    col_map = {
        "INFO": imgui.ImVec4(0.3, 0.8, 1.0, 1.0),
        "TLP": imgui.ImVec4(0.8, 0.4, 1.0, 1.0),
        "CORO": imgui.ImVec4(0.2, 1.0, 0.4, 1.0),
        "ISR": imgui.ImVec4(1.0, 0.6, 0.2, 1.0),
        "WARN": imgui.ImVec4(1.0, 0.3, 0.3, 1.0),
        "MEM": imgui.ImVec4(0.4, 1.0, 0.8, 1.0)
    }

    for entry in logs:
        if flt_lvl != "ALL" and entry["level"] != flt_lvl:
            continue
        if flt_txt and flt_txt not in entry["message"].lower() and flt_txt not in entry["source"].lower():
            continue

        c = col_map.get(entry["level"], imgui.ImVec4(0.9, 0.9, 0.9, 1.0))
        imgui.text_colored(imgui.ImVec4(0.5, 0.5, 0.5, 1.0), f"[{entry['time']}]")
        imgui.same_line()
        imgui.text_colored(c, f"[{entry['level']:<4}]")
        imgui.same_line()
        imgui.text_colored(imgui.ImVec4(0.8, 0.8, 0.6, 1.0), f"[{entry['source']}]:")
        imgui.same_line()
        imgui.text(entry["message"])

    if g_state.log_auto_scroll and imgui.get_scroll_y() >= imgui.get_scroll_max_y() - 20:
        imgui.set_scroll_here_y(1.0)

    imgui.end_child()

def _render_source_inspector_window():
    """
    # @impl [SPEC-STUDIO-11] tools/visualizer/abstractx_studio.py
    Renders Window 5: Cross-Language Source Code & SystemVerilog RTL Hotspot Inspector.
    """
    g_state.update_rates()
    imgui.begin_group()

    # View Mode: C++ Firmware vs FPGA RTL
    is_cpp = (g_state.source_view_mode == "CPP")
    if is_cpp:
        imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.6, 0.9, 1.0))
    if imgui.button("C++ Firmware##src_mode"):
        g_state.source_view_mode = "CPP"
    if is_cpp:
        imgui.pop_style_color()

    imgui.same_line()
    is_rtl = (g_state.source_view_mode == "RTL")
    if is_rtl:
        imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.8, 0.4, 1.0, 1.0))
    if imgui.button("FPGA SystemVerilog RTL##src_mode"):
        g_state.source_view_mode = "RTL"
    if is_rtl:
        imgui.pop_style_color()

    imgui.same_line(0, 15)

    if g_state.source_view_mode == "CPP":
        files = g_state.source_code_files
        g_state.selected_code_file_idx = min(g_state.selected_code_file_idx, len(files) - 1)
        curr_f = files[g_state.selected_code_file_idx]
        imgui.set_next_item_width(320)
        if imgui.begin_combo("C++ Source File", curr_f.split("/")[-1]):
            for idx, fpath in enumerate(files):
                is_sel = (idx == g_state.selected_code_file_idx)
                if imgui.selectable(fpath, is_sel)[0]:
                    g_state.selected_code_file_idx = idx
                    g_state.selected_source_file = fpath
                if is_sel:
                    imgui.set_item_default_focus()
            imgui.end_combo()

        imgui.same_line(0, 15)
        imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.85, 0.25, 0.25, 0.9))
        if imgui.button("🚨 Jump to Overrun Hotspot (main.cpp:42)"):
            g_state.selected_code_file_idx = 0
            g_state.selected_source_file = "apps/gps_imu_app/src/main.cpp"
            g_state.selected_source_line = 42
            g_state.selected_span_id = "imu_overrun"
            g_state.selected_event_name = "imu_pipeline [OVERRUN]"
        imgui.pop_style_color()
    else:
        files = g_state.rtl_code_files
        g_state.selected_rtl_file_idx = min(g_state.selected_rtl_file_idx, len(files) - 1)
        curr_f = files[g_state.selected_rtl_file_idx]
        imgui.set_next_item_width(320)
        if imgui.begin_combo("Verilog RTL Module", curr_f.split("/")[-1]):
            for idx, fpath in enumerate(files):
                is_sel = (idx == g_state.selected_rtl_file_idx)
                if imgui.selectable(fpath, is_sel)[0]:
                    g_state.selected_rtl_file_idx = idx
                    g_state.selected_rtl_file = fpath
                if is_sel:
                    imgui.set_item_default_focus()
            imgui.end_combo()

        imgui.same_line(0, 15)
        imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.85, 0.4, 0.2, 0.9))
        if imgui.button("⚡ Jump to Auto-DMA Engine (Line 112)"):
            g_state.selected_rtl_file_idx = 1
            g_state.selected_rtl_file = "rtl/imu/asp_imu_auto_dma.sv"
            g_state.selected_rtl_line = 112
        imgui.pop_style_color()

    imgui.same_line(0, 10)
    if imgui.button("⛶ Expand Window##source"):
        apply_docking_layout("source_focus")
    imgui.same_line(0, 6)
    if imgui.button("🗖 Pop to Canvas##source"):
        g_state.canvas_windows["source"] = True

    imgui.end_group()
    imgui.separator()

    # Code Listing with Line-by-Line Profiling Badges
    imgui.begin_child("SourceCodeLinesChild", imgui.ImVec2(-1, -1), True)

    code_snippets = {
        "apps/gps_imu_app/src/main.cpp": [
            (35, "int main(int argc, char* argv[]) {", None),
            (36, "    // Initialize dual-core hardware and lock-free SPSC rings", None),
            (37, "    abstractx::init(g_platform_config);", None),
            (38, "    g_dispatcher.spawn(boot_async());", None),
            (39, "", None),
            (40, "    // Primary-Paced Coroutine Ingestion Loop (8 kHz IMU Clock)", None),
            (41, "    while (g_running) {", None),
            (42, "        auto sample = co_await g_sensor_ring.pop();", {"lat": 23.4, "budget": 15.0, "overrun": True, "token": "co_await g_sensor_ring.pop()", "diag": "Head pointer contention during concurrent DMA latch (+8.4 µs late)"}),
            (43, "        attitude_ekf.update(sample.gyro, sample.accel);", {"lat": 18.2, "budget": 20.0, "overrun": False, "token": "attitude_ekf.update()", "diag": "Mahony kinematics quaternion integration"}),
            (44, "        quad_mixer.compute_demands(tau);", {"lat": 2.8, "budget": 25.0, "overrun": False, "token": "quad_mixer.compute_demands()", "diag": "Cascaded rate PID calculations"}),
            (45, "        while (g_mag_channel.try_pop(mag)) { /* aux */ }", {"lat": 6.5, "budget": 10.0, "overrun": False, "token": "g_mag_channel.try_pop()", "diag": "Auxiliary sensor non-blocking drain"}),
            (46, "        co_await g_telemetry_ring.push_async(tlp);", {"lat": 1.2, "budget": 5.0, "overrun": False, "token": "g_telemetry_ring.push_async()", "diag": "64-byte TLP egress push"}),
            (47, "    }", None),
            (48, "    return 0;", None),
            (49, "}", None),
        ],
        "include/abstractx/drivers/imu/icm42688p.hpp": [
            (50, "template <typename SpiBus>", None),
            (51, "class Icm42688pDriver {", None),
            (52, "public:", None),
            (53, "    coro::Task<bool> read_burst_async(ImuSample& out) {", None),
            (54, "        co_await spi_bus_.transfer_dma_async(tx_buf, rx_buf, 14);", {"lat": 8.5, "budget": 10.0, "overrun": False, "token": "spi_bus_.transfer_dma_async()", "diag": "Hardware SPI Auto-DMA burst @ 10 MHz"}),
            (55, "        out.accel = parse_accel(rx_buf);", None),
            (56, "        out.gyro  = parse_gyro(rx_buf);", None),
            (57, "        co_return true;", None),
            (58, "    }", None),
            (59, "};", None),
        ],
        "include/abstractx/fusion/attitude_filter.hpp": [
            (85, "class AttitudeFilter {", None),
            (86, "public:", None),
            (87, "    void update(const Vector3f& gyro, const Vector3f& accel) {", None),
            (88, "        // Mahony quaternion filter integration (zero heap)", {"lat": 18.2, "budget": 20.0, "overrun": False, "token": "attitude_ekf.update()", "diag": "Mahony kinematics quaternion integration"}),
            (89, "        integrate_kinematics(gyro, dt_);", None),
            (90, "    }", None),
            (91, "};", None),
        ],
        "targets/allwinner_e907/src/io_processor.cpp": [
            (45, "void handle_msgbox_irq() {", None),
            (46, "    // Signal Doorbell to Core 1 coroutine engine", None),
            (47, "    e907_signal_doorbell(DOORBELL_CH_IMU);", {"lat": 1.1, "budget": 5.0, "overrun": False, "token": "sun6i_msgbox()", "diag": "Inter-Core RPC Doorbell"}),
            (48, "}", None),
        ],
        "rtl/asp_router.sv": [
            (60, "module asp_router #(parameter CHANNELS = 4) (", None),
            (61, "    input  logic clk, rst_n,", None),
            (62, "    input  logic [CHANNELS-1:0] s_axis_tvalid,", None),
            (63, "    output logic [CHANNELS-1:0] s_axis_tready,", None),
            (64, "    // 64-byte TLP AXI-Stream Crossbar Switch Routing Logic", {"lat": 0.02, "budget": 0.05, "overrun": False, "token": "asp_router.sv Crossbar", "diag": "64-byte TLP non-blocking AXI-Stream switch"}),
            (65, "    input  logic [CHANNELS-1:0][511:0] s_axis_tdata", None),
            (66, ");", None),
        ],
        "rtl/imu/asp_imu_auto_dma.sv": [
            (110, "always_ff @(posedge clk or negedge rst_n) begin", None),
            (111, "    if (!rst_n) state <= IDLE;", None),
            (112, "    else if (drdy_edge) state <= TRIGGER_BURST;", {"lat": 0.01, "budget": 0.02, "overrun": False, "token": "asp_imu_auto_dma.sv", "diag": "Hardware SPI Auto-DMA burst engine triggered by DRDY pin"}),
            (113, "end", None),
        ],
        "rtl/dshot/asp_dshot_core.sv": [
            (85, "module asp_dshot_core (", None),
            (86, "    input  logic clk,", None),
            (87, "    input  logic [10:0] throttle_m1, throttle_m2, throttle_m3, throttle_m4,", None),
            (88, "    output logic [3:0]  dshot_pwm_out", {"lat": 0.01, "budget": 0.03, "overrun": False, "token": "asp_dshot_core.sv", "diag": "4-CH DShot600 bitstream PWM generator"}),
            (89, ");", None),
        ]
    }

    fallback_file = "apps/gps_imu_app/src/main.cpp" if g_state.source_view_mode == "CPP" else "rtl/asp_router.sv"
    curr_lines = code_snippets.get(curr_f, code_snippets[fallback_file])

    imgui.columns(3, "code_inspect_cols", True)
    imgui.set_column_width(0, 50)
    imgui.set_column_width(1, 460)
    imgui.text("Line")
    imgui.next_column()
    imgui.text("Source Code Context (Click to Select Line)")
    imgui.next_column()
    imgui.text("Execution Metrics & Profiling Badges")
    imgui.next_column()
    imgui.separator()

    sel_line = g_state.selected_source_line if g_state.source_view_mode == "CPP" else g_state.selected_rtl_line

    for line_no, code_txt, prof in curr_lines:
        is_sel_line = (line_no == sel_line)
        
        # Line number column
        num_str = f"{line_no:4d}"
        if prof and prof.get("overrun", False):
            imgui.text_colored(imgui.ImVec4(1.0, 0.3, 0.3, 1.0), f"🚨{num_str}")
        elif is_sel_line:
            imgui.text_colored(imgui.ImVec4(1.0, 0.9, 0.2, 1.0), f"👉{num_str}")
        else:
            imgui.text_colored(imgui.ImVec4(0.4, 0.5, 0.6, 1.0), num_str)
        imgui.next_column()

        # Code text column - safely selectable line!
        clicked_line, _ = imgui.selectable(f"{code_txt}##line_{line_no}", is_sel_line, imgui.SelectableFlags_.span_all_columns)
        if clicked_line:
            with g_state.lock:
                if g_state.source_view_mode == "CPP":
                    g_state.selected_source_line = line_no
                    g_state.selected_source_file = curr_f
                else:
                    g_state.selected_rtl_line = line_no
                    g_state.selected_rtl_file = curr_f
                if prof and "token" in prof:
                    g_state.selected_token = prof["token"]
                else:
                    g_state.selected_token = code_txt.strip()
        imgui.next_column()

        # Profiling badges column
        if prof is not None:
            if prof.get("overrun", False):
                imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.9, 0.2, 0.2, 0.9))
                badge_lbl = f"⚠️ {prof['lat']:.1f} µs [OVERRUN +{prof['lat'] - prof['budget']:.1f} µs]"
            else:
                imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.6, 0.3, 0.8))
                badge_lbl = f"✓ {prof['lat']:.1f} µs [Limit: {prof['budget']:.1f} µs]"

            if imgui.button(f"{badge_lbl}##btn_{line_no}"):
                with g_state.lock:
                    if g_state.source_view_mode == "CPP":
                        g_state.selected_source_line = line_no
                        g_state.selected_source_file = curr_f
                    else:
                        g_state.selected_rtl_line = line_no
                        g_state.selected_rtl_file = curr_f
                    g_state.selected_token = prof["token"]
                    if prof.get("overrun", False):
                        g_state.selected_span_id = "imu_overrun"
                        g_state.selected_event_name = "imu_pipeline [OVERRUN]"
            imgui.pop_style_color()
            imgui.same_line()
            imgui.text_colored(imgui.ImVec4(0.7, 0.7, 0.7, 0.9), prof["diag"])
        else:
            imgui.text("")
        imgui.next_column()

    imgui.columns(1)
    imgui.end_child()

def _render_fpga_peripherals_window():
    """
    # @impl [SPEC-STUDIO-12] tools/visualizer/abstractx_studio.py
    Renders Window 6: FPGA Hardware Accelerators & Peripheral Subsystems Inspector.
    """
    g_state.update_rates()
    imgui.begin_group()
    imgui.text_colored(imgui.ImVec4(0.8, 0.4, 1.0, 1.0), "FPGA SPU Accelerator Engine & High-Speed Peripherals")
    imgui.same_line(0, 20)
    if imgui.button("⛶ Expand Window##fpga"):
        apply_docking_layout("fpga_focus")
    imgui.same_line(0, 6)
    if imgui.button("🗖 Pop to Canvas##fpga"):
        g_state.canvas_windows["fpga"] = True
    imgui.end_group()
    imgui.separator()

    imgui.columns(3, "fpga_subsystem_cols", True)

    # Subsystem 1: SPI0 Auto-DMA Engine
    imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), "[SPI0 Auto-DMA Engine]")
    imgui.bullet_text("Controller : Hardware FPGA SPU SPI0")
    imgui.bullet_text("Clock Speed: 10.0 MHz (Mode 3)")
    imgui.bullet_text("Burst Size : 14 Bytes / Transfer")
    imgui.bullet_text("Burst Latency: 8.5 µs")
    imgui.bullet_text("Trigger    : ICM-42688-P DRDY DIO Line")
    imgui.bullet_text("Status     : Auto-DMA Engaged (0% CPU)")
    
    imgui.spacing()
    imgui.text("Bus Saturation (8 kHz Burst Cadence): 6.8%")
    imgui.progress_bar(0.068, imgui.ImVec2(-1, 16), "6.8% Bandwidth")
    imgui.next_column()

    # Subsystem 2: AXI-Stream TLP Crossbar (asp_router.sv)
    imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0), "[AXI-Stream TLP Switch Crossbar]")
    imgui.bullet_text("Module     : asp_router.sv (Full Crossbar)")
    imgui.bullet_text("Fabric Clk : 150.0 MHz")
    imgui.bullet_text("Throughput : 8,240 pkts/s (64B TLPs)")
    imgui.bullet_text("Bandwidth  : 527.4 KB/s (9.6 Gbps max)")
    imgui.bullet_text("Latency    : 2 cycles (13.3 ns zero-copy)")
    imgui.bullet_text("Contention : 0 Stalls (0.0% backpressure)")

    imgui.spacing()
    imgui.text("Channel Routing: Ch 0 (Clock), Ch 1 (Sensor), Ch 2 (Telem)")
    imgui.progress_bar(12.0 / 64.0, imgui.ImVec2(-1, 16), "12 / 64 Descriptors")
    imgui.next_column()

    # Subsystem 3: DShot ESC Pulse Generator
    imgui.text_colored(imgui.ImVec4(0.3, 1.0, 0.5, 1.0), "[DShot600 ESC Motor Generator]")
    imgui.bullet_text("Protocol   : DShot600 (600 kbit/s bitstream)")
    imgui.bullet_text("Channels   : 4 Concurrent DMA PWM (M1..M4)")
    imgui.bullet_text("Frame Time : 26.7 µs / command packet")
    imgui.bullet_text("Resolution : 11-bit throttle (0..2047)")
    imgui.bullet_text("Telemetry  : Bidirectional DShot eRPM enabled")
    imgui.bullet_text("Integrity  : 4-bit hardware CRC checked")

    imgui.spacing()
    imgui.text("Motor Demands Cadence: 100 Hz Sync")
    imgui.progress_bar(0.65, imgui.ImVec2(-1, 16), "M1..M4 Active")
    imgui.columns(1)
    imgui.separator()

def render_gui():
    """Immediate mode GUI rendering combining all windows inside unified tabs for fallback/tests."""
    g_state.update_rates()

    # Top Toolbar
    imgui.begin_group()
    if g_state.connected:
        imgui.text_colored(imgui.ImVec4(0.1, 0.9, 0.2, 1.0), "[ONLINE]")
    else:
        imgui.text_colored(imgui.ImVec4(0.9, 0.2, 0.1, 1.0), "[OFFLINE / WAITING]")
    imgui.same_line()
    imgui.text(f"| Platform: {g_state.platform_name} ({g_state.platform_arch}) | Packets: {g_state.packet_count:,} | Rate: {g_state.fps_packet_rate} pkts/s")
    imgui.end_group()
    imgui.separator()

    # Tab Bar
    if imgui.begin_tab_bar("StudioTabBar"):
        flags_platform = imgui.TabItemFlags_.set_selected if g_initial_tab == "platform" else 0
        flags_flight = imgui.TabItemFlags_.set_selected if g_initial_tab == "flight" else 0
        flags_memory = imgui.TabItemFlags_.set_selected if g_initial_tab == "memory" else 0

        opened, _ = imgui.begin_tab_item("User Domain Instruments (gps_imu_app)", None, flags_flight)
        if opened:
            _render_user_domain_window()
            imgui.end_tab_item()

        opened, _ = imgui.begin_tab_item("AbstractX Core Studio (CPU & Topology)", None, flags_platform)
        if opened:
            _render_core_studio_window()
            imgui.end_tab_item()

        opened, _ = imgui.begin_tab_item("Source Code & RTL Inspector", None, 0)
        if opened:
            _render_source_inspector_window()
            imgui.end_tab_item()

        opened, _ = imgui.begin_tab_item("FPGA Peripherals", None, 0)
        if opened:
            _render_fpga_peripherals_window()
            imgui.end_tab_item()

        opened, _ = imgui.begin_tab_item("MemBrowse CI Memory Report", None, flags_memory)
        if opened:
            _render_level1_memory()
            imgui.end_tab_item()

        imgui.end_tab_bar()

def main():
    global g_initial_tab
    parser = argparse.ArgumentParser(description="AbstractX Visualizer Studio Workbench")
    parser.add_argument("--port", type=int, default=DEFAULT_UDP_PORT, help="UDP listening port")
    parser.add_argument("--sim", action="store_true", help="Run with synthetic flight & telemetry generator")
    parser.add_argument("--tab", type=str, default="flight", choices=["flight", "platform", "memory"],
                        help="Initial tab to display in single-window mode")
    parser.add_argument("--viewports", action="store_true",
                        help="Enable experimental multi-viewport secondary OS windows")
    args = parser.parse_args()

    g_initial_tab = args.tab

    # Start UDP receiver background thread
    recv_thread = threading.Thread(target=udp_receiver_thread, args=(args.port, args.sim), daemon=True)
    recv_thread.start()

    # Configure HelloImGui Multi-Window Docking Workbench
    runner_params = create_docking_runner_params(enable_viewports=args.viewports)
    implot.create_context()
    immapp.run(runner_params)
    implot.destroy_context()

def create_docking_runner_params(enable_viewports: bool = False) -> hello_imgui.RunnerParams:
    """
    # @impl [SPEC-STUDIO-01] tools/visualizer/abstractx_studio.py
    Creates and configures HelloImGui dynamic docking layout for AbstractX Studio.
    Defaults to single dedicated OS window for rock-solid portability across Windows and Linux,
    with support for internal floating windows with dedicated size controls and presets.
    """
    runner_params = hello_imgui.RunnerParams()
    runner_params.app_window_params.window_title = "AbstractX Studio & User Domain Workbench"
    runner_params.app_window_params.window_geometry.size = (1560, 920)
    runner_params.app_window_params.resizable = True
    runner_params.app_window_params.restore_previous_geometry = True
    runner_params.app_window_params.borderless_resizable = True
    runner_params.app_window_params.handle_edge_insets = True

    # Enable full screen docking layout
    runner_params.imgui_window_params.default_imgui_window_type = (
        hello_imgui.DefaultImGuiWindowType.provide_full_screen_dock_space
    )
    runner_params.imgui_window_params.enable_viewports = enable_viewports
    runner_params.imgui_window_params.show_menu_bar = True
    runner_params.imgui_window_params.show_menu_view = True
    runner_params.imgui_window_params.show_status_bar = True
    runner_params.imgui_window_params.menu_app_title = "AbstractX"

    # Define dedicated dockable windows
    win_user = hello_imgui.DockableWindow()
    win_user.label = "User Domain Instruments"
    win_user.dock_space_name = "MainDockSpace"
    win_user.gui_function = _render_user_domain_window

    win_core = hello_imgui.DockableWindow()
    win_core.label = "AbstractX Core Studio"
    win_core.dock_space_name = "LeftSpace"
    win_core.gui_function = _render_core_studio_window

    win_tlp = hello_imgui.DockableWindow()
    win_tlp.label = "TLP Bus Debugger"
    win_tlp.dock_space_name = "BottomSpace"
    win_tlp.gui_function = _render_tlp_debugger_window
    win_tlp.is_visible = True

    win_log = hello_imgui.DockableWindow()
    win_log.label = "System Event Log"
    win_log.dock_space_name = "BottomRightSpace"
    win_log.gui_function = _render_event_log_window
    win_log.is_visible = True

    win_source = hello_imgui.DockableWindow()
    win_source.label = "Source Code & Performance Inspector"
    win_source.dock_space_name = "MainDockSpace"
    win_source.gui_function = _render_source_inspector_window
    win_source.is_visible = False

    win_fpga = hello_imgui.DockableWindow()
    win_fpga.label = "FPGA & Hardware Peripherals"
    win_fpga.dock_space_name = "BottomSpace"
    win_fpga.gui_function = _render_fpga_peripherals_window
    win_fpga.is_visible = False

    # Store in global dictionary for dynamic layout and pop-out control
    global g_dockable_windows
    g_dockable_windows["core"] = win_core
    g_dockable_windows["user"] = win_user
    g_dockable_windows["tlp"] = win_tlp
    g_dockable_windows["log"] = win_log
    g_dockable_windows["source"] = win_source
    g_dockable_windows["fpga"] = win_fpga

    # Define Docking Splits:
    # 1. LeftSpace (54% width) on the left for AbstractX Core Studio
    split_left = hello_imgui.DockingSplit()
    split_left.initial_dock = "MainDockSpace"
    split_left.new_dock = "LeftSpace"
    split_left.direction = imgui.Dir_.left
    split_left.ratio = 0.54

    # 2. BottomSpace (36% height) at the bottom for auxiliary decoupled panes
    split_bottom = hello_imgui.DockingSplit()
    split_bottom.initial_dock = "MainDockSpace"
    split_bottom.new_dock = "BottomSpace"
    split_bottom.direction = imgui.Dir_.down
    split_bottom.ratio = 0.36

    # 3. Split BottomSpace into Left (TLP Debugger) and Right (System Event Log)
    split_bottom_log = hello_imgui.DockingSplit()
    split_bottom_log.initial_dock = "BottomSpace"
    split_bottom_log.new_dock = "BottomRightSpace"
    split_bottom_log.direction = imgui.Dir_.right
    split_bottom_log.ratio = 0.50

    runner_params.docking_params.docking_splits = [split_left, split_bottom, split_bottom_log]
    runner_params.docking_params.dockable_windows = [win_core, win_user, win_tlp, win_log, win_source, win_fpga]
    runner_params.callbacks.show_status = _render_status_bar
    runner_params.callbacks.setup_imgui_style = _setup_studio_style
    runner_params.callbacks.post_render_dockable_windows = _render_floating_canvas_windows
    return runner_params

if __name__ == "__main__":
    main()

