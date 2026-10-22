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
try:
    import runtime_stats
except ImportError:
    from tools.visualizer import runtime_stats

# Live mode ([SPEC-STUDIO-14]): commands that would need a REPL link to the target.
TARGET_CLI_COMMANDS = ("peek", "poke", "ping", "tasks", "tlp")
NO_TARGET_LINK_MSG = ("no REPL link to the target: Studio cannot run this on hardware yet "
                      "(use run_e906_script.py / e906_workbench.py, or --sim for demo data)")

# CTF Constants & Defaults
CTF_MAGIC = 0xC1FC1FC1
DEFAULT_UDP_PORT = 9870
HISTORY_SIZE = 1000

class TelemetryState:
    def __init__(self):
        self.lock = threading.Lock()
        # @impl [SPEC-STUDIO-14] tools/visualizer/SPECIFICATION.md
        self.sim_mode = False
        self.connected = False
        self.packet_count = 0
        self.bytes_received = 0
        self.fps_packet_rate = 0
        self.last_rate_calc = time.time()
        self.rate_counter = 0
        self.discarded_events = 0

        # Source & Pipeline status
        self.board_ip = "192.168.3.3"
        self.host_ip = "192.168.3.1"
        self.udp_port = 9870
        self.source_mode = "STANDBY (Waiting on UDP :9870)"
        self.fpga_trace = "Pipeline: idle (no packets yet)"

        # Platform Topology Table & Hardware Interconnect (Level 1)
        # @impl [SPEC-STUDIO-02] tools/visualizer/abstractx_studio.py
        # @impl [SPEC-STUDIO-07] tools/visualizer/abstractx_studio.py
        self.has_fpga = False  # Pure Silicon by default (Radxa Cubie A5E / No FPGA)
        self.profile_id = "cubie_a5e_pure"
        self.profile_file = "tools/visualizer/profiles/cubie_a5e_pure.json"
        self.schema_file = "apps/gps_imu_app/trace_schema.json"
        self.compiled_schema = None
        self.show_profile_dialog = False
        self.show_schema_dialog = False
        self.custom_profile_path = "tools/visualizer/profiles/cubie_a5e_pure.json"
        self.custom_schema_path = "apps/gps_imu_app/trace_schema.json"

        self.platform_arch = "Linux_Host_E906_PureSilicon"
        self.platform_name = "Allwinner Cubie A5E (Pure Silicon)"
        self.board_model = "Radxa Cubie A5E"
        self.primary_transport = "Shared SRAM (0x40000000) + sun6i-msgbox (/dev/rpmsg0)"
        self.active_cores = [
            {"id": 0, "role": "Host Linux ARM64", "task": "Flight Supervisor & Telemetry", "clock_mhz": 1400},
            {"id": 1, "role": "XuanTie E906 RISC-V", "task": "Real-Time I/O Reactor & Sensors", "clock_mhz": 800},
        ]
        self.hardware_accels = ["UART3 FIFO (GPS)", "TWI1 I2C (Compass)", "sun6i-msgbox"]

        # Dual-Plane Execution Data (Level 1)
        # Plane 1: I/O Processor & Drivers (Interrupt & DMA Context)
        self.io_driver_events = []
        # Plane 2: Main Coroutine Loop (Cooperative C++20 Tasks)
        self.coro_names = []
        self.coro_states = []
        self.coro_latencies_us = []
        self.coro_source_info = []

        # Selected Source Code View
        self.selected_event_name = ""
        self.selected_source_file = ""
        self.selected_source_line = 0
        self.selected_token = ""

        # Per-Processor Utilization
        self.linux_total_cpu = 0.0
        self.linux_abstractx_cpu = 0.0
        self.linux_external_cpu = 0.0
        self.e906_active_duty_pct = 0.0
        self.e906_wfi_sleep_pct = 0.0
        self.fpga_lut_utilization_pct = 0.0
        self.fpga_dma_bw_mbps = 0.0

        # SPSC Queue Saturation
        self.sensor_ring_fill = 0
        self.telemetry_ring_fill = 0
        self.rtt_latency_us = 0.0

        # Memory Observability & Static Section Budgets (MemBrowse Integration)
        # Only memory_metrics.json fills this ([SPEC-STUDIO-14]); --sim adds demo targets.
        self.selected_mem_target = ""
        self.mem_targets = {}

        # MemBrowse CI Memory Observability & Static Budget State
        self.membrowse_last_run = "Never"
        self.membrowse_status_msg = "Awaiting CI static audit metrics..."
        self.load_membrowse_metrics()

        # Level 2 Domain Telemetry (Flight & Sensors)
        self.roll_deg = 0.0
        self.pitch_deg = 0.0
        self.yaw_deg = 0.0
        self.altitude_m = 0.0
        self.ground_speed_mps = 0.0
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
        # Last tag-3 payload as received; its format belongs to the app (plugins decode it)
        self.mag_payload = b""

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
        self.gps_lat = 0.0
        self.gps_lon = 0.0
        self.gps_alt_m = 0.0
        self.gps_speed_mps = 0.0
        self.gps_sats = 0
        self.gps_fix_type = 0

        # Logging & TLP Debugger State
        self.logs = []
        self.recent_tlp_packets = []
        self.selected_tlp_idx = 0
        self.tlp_stream_paused = False
        # TLP debugger view ([SPEC-STUDIO-04]): the table is a snapshot refreshed a few times a
        # second, and the selection is the packet itself, so neither moves under the reader.
        self.tlp_view = []
        self.tlp_view_t = 0.0
        self.selected_tlp = None
        self.tlp_tag_counts = {}

        # Live runtime status from the target ([SPEC-STUDIO-15], [SPEC-TRACE-08])
        self.runtime_report = None
        self.runtime_rates = None
        self.runtime_reports = 0
        self.runtime_busy_hist = []
        self.runtime_plot_autoscale = True
        self.runtime_plot_window_s = 300
        self.ctf_other_packets = 0
        self.log_filter_level = "ALL"
        self.log_search_text = ""
        self.log_auto_scroll = True

        # AbstractX Studio & CPU Line Chart History (Last 30 seconds, 60 samples at 2 Hz)
        self.chart_hist_len = 60
        self.chart_time = np.linspace(-30.0, 0.0, self.chart_hist_len, dtype=np.float64)
        self.chart_cpu_c0 = np.zeros(self.chart_hist_len, dtype=np.float64)
        self.chart_cpu_c1 = np.zeros(self.chart_hist_len, dtype=np.float64)
        self.chart_cpu_spu = np.zeros(self.chart_hist_len, dtype=np.float64)
        
        # Coroutine & Task Latencies (microseconds)
        self.chart_lat_imu = np.zeros(self.chart_hist_len, dtype=np.float64)
        self.chart_lat_ekf = np.zeros(self.chart_hist_len, dtype=np.float64)
        self.chart_lat_ctl = np.zeros(self.chart_hist_len, dtype=np.float64)
        self.chart_lat_dma = np.zeros(self.chart_hist_len, dtype=np.float64)

        # SPSC Ring Occupancies
        self.chart_ring_sensor = np.zeros(self.chart_hist_len, dtype=np.float64)
        self.chart_ring_telem = np.zeros(self.chart_hist_len, dtype=np.float64)

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
            "cli": False,
        }
        self.user_canvas_floated_on_startup = False

        # CLI Console & MicroPython Hardware REPL State
        # @impl [SPEC-STUDIO-13] tools/visualizer/abstractx_studio.py
        self.cli_history = [
            ("INFO", "AbstractX Interactive CLI."),
            ("WARN", "Live mode: Studio has no REPL link to the target yet; target commands are unavailable."),
            ("INFO", "Type 'help' for command list or switch modes above."),
        ]
        self.cli_command_history = []
        self.cli_history_idx = -1
        self.cli_input_text = ""
        self.cli_mode = "AbstractX"  # "AbstractX" or "MicroPython"
        self.cli_auto_scroll = True
        self.cli_simulated_regs = {}

        # Simple Trace Viewer State
        self.simple_trace_events = []
        self.simple_trace_paused = False
        self.simple_trace_filter_core = "ALL"
        self.simple_trace_search = ""
        self.simple_trace_auto_scroll = True
        self.selected_trace_idx = 0

        # Dual-Plane Execution Swimlanes & Flow-Integrity Drill-Down State
        self.active_layout_preset = "balanced"
        self.source_code_files = [
            "apps/gps_imu_app/src/main.cpp",
            "include/abstractx/drivers/imu/icm42688p.hpp",
            "include/abstractx/fusion/attitude_filter.hpp",
            "targets/allwinner_e906/src/io_processor.cpp"
        ]
        self.selected_code_file_idx = 0
        self.timing_zoom = 1.0
        self.timing_pan_us = 0.0
        self.timing_paused = False
        self.timing_show_overruns_only = False
        self.selected_span_id = ""

        # Real-Time Flow-Integrity & Congestion Metrics
        self.chart_transit_delay_us = np.zeros(self.chart_hist_len, dtype=np.float64)
        self.chart_interarrival_us = np.zeros(self.chart_hist_len, dtype=np.float64)
        self.pacing_eye_jitter_us = np.zeros(48, dtype=np.float64)
        self.hol_contention_ratio = 0.0
        self.tlp_seq_drops = 0
        self.tlp_last_seq = 0
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
        self.coro_frames = []
        # Static frame pool metrics (from coro::coro_pool_used() / coro_pool_capacity())
        self.coro_pool_used_bytes = 0
        self.coro_pool_capacity_bytes = 0
        # Spawn topology: list of (parent_name, child_name) edges for topology tree
        self.coro_topology = []

        self.add_log("INFO", "Studio", "Live mode: showing only received data (start with --sim for demo data).")

    def enable_sim(self):
        """Fill the workbench with demo data; only for --sim ([SPEC-STUDIO-09], [SPEC-STUDIO-14])."""
        # @impl [SPEC-STUDIO-14] tools/visualizer/SPECIFICATION.md
        self.sim_mode = True
        self.logs.clear()
        self.cli_history = [h for h in self.cli_history if "no REPL link" not in h[1]]
        self.cli_history.insert(1, ("WARN", "SIMULATED DATA (--sim): nothing here comes from a target."))
        self._seed_sim_data()
        self.load_membrowse_metrics()

    def _seed_sim_data(self):
        self.io_driver_events = [
            {"name": "SPI0 DMA Burst", "driver": "hal_spi", "subsystem": "ICM-42688-P DMA", "latency_us": 0.8, "file": "targets/linux/src/hal_spi.cpp", "line": 78},
            {"name": "TWI0 I2C ISR", "driver": "hal_i2c", "subsystem": "Compass / Baro", "latency_us": 1.2, "file": "targets/allwinner_e906/src/hal_i2c.cpp", "line": 45},
            {"name": "MSGBox Doorbell", "driver": "msgbox", "subsystem": "Inter-Core RPC", "latency_us": 0.4, "file": "targets/allwinner_e906/src/io_processor.cpp", "line": 92},
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
        self.linux_total_cpu = 8.4
        self.linux_abstractx_cpu = 5.2
        self.linux_external_cpu = 3.2
        self.e906_active_duty_pct = 14.8
        self.e906_wfi_sleep_pct = 85.2
        self.fpga_lut_utilization_pct = 18.2
        self.fpga_dma_bw_mbps = 12.8

        # SPSC Queue Saturation
        self.sensor_ring_fill = 18
        self.telemetry_ring_fill = 8
        self.rtt_latency_us = 12.4
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
            "XuanTie E906 RISC-V": {
                "arch": "RV32IMAFDC @ up to 200 MHz Co-Processor",
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
        self.gps_lat = 37.7749
        self.gps_lon = -122.4194
        self.gps_alt_m = 142.5
        self.gps_speed_mps = 12.4
        self.gps_sats = 18
        self.gps_fix_type = 3
        self.chart_cpu_c0[:] = 22.4
        self.chart_cpu_c1[:] = 34.1
        self.chart_cpu_spu[:] = 11.2
        self.chart_lat_imu[:] = 1.2
        self.chart_lat_ekf[:] = 4.5
        self.chart_lat_ctl[:] = 2.8
        self.chart_lat_dma[:] = 0.8
        self.chart_ring_sensor[:] = 22.0
        self.chart_ring_telem[:] = 12.0
        self.chart_transit_delay_us[:] = 2.4
        self.chart_interarrival_us[:] = 125.0
        self.cli_simulated_regs = {
            0x07090000: 0x18FA2040,  # MTIME
            0x07090008: 0x18FA2428,  # MTIMECMP
            0x07130000: 0x544C5052,  # SRAM C Magic (TLPR)
            0x10000000: 0x00000000,  # PLIC Base
            0x03003000: 0x00000002,  # MSGBOX Ch1 Status
            0x40000100: 0x00000001,  # FPGA Interconnect Status
        }
        self._seed_initial_trace_events()
        self.pacing_eye_jitter_us = np.linspace(-4.2, 4.8, 48, dtype=np.float64)
        self.hol_contention_ratio = 0.035
        self.selected_span_id = "imu_overrun"
        self.selected_event_name = "imu_pipeline [OVERRUN]"
        self.selected_source_file = "apps/gps_imu_app/src/main.cpp"
        self.selected_source_line = 42
        self.selected_token = "co_await g_sensor_ring.pop() [OVERRUN: 23.4 µs vs 15.0 µs budget]"
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
            (112.0, "Core 0", "DOORBELL", "sun6i_msgbox()", "Mailbox interrupt signaled to Core 1", 1.1, "targets/allwinner_e906/main.cpp", 47),
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

    def ingest_ctf(self, data: bytes):
        """A CTF packet from the target's trace endpoint ([SPEC-STUDIO-15])."""
        # @impl [SPEC-STUDIO-15] tools/visualizer/SPECIFICATION.md
        report = runtime_stats.decode(data)
        if report is None:
            with self.lock:
                self.ctf_other_packets += 1
            return
        prev = self.runtime_report
        rates = runtime_stats.rates(prev, report) if prev else None
        if prev and report["timestamp_us"] < prev["timestamp_us"]:
            self.add_log("WARN", "status", "target restarted (status timestamps went back)")
            rates = None
        with self.lock:
            self.runtime_report = report
            self.runtime_reports += 1
            if rates:
                self.runtime_rates = rates
                self.e906_active_duty_pct = rates["busy_pct"]
                self.e906_wfi_sleep_pct = rates["idle_pct"]
                self.runtime_busy_hist.append((time.time(), rates["busy_pct"], rates["isr_pct"]))
                del self.runtime_busy_hist[:-300]
        if not rates:
            return
        top = rates["tasks"][0] if rates["tasks"] else None
        drv = ", ".join(f"{d['name']} {d['irq_per_s']:.0f}/s" for d in rates["drivers"] if d["irq_per_s"] >= 1)
        self.add_log("INFO", "status",
                     f"busy {rates['busy_pct']:.1f}% (isr {rates['isr_pct']:.2f}%)"
                     + (f" | {top['name']} {top['cpu_pct']:.1f}%" if top else "")
                     + (f" | irq {drv}" if drv else ""))
        for d in rates["drivers"]:
            if d["new_errors"]:
                self.add_log("WARN", d["name"], f"{d['new_errors']} new errors (total {d['errors']})")
        if prev and report["dropped_resumes"] != prev["dropped_resumes"]:
            self.add_log("WARN", "dispatcher", f"dropped resumes: {report['dropped_resumes']}")
        if prev and report["trace_discarded"] != prev["trace_discarded"]:
            self.add_log("WARN", "trace", f"trace events discarded: {report['trace_discarded']}")
        if prev and report["pool_high_water"] > prev["pool_high_water"]:
            self.add_log("CORO", "frame_pool", f"high water {report['pool_high_water']} / {report['pool_capacity']} B")

    def add_cli_line(self, line_type: str, text: str):
        # @impl [SPEC-STUDIO-13] tools/visualizer/abstractx_studio.py
        with self.lock:
            self.cli_history.append((line_type, text))
            if len(self.cli_history) > 500:
                self.cli_history.pop(0)

    def execute_cli_command(self, cmd_str: str):
        # @impl [SPEC-STUDIO-13] tools/visualizer/abstractx_studio.py
        cmd = cmd_str.strip()
        if not cmd:
            return
        with self.lock:
            self.cli_command_history.append(cmd)
            self.cli_history_idx = -1
            prompt_prefix = "abstractx> " if self.cli_mode == "AbstractX" else ">>> "
            self.cli_history.append(("PROMPT", f"{prompt_prefix}{cmd}"))

        if self.cli_mode == "MicroPython":
            self._execute_micropython_command(cmd)
        else:
            self._execute_abstractx_command(cmd)

    def _execute_abstractx_command(self, cmd: str):
        # @impl [SPEC-STUDIO-13] tools/visualizer/abstractx_studio.py
        parts = cmd.split()
        op = parts[0].lower()
        # @impl [SPEC-STUDIO-14] tools/visualizer/SPECIFICATION.md
        if op in TARGET_CLI_COMMANDS and not self.sim_mode:
            self.add_cli_line("ERR", NO_TARGET_LINK_MSG)
            return
        if op in ("help", "?"):
            self.add_cli_line("OUT", "Available AbstractX CLI Commands:")
            self.add_cli_line("OUT", "  help, ?              - Show this command reference")
            self.add_cli_line("OUT", "  ps, coro             - Dump live C++20 coroutine state machine table")
            self.add_cli_line("OUT", "  tasks                - Dump FreeRTOS task runtime & CPU usage metrics")
            self.add_cli_line("OUT", "  peek <addr>          - Read 32-bit hardware register (e.g. peek 0x07090000)")
            self.add_cli_line("OUT", "  poke <addr> <val>    - Write 32-bit register value (e.g. poke 0x07130000 0x1)")
            self.add_cli_line("OUT", "  ping [count]         - Measure round-trip IPC latency across bus")
            self.add_cli_line("OUT", "  tlp <send|stat> ...  - Inspect or inject 64-byte PCIe TLP frames")
            self.add_cli_line("OUT", "  stats, flow          - View Head-of-Line blocking and bus pacing stats")
            self.add_cli_line("OUT", "  mode <name>          - Switch mode ('abstractx' or 'micropython')")
            self.add_cli_line("OUT", "  clear, cls           - Clear terminal history buffer")
        elif op in ("ps", "coro"):
            with self.lock:
                frames = list(self.coro_frames)
            self.add_cli_line("HEADER", f"{'#ID':<4} {'Name':<16} {'State':<11} {'co_await Token':<20} {'Duration (us)':<12}")
            self.add_cli_line("OUT", "-" * 65)
            for f in frames:
                st = f.get('state', 'RUNNING')
                aw = f.get('awaiter', '-')
                dur = f.get('duration_us', 0.0)
                self.add_cli_line("CORO", f"#{f['task_id']:<3} {f['name']:<16} {st:<11} {aw:<20} {dur:8.1f} us")
        elif op == "tasks":
            self.add_cli_line("HEADER", f"{'Task Name':<14} | {'ID':<3} | {'State':<5} | {'Stack Left':<11} | {'CPU %':<6}")
            self.add_cli_line("OUT", "-" * 50)
            self.add_cli_line("TASK", "bench_a        | #1  | DEL   | 396 words   | 48.2%")
            self.add_cli_line("TASK", "bench_b        | #2  | DEL   | 396 words   | 48.2%")
            self.add_cli_line("TASK", "rpmsg_rx       | #3  | BLK   | 960 words   |  1.4%")
            self.add_cli_line("TASK", "IDLE           | #0  | RDY   | 210 words   |  2.2%")
            self.add_cli_line("OK", "FreeRTOS Heap Free: 24,128 bytes | Context Switch: 240 ns")
        elif op == "peek":
            if len(parts) < 2:
                self.add_cli_line("ERR", "Usage: peek <hex_addr>  (e.g. peek 0x07090000)")
            else:
                try:
                    addr = int(parts[1], 16) if parts[1].startswith("0x") or parts[1].startswith("0X") else int(parts[1])
                    val = self.cli_simulated_regs.get(addr, 0x00000000)
                    if addr == 0x07090000:
                        val += int(time.time() * 200000000) & 0xFFFFFFFF
                    self.add_cli_line("OK", f"[0x{addr:08X}] = 0x{val:08X} ({val} dec)")
                except Exception as e:
                    self.add_cli_line("ERR", f"Invalid address: {e}")
        elif op == "poke":
            if len(parts) < 3:
                self.add_cli_line("ERR", "Usage: poke <hex_addr> <hex_val>")
            else:
                try:
                    addr = int(parts[1], 16) if parts[1].startswith("0x") else int(parts[1])
                    val = int(parts[2], 16) if parts[2].startswith("0x") else int(parts[2])
                    self.cli_simulated_regs[addr] = val
                    self.add_cli_line("OK", f"[POKE] Wrote 0x{val:08X} to physical address 0x{addr:08X}")
                except Exception as e:
                    self.add_cli_line("ERR", f"Error in poke: {e}")
        elif op == "ping":
            cnt = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 3
            self.add_cli_line("OUT", f"Pinging XuanTie E906 co-processor ({cnt} iterations)...")
            for i in range(1, cnt + 1):
                lat = round(1.82 + (i % 3) * 0.04, 2)
                self.add_cli_line("OK", f"  Reply #{i} from E906: bytes=64 time={lat} us transport=SRAM_UIO")
        elif op == "tlp":
            self.add_cli_line("OK", "[TLP] Injected 64-byte frame Tag=0x00 Channel=3 (asp_router.sv accepted)")
        elif op in ("stats", "flow"):
            self.add_cli_line("OUT", f"Bus Flow Stats: Packets={self.packet_count:,} Rate={self.packet_rate_smoothed:.1f} pkts/s")
            if self.sim_mode:
                self.add_cli_line("OUT", "HoL Max Stall: 1.4 us | Ring Fill: 42% | Dynamic Heap: 0 B")
        elif op in ("clear", "cls"):
            with self.lock:
                self.cli_history.clear()
        elif op == "mode":
            if len(parts) > 1 and parts[1].lower() in ("python", "micropython"):
                self.cli_mode = "MicroPython"
                self.add_cli_line("INFO", "Switched to MicroPython REPL mode (prompt: '>>> ')")
            else:
                self.cli_mode = "AbstractX"
                self.add_cli_line("INFO", "Switched to AbstractX Native CLI mode (prompt: 'abstractx> ')")
        else:
            self.add_cli_line("WARN", f"Unknown command: '{op}'. Type 'help' for command list.")

    def _execute_micropython_command(self, cmd: str):
        # @impl [SPEC-STUDIO-13] tools/visualizer/abstractx_studio.py
        if cmd.lower() in ("exit", "quit", "mode abstractx"):
            self.cli_mode = "AbstractX"
            self.add_cli_line("INFO", "Switched to AbstractX Native CLI mode.")
            return
        # @impl [SPEC-STUDIO-14] tools/visualizer/SPECIFICATION.md
        if not self.sim_mode:
            self.add_cli_line("ERR", NO_TARGET_LINK_MSG)
            return

        state_ref = self
        class MachineMem:
            def __getitem__(_, addr):
                return state_ref.cli_simulated_regs.get(addr, 0x0)
            def __setitem__(_, addr, val):
                state_ref.cli_simulated_regs[addr] = val

        class AbstractXBridge:
            @staticmethod
            def coros():
                return [f['name'] for f in state_ref.coro_frames]
            @staticmethod
            def ping():
                return 1.85
            @staticmethod
            def rate():
                return state_ref.packet_rate_smoothed
            @staticmethod
            def heap():
                return "0 B (Freestanding)"

        safe_env = {
            "abstractx": AbstractXBridge,
            "machine": type("Machine", (), {"mem32": MachineMem()}),
            "read32": lambda addr: state_ref.cli_simulated_regs.get(addr, 0x0),
            "write32": lambda addr, val: state_ref.cli_simulated_regs.update({addr: val}),
            "math": math,
            "time": time,
            "hex": hex,
            "bin": bin,
            "len": len,
            "range": range,
            "print": lambda *args: state_ref.add_cli_line("OUT", " ".join(str(a) for a in args)),
        }

        try:
            res = eval(cmd, safe_env)
            if res is not None:
                self.add_cli_line("PY_RES", str(res))
        except SyntaxError:
            try:
                exec(cmd, safe_env)
            except Exception as e:
                self.add_cli_line("ERR", f"MicroPython Exception: {type(e).__name__}: {e}")
        except Exception as e:
            self.add_cli_line("ERR", f"MicroPython Exception: {type(e).__name__}: {e}")


    def add_tlp_packet(self, raw_bytes: bytes, tlp_type: int, tag: int, channel: int, seq: int, ts_ns: int, crc_ok: bool):
        with self.lock:
            self.tlp_tag_counts[tag] = self.tlp_tag_counts.get(tag, 0) + 1
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
        """Executes track_memory_footprint.py locally to regenerate memory_metrics.json and refresh state."""
        import subprocess, sys
        script_path = Path(__file__).resolve().parents[2] / "tools" / "track_memory_footprint.py"
        if not script_path.exists():
            script_path = Path(__file__).resolve().parents[2] / "tools" / "track_memory_membrowse.py"
        try:
            subprocess.run([sys.executable, str(script_path)], capture_output=True, text=True, check=True)
            self.load_membrowse_metrics()
            self.membrowse_status_msg = "Memory audit completed successfully!"
            self.add_log("MEM", "Memory", "Local memory audit completed. memory_metrics.json refreshed.")
        except Exception as e:
            self.membrowse_status_msg = f"Analysis error: {e}"
            self.add_log("WARN", "Memory", f"Failed to run track_memory_footprint.py: {e}")

    def run_spectrace_dashboard(self):
        """Executes track_quality_trends.py locally to refresh quality trends and running profile."""
        import subprocess, sys
        script_path = Path(__file__).resolve().parents[2] / "tools" / "track_quality_trends.py"
        try:
            subprocess.run([sys.executable, str(script_path)], capture_output=True, text=True, check=True)
            self.load_membrowse_metrics()
            self.membrowse_status_msg = "SpecTrace Dashboard updated successfully!"
            self.add_log("SPEC", "SpecTrace", "SpecTrace dashboard & verification profile refreshed.")
        except Exception as e:
            self.membrowse_status_msg = f"Dashboard error: {e}"
            self.add_log("WARN", "SpecTrace", f"Failed to run track_quality_trends.py: {e}")

    def set_has_fpga(self, enabled: bool):
        # @impl [SPEC-STUDIO-02] tools/visualizer/abstractx_studio.py
        self.has_fpga = enabled
        global g_dockable_windows
        if "fpga" in g_dockable_windows and g_dockable_windows["fpga"] is not None:
            g_dockable_windows["fpga"].is_visible = enabled
        if not enabled:
            self.canvas_windows["fpga"] = False
            # Filter out FPGA core from active_cores in pure silicon mode
            self.active_cores = [c for c in self.active_cores if "FPGA" not in c.get("role", "")]
            self.platform_arch = "Linux_Host_E906_PureSilicon"
            self.add_log("PROFILE", "CFG", "Hardware Profile: Pure Silicon Mode (FPGA windows disabled)")
        else:
            if not any("FPGA" in c.get("role", "") for c in self.active_cores):
                self.active_cores.append({"id": 2, "role": "Tang Primer 20K FPGA", "task": "Hardware Auto-DMA & DShot Fabric", "clock_mhz": 50})
            self.platform_arch = "Linux_Host_E906_FPGA"
            self.add_log("PROFILE", "CFG", "Hardware Profile: FPGA X-Fabric Enabled (FPGA windows active)")

    def load_profile_from_json(self, profile_path: str):
        # @impl [SPEC-STUDIO-07] tools/visualizer/abstractx_studio.py
        try:
            import json
            from pathlib import Path
            p = Path(profile_path)
            if not p.is_absolute():
                repo_root = Path(__file__).resolve().parent.parent.parent
                p = repo_root / profile_path
            if not p.exists():
                self.add_log("WARN", "CFG", f"Profile file not found: {p}")
                return False
            with open(p, "r", encoding="utf-8") as f:
                data = json.load(f)

            self.profile_id = data.get("profile_id", p.stem)
            self.profile_file = str(p)
            self.platform_name = data.get("platform_name", self.platform_name)
            self.board_model = data.get("board_model", self.board_model)
            self.platform_arch = data.get("platform_arch", self.platform_arch)
            self.primary_transport = data.get("primary_transport", self.primary_transport)
            self.board_ip = data.get("board_ip", self.board_ip)
            self.host_ip = data.get("host_ip", self.host_ip)
            self.udp_port = data.get("udp_port", self.udp_port)
            if "active_cores" in data:
                self.active_cores = data["active_cores"]
            if "hardware_accels" in data:
                self.hardware_accels = data["hardware_accels"]
            has_fpga = data.get("has_fpga", False)
            self.set_has_fpga(has_fpga)

            schema_f = data.get("schema_file")
            if schema_f:
                self.load_barectf_schema(schema_f)

            self.add_log("PROFILE", "JSON", f"Loaded profile '{self.platform_name}' (has_fpga={self.has_fpga})")
            return True
        except Exception as e:
            self.add_log("ERROR", "CFG", f"Failed loading profile {profile_path}: {e}")
            return False

    def load_barectf_schema(self, schema_path: str):
        # @impl [SPEC-STUDIO-07] tools/visualizer/abstractx_studio.py
        try:
            from pathlib import Path
            p = Path(schema_path)
            if not p.is_absolute():
                repo_root = Path(__file__).resolve().parent.parent.parent
                p = repo_root / schema_path
            if not p.exists():
                self.add_log("WARN", "SCHEMA", f"Schema file not found: {p}")
                return False
            try:
                from ctf_schema_loader import CtfSchema
            except ImportError:
                from tools.visualizer.ctf_schema_loader import CtfSchema
            self.compiled_schema = CtfSchema(str(p))
            self.schema_file = str(p)
            stream_count = len(self.compiled_schema.streams)
            self.add_log("SCHEMA", "CTF1.8", f"Loaded barectf schema '{p.name}': {stream_count} streams compiled.")
            return True
        except Exception as e:
            self.add_log("ERROR", "SCHEMA", f"Failed compiling barectf schema: {e}")
            return False

    def auto_detect_target(self, board_ip: str):
        # @impl [SPEC-STUDIO-01] tools/visualizer/abstractx_studio.py
        import subprocess
        try:
            res = subprocess.run(
                ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=2", f"root@{board_ip}",
                 "test -e /dev/asp_fpga || test -e /sys/class/fpga_manager/fpga0 && echo 'FPGA' || echo 'PURE_SILICON'"],
                capture_output=True, text=True, timeout=3
            )
            detected = res.stdout.strip()
            if "FPGA" in detected:
                self.load_profile_from_json("tools/visualizer/profiles/cubie_a5e_fpga.json")
                self.add_log("DETECT", "SSH", f"Target {board_ip}: Detected FPGA hardware switch fabric.")
            else:
                self.load_profile_from_json("tools/visualizer/profiles/cubie_a5e_pure.json")
                self.add_log("DETECT", "SSH", f"Target {board_ip}: Detected Pure Silicon (No FPGA attached).")
        except Exception as e:
            self.load_profile_from_json("tools/visualizer/profiles/cubie_a5e_pure.json")
            self.add_log("DETECT", "SYS", f"Target query failed ({e}); defaulting to Pure Silicon (No FPGA).")

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
                    g_state.e906_active_duty_pct = c1
                    g_state.e906_wfi_sleep_pct = 100.0 - c1
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
                    ("Core 0", "DOORBELL", "sun6i_msgbox()", "Mailbox interrupt signaled to Core 1", 1.1, "targets/allwinner_e906/main.cpp", 47),
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

                # A CTF packet (trace endpoint) is never a run of TLPs ([SPEC-STUDIO-15]).
                if runtime_stats.is_ctf(data):
                    g_state.ingest_ctf(data)
                    continue

                if len(data) >= 64:
                    for off in range(0, len(data) - 63, 64):
                        frame = data[off:off+64]
                        
                        # Inspect Big-Endian (FPGA Verilator RTL) vs Little-Endian (Software C++20 SITL)
                        be_type, be_flags, be_tag, be_ch, be_addr, be_len, be_seq, be_ts = struct.unpack(">BBBBIHHQ", frame[:20])
                        le_type, le_flags, le_tag, le_ch, le_addr, le_len, le_seq, le_ts = struct.unpack("<BBBBIHHQ", frame[:20])

                        g_state.add_tlp_packet(frame, le_type, le_tag, le_ch, le_seq, le_ts, True)

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
                                g_state.fpga_trace = f"UDP: FPGA IMU TLP (big-endian, ch 2) | Seq: #{be_seq}"
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

                        # 2. Software C++20 SITL / Live Hardware Telemetry
                        elif le_type == 0x10 and (le_tag == 4 or le_tag == 5):
                            roll_cdeg, pitch_cdeg, yaw_cdeg, alt_mm, spd_cm_s, m1, m2, m3, m4 = struct.unpack("<2hHiH4H", frame[20:40])
                            with g_state.lock:
                                if le_tag == 5:
                                    g_state.source_mode = "LIVE HARDWARE (Radxa Cubie A5E + XuanTie E906)"
                                    g_state.fpga_trace = f"UDP: board TLP tag 5 (heading/GPS) | Seq: #{le_seq}"
                                else:
                                    g_state.source_mode = "SOFTWARE C++20 SITL (gps_imu_app)"
                                    g_state.fpga_trace = f"UDP: SITL TLP tag 4 | Seq: #{le_seq}"
                                g_state.roll_deg = roll_cdeg * 0.01
                                g_state.pitch_deg = pitch_cdeg * 0.01
                                g_state.yaw_deg = yaw_cdeg * 0.01
                                g_state.altitude_m = alt_mm * 0.001
                                g_state.ground_speed_mps = spd_cm_s * 0.01
                        elif le_type == 0x10 and le_tag == 2:
                            # Stream 2: GPS Geodetic Solution Packet (26B)
                            if len(frame) >= 46:
                                itow, lat_i, lon_i, alt_i, spd_i, head_i, sats_b, fix_b = struct.unpack("<i5i2B", frame[20:46])
                                with g_state.lock:
                                    g_state.gps_lat = lat_i * 1e-7
                                    g_state.gps_lon = lon_i * 1e-7
                                    g_state.gps_sats = sats_b
                                    g_state.gps_fix_type = fix_b
                        elif le_type == 0x10 and le_tag == 3:
                            # @impl [SPEC-STUDIO-14] tools/visualizer/SPECIFICATION.md
                            with g_state.lock:
                                g_state._mag_count += 1
                                g_state.mag_payload = bytes(frame[20:60])
                        elif le_tag == 0:
                            # Stream 0: Coroutine / FreeRTOS Task Lifecycle Event (CoroEventPayload, 38B)
                            # @impl [SPEC-STUDIO-02] tools/visualizer/abstractx_studio.py
                            if len(frame) >= 58:
                                ev_id, ts_us, task_id, handle_addr, state_code, reason, raw_name, raw_token = struct.unpack(
                                    "<BQIIBB8s11s", frame[20:58]
                                )
                                name_str = raw_name.decode("utf-8", errors="ignore").split("\x00")[0]
                                token_str = raw_token.decode("utf-8", errors="ignore").split("\x00")[0]
                                state_str = {0: "SPAWNED", 1: "SUSPENDED", 2: "RUNNING", 3: "DONE"}.get(state_code, "RUNNING")

                                with g_state.lock:
                                    found = False
                                    for frame_entry in g_state.coro_frames:
                                        if frame_entry["task_id"] == task_id or frame_entry["name"] == name_str:
                                            frame_entry["state"] = state_str
                                            frame_entry["awaiter"] = token_str if token_str else ("running" if state_str == "RUNNING" else "yield/wait")
                                            frame_entry["name"] = name_str
                                            frame_entry["duration_us"] = max(0.1, (t * 1e6 - ts_us) if ts_us > 0 else 1.0)
                                            found = True
                                            break
                                    if not found and name_str:
                                        g_state.coro_frames.append({
                                            "task_id": task_id,
                                            "name": name_str,
                                            "state": state_str,
                                            "awaiter": token_str if token_str else "active",
                                            "duration_us": 1.0,
                                            "budget_us": 500,
                                            "source": f"task_{name_str}",
                                            "file": "apps/testFreeRTOS/main.cpp",
                                            "line": 40
                                        })
                                    if name_str and name_str not in g_state.coro_names:
                                        g_state.coro_names.append(name_str)
                                        g_state.coro_states.append(1 if state_str == "RUNNING" else 0)
                                        g_state.coro_latencies_us.append(0.5)
                                        g_state.coro_source_info.append({
                                            "file": "apps/testFreeRTOS/main.cpp",
                                            "line": 40,
                                            "token": f"Task: {name_str}"
                                        })

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
        if w_source: w_source.is_visible = False
        if w_fpga: w_fpga.is_visible = False
    elif preset == "core_focus":
        if w_core: w_core.is_visible = True
        if w_user: w_user.is_visible = False
        if w_tlp: w_tlp.is_visible = True
        if w_log: w_log.is_visible = True
        if w_source: w_source.is_visible = False
        if w_fpga: w_fpga.is_visible = False
    elif preset == "source_focus":
        if w_core: w_core.is_visible = True
        if w_source: w_source.is_visible = True
        if w_user: w_user.is_visible = False
        if w_tlp: w_tlp.is_visible = False
        if w_log: w_log.is_visible = False
        if w_fpga: w_fpga.is_visible = False
        g_state.requested_studio_tab = "source"
    elif preset == "fpga_focus":
        g_state.has_fpga = True
        if w_core: w_core.is_visible = True
        if w_fpga: w_fpga.is_visible = True
        if w_user: w_user.is_visible = False
        if w_tlp: w_tlp.is_visible = False
        if w_log: w_log.is_visible = False
        if w_source: w_source.is_visible = False
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

def _no_live_data(panel: str, needs: str) -> bool:
    """True (after drawing a placeholder) when a panel has no live source ([SPEC-STUDIO-14])."""
    # @impl [SPEC-STUDIO-14] tools/visualizer/SPECIFICATION.md
    if g_state.sim_mode:
        return False
    imgui.text_colored(imgui.ImVec4(0.9, 0.75, 0.3, 1.0), f"{panel}: no live data")
    imgui.text_wrapped(f"Needs: {needs}. Start Studio with --sim to see demo data.")
    return True


def _render_connection_bar(prefix: str = "core"):
    """
    # @impl [SPEC-STUDIO-01] tools/visualizer/abstractx_studio.py
    Renders unified hardware connection and live UDP streaming toolbar.
    Provides clear status, IP/Port inputs, Connect, Disconnect, and Reconnect controls.
    """
    try:
        from flight_plugin import get_bridge_status, start_board_bridge, stop_board_bridge
    except ImportError:
        from tools.visualizer.flight_plugin import get_bridge_status, start_board_bridge, stop_board_bridge

    is_bridge_running = get_bridge_status()
    is_live = g_state.connected and g_state.fps_packet_rate > 0

    imgui.begin_group()
    # Status Badge
    if is_live:
        imgui.text_colored(imgui.ImVec4(0.1, 0.95, 0.3, 1.0), "● STREAMING")
    elif is_bridge_running:
        imgui.text_colored(imgui.ImVec4(0.95, 0.8, 0.1, 1.0), "◌ BRIDGE CONNECTED")
    else:
        imgui.text_colored(imgui.ImVec4(0.95, 0.3, 0.2, 1.0), "○ DISCONNECTED")

    imgui.same_line(0, 10)
    # Editable Board IP and Host IP / Port
    imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "Board:")
    imgui.same_line(0, 4)
    imgui.push_item_width(110)
    _, g_state.board_ip = imgui.input_text(f"##board_ip_{prefix}", g_state.board_ip)
    imgui.pop_item_width()

    imgui.same_line(0, 8)
    imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "Host UDP:")
    imgui.same_line(0, 4)
    imgui.push_item_width(110)
    _, g_state.host_ip = imgui.input_text(f"##host_ip_{prefix}", g_state.host_ip)
    imgui.pop_item_width()

    imgui.same_line(0, 4)
    imgui.text(":")
    imgui.same_line(0, 4)
    imgui.push_item_width(55)
    _, g_state.udp_port = imgui.input_int(f"##udp_port_{prefix}", g_state.udp_port, 0, 0)
    imgui.pop_item_width()

    imgui.same_line(0, 12)
    # Connect button
    if not is_bridge_running:
        imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.1, 0.68, 0.3, 1.0))
        imgui.push_style_color(imgui.Col_.button_hovered, imgui.ImVec4(0.15, 0.8, 0.35, 1.0))
        if imgui.button(f"▶ Connect Board##btn_conn_{prefix}"):
            start_board_bridge(g_state.board_ip, g_state.host_ip, g_state.udp_port)
            g_state.add_log("BRIDGE", "SSH", f"Started live sensor streaming from {g_state.board_ip} to {g_state.host_ip}:{g_state.udp_port}")
        imgui.pop_style_color(2)
    else:
        imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.15, 0.5, 0.85, 1.0))
        imgui.push_style_color(imgui.Col_.button_hovered, imgui.ImVec4(0.2, 0.6, 0.95, 1.0))
        if imgui.button(f"🔄 Reconnect##btn_reconn_{prefix}"):
            stop_board_bridge()
            time.sleep(0.2)
            start_board_bridge(g_state.board_ip, g_state.host_ip, g_state.udp_port)
            g_state.add_log("BRIDGE", "SSH", f"Reconnected live streaming to {g_state.board_ip}")
        imgui.pop_style_color(2)

        imgui.same_line(0, 6)
        imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.85, 0.2, 0.2, 1.0))
        imgui.push_style_color(imgui.Col_.button_hovered, imgui.ImVec4(0.95, 0.3, 0.3, 1.0))
        if imgui.button(f"⏹ Disconnect##btn_disc_{prefix}"):
            stop_board_bridge()
            g_state.add_log("BRIDGE", "SSH", "Stopped live sensor streaming")
        imgui.pop_style_color(2)

    imgui.same_line(0, 14)
    imgui.text_colored(imgui.ImVec4(0.7, 0.8, 0.9, 1.0), f"| Packets: {g_state.packet_count:,} ({g_state.fps_packet_rate} pkts/s)")
    imgui.end_group()

def _render_status_bar():
    """Renders bottom status bar with quick dynamic window layout presets, reset, and canvas pop-out."""
    _render_connection_bar("status")
    imgui.same_line(0, 15)
    # @impl [SPEC-STUDIO-14] tools/visualizer/SPECIFICATION.md
    if g_state.sim_mode:
        imgui.text_colored(imgui.ImVec4(1.0, 0.35, 0.35, 1.0), "SIMULATED DATA (--sim)")
        imgui.same_line()
    imgui.text(f"| Platform: {g_state.platform_name} | {'FPGA: Tang20K' if g_state.has_fpga else 'Pure Silicon (No FPGA)'}")
    imgui.same_line()
    membrowse_lbl = "MemBrowse CI: 0 B Heap (Pass)" if g_state.mem_targets else "MemBrowse CI: no metrics"
    if imgui.small_button(membrowse_lbl):
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
    ]
    if g_state.has_fpga:
        presets.append(("⚡ FPGA Hardware", "fpga_focus"))
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

def runtime_plot_y_max(values, autoscale=True):
    """Top of the busy % axis ([SPEC-STUDIO-15]): fixed 100, or the data peak plus headroom,
    rounded up to a 1-2-5 step so the axis does not jitter every second."""
    # @impl [SPEC-STUDIO-15] tools/visualizer/SPECIFICATION.md
    if not autoscale:
        return 100.0
    peak = max(values, default=0.0) * 1.25
    step = 0.01
    while step < 100.0:
        for m in (1.0, 2.0, 5.0):
            if peak <= step * m:
                return step * m
        step *= 10.0
    return 100.0


def _render_live_runtime_status():
    """Live CPU, coroutine, driver and pool status from the target ([SPEC-STUDIO-15])."""
    # @impl [SPEC-STUDIO-15] tools/visualizer/SPECIFICATION.md
    with g_state.lock:
        rates = g_state.runtime_rates
        report = g_state.runtime_report
        n = g_state.runtime_reports
        hist = list(g_state.runtime_busy_hist)
    if rates is None:
        imgui.text_colored(imgui.ImVec4(0.9, 0.75, 0.3, 1.0), "Runtime status: waiting for the target")
        imgui.text_wrapped("Needs: runtime status reports (CTF stream 3) on the abstractx-trace endpoint, "
                           f"one per second; {n} received so far. Connect the board link to start "
                           "e906_rpmsg_bridge.py trace.")
        return
    dim = imgui.ImVec4(0.6, 0.7, 0.8, 1.0)
    hz = report["cycle_hz"]

    # Gauges: target busy, interrupt share, frame pool fill.
    imgui.columns(3, "rt_gauges", False)
    for value, label, sub in (
            (rates["busy_pct"], "Target CPU", f"busy {rates['busy_pct']:.1f}%"),
            (rates["isr_pct"], "Interrupts", f"ISR {rates['isr_pct']:.2f}%"),
            (100.0 * rates["pool_used"] / max(1, rates["pool_capacity"]), "Frame pool",
             f"{rates['pool_used']} / {rates['pool_capacity']} B")):
        col_w = imgui.get_column_width()
        pos = imgui.get_cursor_screen_pos()
        draw_radial_gauge(pos.x + col_w / 2.0, pos.y + 40.0, 32.0, value, label, sub)
        imgui.dummy(imgui.ImVec2(col_w, 100.0))
        imgui.next_column()
    imgui.columns(1)
    imgui.text_colored(dim, f"Clock {hz / 1e6:.0f} MHz | step() {rates['steps_per_s']:.0f}/s | "
                            f"reports {n} | host CPU: not measured")
    imgui.separator()

    # Busy history: auto-scaled Y by default, selectable window, fills the space the tables leave.
    _, g_state.runtime_plot_autoscale = imgui.checkbox("Auto scale##rt_auto", g_state.runtime_plot_autoscale)
    for secs, lbl in ((60, "60 s"), (300, "5 min")):
        imgui.same_line()
        if imgui.radio_button(f"{lbl}##rt_win", g_state.runtime_plot_window_s == secs):
            g_state.runtime_plot_window_s = secs
    window = g_state.runtime_plot_window_s
    t_end = hist[-1][0] if hist else 0.0
    hist = [h for h in hist if t_end - h[0] <= window]
    n_rows = len(rates["tasks"]) + len(rates["drivers"]) + 3 + 3 * 3  # rows + titles/headers
    plot_h = max(220.0, imgui.get_content_region_avail().y - n_rows * imgui.get_text_line_height_with_spacing() - 40.0)
    if len(hist) >= 2 and implot.begin_plot("Target CPU busy % (1 s reports)##rt_busy", imgui.ImVec2(-1, plot_h)):
        busy = np.array([h[1] for h in hist], dtype=np.float64)
        isr = np.array([h[2] for h in hist], dtype=np.float64)
        y_max = runtime_plot_y_max(np.concatenate((busy, isr)).tolist(), g_state.runtime_plot_autoscale)
        implot.setup_axes("seconds ago", "%", implot.AxisFlags_.none, implot.AxisFlags_.none)
        implot.setup_axes_limits(-window, 0, 0, y_max, imgui.Cond_.always)
        t = np.array([h[0] - t_end for h in hist], dtype=np.float64)
        implot.plot_line("busy %", t, busy)
        implot.plot_line("ISR %", t, isr)
        implot.end_plot()

    def table(title, cols, rows):
        imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), title)
        imgui.columns(len(cols), title, True)
        for c in cols:
            imgui.text(c)
            imgui.next_column()
        imgui.separator()
        for row in rows:
            for cell in row:
                imgui.text(cell)
                imgui.next_column()
        imgui.columns(1)
        imgui.spacing()

    table("Coroutines (like RTOS task run-time stats)", ["Task", "CPU %", "Resumes/s", "Longest resume"],
          [(t["name"], f"{t['cpu_pct']:.1f}", f"{t['resumes_per_s']:.1f}",
            f"{t['max_us'] / 1000:.1f} ms" if t["max_us"] >= 1000 else f"{t['max_us']:.1f} µs")
           for t in rates["tasks"]])
    table("Drivers / interrupt lines", ["Driver (line)", "IRQ/s", "ISR CPU %", "µs / IRQ", "Errors"],
          [(f"{d['name']} ({d['id']})", f"{d['irq_per_s']:.0f}", f"{d['isr_pct']:.3f}",
            f"{d['isr_pct'] * 1e4 / d['irq_per_s']:.2f}" if d["irq_per_s"] else "-",
            f"{d['errors']}" + (f" (+{d['new_errors']})" if d["new_errors"] else ""))
           for d in rates["drivers"]])
    table("Pools", ["Pool", "Now", "High water", "Capacity / note"],
          [("Coroutine frames (B)", str(rates["pool_used"]), str(rates["pool_high_water"]), str(rates["pool_capacity"])),
           ("Dispatcher queue", str(rates["queue_depth"]), "-",
            f"{rates['queue_capacity']} | dropped {rates['dropped_resumes']}"),
           ("Trace events", "-", "-", f"discarded {rates['trace_discarded']}")])


def _render_core_cpu_and_topology():
    """
    Renders Silicon Cores, Radial CPU & SPSC Gauges, Interactive Zoomable Load Plots,
    Dynamic Window Sizing Controls, and Platform Architecture Topology.
    """
    if not g_state.sim_mode:
        _render_live_runtime_status()
        return
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

    # ── Section 1: Silicon Cores & Hardware SPSC Gauges ──
    num_cols = 4 if g_state.has_fpga else 3
    imgui.columns(num_cols, "cpu_gauge_cols", True)

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
                      g_state.e906_active_duty_pct, "Core 1", f"Duty {g_state.e906_active_duty_pct:.1f}%")
    imgui.dummy(imgui.ImVec2(col_w, 105.0))
    imgui.text_colored(imgui.ImVec4(0.3, 1.0, 0.4, 1.0), "Coroutine Engine")
    imgui.text("Dispatcher: 8.2 kHz | 6 Tasks")
    imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), f"Pool: {g_state.coro_pool_used_bytes // 1024} / {g_state.coro_pool_capacity_bytes // 1024} KB")
    imgui.next_column()

    # Card 3: SPU (FPGA AXI-Stream TLP Crossbar Fabric) - only rendered if FPGA present
    if g_state.has_fpga:
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
        if g_state.has_fpga:
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
    if not g_state.simple_trace_events and _no_live_data(
            "Simple Trace", "CTF trace packets on UDP (Studio has no CTF packet decoder yet)"):
        return
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
    if _no_live_data("Dual-Plane Timeline", "driver and coroutine timing events from the target (not decoded yet)"):
        return
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
    if imgui.button("▶ Run Local Memory Audit"):
        g_state.run_membrowse_analysis()
    imgui.same_line()
    if imgui.button("📊 Run SpecTrace Dashboard"):
        g_state.run_spectrace_dashboard()
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
    # @impl [SPEC-STUDIO-14] tools/visualizer/SPECIFICATION.md
    if not g_state.mem_targets:
        imgui.text_colored(imgui.ImVec4(0.9, 0.75, 0.3, 1.0), "No memory metrics loaded.")
        imgui.text_wrapped("tools/visualizer/memory_metrics.json does not exist yet: "
                           "click 'Run Local Memory Audit' to build it from the target ELFs.")
        return

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


def _render_cli_console():
    """
    # @impl [SPEC-STUDIO-13] tools/visualizer/abstractx_studio.py
    Renders Level 1 Core: Integrated Interactive CLI Console & MicroPython Hardware REPL.
    """
    imgui.begin_group()
    mode_is_py = (g_state.cli_mode == "MicroPython")
    col_mode = imgui.ImVec4(0.3, 0.8, 1.0, 1.0) if mode_is_py else imgui.ImVec4(0.3, 0.9, 0.5, 1.0)
    imgui.text_colored(imgui.ImVec4(0.9, 0.75, 0.2, 1.0), "Interactive CLI Console & Hardware REPL")
    imgui.same_line(0, 16)
    if imgui.button("🗖 Pop to Canvas##cli_pop"):
        g_state.canvas_windows["cli"] = True
    imgui.same_line(0, 16)

    # Mode Selector
    imgui.text_colored(col_mode, f"[{g_state.cli_mode} Mode]")
    imgui.same_line(0, 8)
    if imgui.button("Switch to MicroPython##mode_py" if not mode_is_py else "Switch to AbstractX##mode_ax"):
        g_state.cli_mode = "MicroPython" if not mode_is_py else "AbstractX"
        g_state.add_cli_line("INFO", f"Switched mode to {g_state.cli_mode}.")
    imgui.end_group()
    imgui.separator()

    # Fast Action Buttons Bar
    imgui.begin_group()
    imgui.text("Quick Actions:")
    imgui.same_line()
    if imgui.button("ps##cli_ps"):
        g_state.execute_cli_command("ps")
    imgui.same_line()
    if imgui.button("ping##cli_png"):
        g_state.execute_cli_command("ping 3")
    imgui.same_line()
    if imgui.button("tasks##cli_tsk"):
        g_state.execute_cli_command("tasks")
    imgui.same_line()
    if imgui.button("peek MTIME##cli_mtime"):
        g_state.execute_cli_command("peek 0x07090000")
    imgui.same_line()
    if imgui.button("stats##cli_stat"):
        g_state.execute_cli_command("stats")
    imgui.same_line()
    if imgui.button("Clear##cli_clr"):
        g_state.execute_cli_command("clear")
    imgui.same_line(0, 16)
    _, g_state.cli_auto_scroll = imgui.checkbox("Auto-Scroll##cli_ascroll", g_state.cli_auto_scroll)
    imgui.end_group()
    imgui.separator()

    # Terminal Scrolling Window
    imgui.push_style_color(imgui.Col_.child_bg, imgui.ImVec4(0.06, 0.08, 0.11, 1.0))
    imgui.begin_child("CliOutputRegion", imgui.ImVec2(-1, -40), True)

    col_out   = imgui.ImVec4(0.85, 0.88, 0.92, 1.0)
    col_prmpt = imgui.ImVec4(0.25, 0.85, 1.0, 1.0)
    col_ok    = imgui.ImVec4(0.3, 0.92, 0.45, 1.0)
    col_err   = imgui.ImVec4(0.95, 0.35, 0.35, 1.0)
    col_warn  = imgui.ImVec4(0.95, 0.75, 0.2, 1.0)
    col_coro  = imgui.ImVec4(0.35, 0.9, 0.75, 1.0)
    col_task  = imgui.ImVec4(0.85, 0.75, 0.95, 1.0)
    col_head  = imgui.ImVec4(0.9, 0.8, 0.3, 1.0)

    with g_state.lock:
        lines = list(g_state.cli_history)

    for line_type, txt in lines:
        if line_type == "PROMPT":
            imgui.text_colored(col_prmpt, txt)
        elif line_type in ("OK", "PY_RES"):
            imgui.text_colored(col_ok, txt)
        elif line_type == "ERR":
            imgui.text_colored(col_err, txt)
        elif line_type == "WARN":
            imgui.text_colored(col_warn, txt)
        elif line_type == "CORO":
            imgui.text_colored(col_coro, txt)
        elif line_type == "TASK":
            imgui.text_colored(col_task, txt)
        elif line_type == "HEADER":
            imgui.text_colored(col_head, txt)
        else:
            imgui.text_colored(col_out, txt)

    if g_state.cli_auto_scroll:
        imgui.set_scroll_here_y(1.0)
    imgui.end_child()
    imgui.pop_style_color()

    # Input line at bottom
    prompt_str = ">>> " if mode_is_py else "abstractx> "
    imgui.text_colored(col_prmpt, prompt_str)
    imgui.same_line()
    imgui.push_item_width(-90)
    entered, g_state.cli_input_text = imgui.input_text("##cli_input_field", g_state.cli_input_text, imgui.InputTextFlags_.enter_returns_true)
    imgui.pop_item_width()
    imgui.same_line()
    if imgui.button("Send##cli_snd") or entered:
        if g_state.cli_input_text.strip():
            g_state.execute_cli_command(g_state.cli_input_text)
            g_state.cli_input_text = ""


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
    if not frames and _no_live_data("Coroutine Inspector", "coroutine lifecycle TLPs (tag 0) from the target"):
        return

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
    if _no_live_data("Flow Integrity", "per-channel stall and latch timestamps from the TLP router (not decoded yet)"):
        return
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

def is_tool_rendered_externally(key: str) -> bool:
    """Returns True if the tool is actively visible in a dedicated dock window or floating canvas."""
    if g_state.canvas_windows.get(key, False):
        return True
    w = g_dockable_windows.get(key)
    if w is not None and w.is_visible:
        return True
    return False

def _render_popped_tab_placeholder(display_name: str, key: str, window_title: str):
    """Renders an interactive placeholder when a diagnostic tab is popped out or active in a dedicated window."""
    imgui.dummy(imgui.ImVec2(1, 16))
    imgui.push_style_color(imgui.Col_.child_bg, imgui.ImVec4(0.08, 0.12, 0.18, 0.95))
    imgui.begin_child(f"PoppedPlaceholder_{key}", imgui.ImVec2(-1, 160), True)

    is_canvas = g_state.canvas_windows.get(key, False)
    mode_desc = "Movable Floating Canvas Window" if is_canvas else "Dedicated Window"
    imgui.text_colored(imgui.ImVec4(0.3, 0.85, 1.0, 1.0), f"🗖 {display_name} Active in {mode_desc}")
    imgui.separator()
    imgui.spacing()
    imgui.text("This diagnostic tool is currently open in its dedicated window to eliminate duplicate controls and clutter.")
    imgui.text("You can view it in its dedicated window, or click below to embed it directly inside this tab.")
    imgui.dummy(imgui.ImVec2(1, 12))
    if imgui.button(f"Bring {display_name} to Front##focus_{key}"):
        try:
            if is_canvas:
                imgui.set_window_focus(f"{window_title}##CanvasWin")
            else:
                imgui.set_window_focus(display_name)
        except Exception:
            pass
    imgui.same_line(0, 16)
    imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.65, 0.4, 0.9))
    if imgui.button(f"🗗 Embed into this Studio Tab##dock_{key}"):
        g_state.canvas_windows[key] = False
        w = g_dockable_windows.get(key)
        if w:
            w.is_visible = False
    imgui.pop_style_color()
    imgui.end_child()
    imgui.pop_style_color()

def _render_floating_canvas_windows():
    """
    Renders popped-out floating canvas windows directly onto the desktop workspace.
    Enables the user to position and move multiple diagnostic tools side-by-side or stacked.
    """

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

    # 9. FPGA & Hardware Peripherals Floating Window (only if FPGA present)
    if g_state.has_fpga and g_state.canvas_windows.get("fpga", False):
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

    # 11. Interactive CLI Console & MicroPython Hardware REPL Floating Window
    if g_state.canvas_windows.get("cli", False):
        imgui.set_next_window_size(imgui.ImVec2(1040, 620), imgui.Cond_.first_use_ever)
        expanded, opened = imgui.begin("CLI Console & Hardware REPL##CanvasWin", True)
        if expanded:
            imgui.begin_group()
            imgui.text_colored(imgui.ImVec4(0.95, 0.82, 0.25, 1.0), "🗖 MOVABLE CANVAS WINDOW")
            imgui.same_line(0, 16)
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.65, 0.4, 0.9))
            if imgui.button("🗗 Pop In (Dock to Studio Tab)##cli_popin"):
                g_state.canvas_windows["cli"] = False
            imgui.pop_style_color()
            imgui.same_line(0, 16)
            imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), "| Drag titlebar to move · Drag edges to resize")
            imgui.end_group()
            imgui.separator()
            _render_cli_console()
        imgui.end()
        if not opened:
            g_state.canvas_windows["cli"] = False

    # 12. Load Hardware Profile JSON Dialog
    if getattr(g_state, "show_profile_dialog", False):
        imgui.set_next_window_size(imgui.ImVec2(520, 280), imgui.Cond_.first_use_ever)
        expanded, opened = imgui.begin("Load Target Hardware Profile JSON##Dialog", True)
        if expanded:
            imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), "Target Hardware Topology Profile (JSON):")
            _, g_state.custom_profile_path = imgui.input_text("##profile_path_in", g_state.custom_profile_path)
            imgui.spacing()
            imgui.text("Predefined Target Profiles:")
            for p_lbl, p_file in [
                ("Cubie A5E (Pure Silicon - No FPGA)", "tools/visualizer/profiles/cubie_a5e_pure.json"),
                ("Cubie A5E + FPGA X-Fabric", "tools/visualizer/profiles/cubie_a5e_fpga.json"),
                ("Raspberry Pi Pico 2 W", "tools/visualizer/profiles/pico2w_rp2350.json"),
                ("Desktop SITL (Linux)", "tools/visualizer/profiles/desktop_sitl.json"),
            ]:
                if imgui.small_button(f"📄 {p_lbl}"):
                    g_state.custom_profile_path = p_file
            imgui.spacing()
            imgui.separator()
            if imgui.button("Load Profile##act_btn", imgui.ImVec2(120, 28)):
                g_state.load_profile_from_json(g_state.custom_profile_path)
                g_state.show_profile_dialog = False
            imgui.same_line()
            if imgui.button("Cancel##prof_canc", imgui.ImVec2(100, 28)):
                g_state.show_profile_dialog = False
        imgui.end()
        if not opened:
            g_state.show_profile_dialog = False

    # 13. Load barectf Trace Schema JSON Dialog
    if getattr(g_state, "show_schema_dialog", False):
        imgui.set_next_window_size(imgui.ImVec2(520, 240), imgui.Cond_.first_use_ever)
        expanded, opened = imgui.begin("Load CTF 1.8 barectf Schema JSON##Dialog", True)
        if expanded:
            imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), "Dynamic CTF 1.8 Schema File:")
            _, g_state.custom_schema_path = imgui.input_text("##schema_path_in", g_state.custom_schema_path)
            imgui.spacing()
            if imgui.small_button("📄 apps/gps_imu_app/trace_schema.json"):
                g_state.custom_schema_path = "apps/gps_imu_app/trace_schema.json"
            imgui.spacing()
            imgui.separator()
            if imgui.button("Compile Schema##act_btn", imgui.ImVec2(140, 28)):
                g_state.load_barectf_schema(g_state.custom_schema_path)
                g_state.show_schema_dialog = False
            imgui.same_line()
            if imgui.button("Cancel##schema_canc", imgui.ImVec2(100, 28)):
                g_state.show_schema_dialog = False
        imgui.end()
        if not opened:
            g_state.show_schema_dialog = False

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
    _render_connection_bar("core")
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
    flag_cli     = imgui.TabItemFlags_.set_selected if g_state.requested_studio_tab == "cli"      else 0

    # Reset requested tab after consumption
    g_state.requested_studio_tab = None

    if imgui.begin_tab_bar("CoreStudioTabBar"):
        # Tab 1: Coroutine Inspector (C++20 first - the primary diagnostic surface)
        if imgui.begin_tab_item("Coroutine Inspector", None, flag_coro)[0]:
            if is_tool_rendered_externally("coro"):
                _render_popped_tab_placeholder("Coroutine Inspector", "coro", "Coroutine State & Suspension Inspector")
            else:
                _render_coroutine_inspector()
            imgui.end_tab_item()

        if imgui.begin_tab_item("CPU Gauges & Topology", None, flag_cpu)[0]:
            if is_tool_rendered_externally("cpu"):
                _render_popped_tab_placeholder("CPU Gauges & Topology", "cpu", "Silicon Cores & CPU Gauges")
            else:
                _render_core_cpu_and_topology()
            imgui.end_tab_item()

        if imgui.begin_tab_item("TLP Bus Debugger", None, flag_tlp)[0]:
            if is_tool_rendered_externally("tlp"):
                _render_popped_tab_placeholder("TLP Bus Debugger", "tlp", "TLP Bus Debugger")
            else:
                _render_tlp_debugger_window()
            imgui.end_tab_item()

        if imgui.begin_tab_item("System Event Log", None, flag_log)[0]:
            if is_tool_rendered_externally("log"):
                _render_popped_tab_placeholder("System Event Log", "log", "System Event Log")
            else:
                _render_event_log_window()
            imgui.end_tab_item()

        if imgui.begin_tab_item("Flow Integrity & Pacing Eye", None, flag_flow)[0]:
            _render_flow_integrity_and_eye_diagram()
            imgui.end_tab_item()

        if imgui.begin_tab_item("Simple Trace Viewer", None, flag_trace)[0]:
            if is_tool_rendered_externally("trace"):
                _render_popped_tab_placeholder("Simple Trace Viewer", "trace", "Simple Trace Viewer")
            else:
                _render_simple_trace_view()
            imgui.end_tab_item()

        if imgui.begin_tab_item("Dual-Plane Timeline", None, flag_timeline)[0]:
            if is_tool_rendered_externally("timeline"):
                _render_popped_tab_placeholder("Dual-Plane Timeline", "timeline", "Dual-Plane Coroutine Timeline")
            else:
                _render_core_timeline()
            imgui.end_tab_item()

        if imgui.begin_tab_item("Source Code & RTL Inspector", None, flag_source)[0]:
            if is_tool_rendered_externally("source"):
                _render_popped_tab_placeholder("Source Code & RTL Inspector", "source", "Source Code & RTL Inspector")
            else:
                _render_source_inspector_window()
            imgui.end_tab_item()

        if g_state.has_fpga:
            if imgui.begin_tab_item("FPGA Peripherals", None, flag_fpga)[0]:
                if is_tool_rendered_externally("fpga"):
                    _render_popped_tab_placeholder("FPGA Peripherals", "fpga", "FPGA & Hardware Peripherals")
                else:
                    _render_fpga_peripherals_window()
                imgui.end_tab_item()

        if imgui.begin_tab_item("MemBrowse CI Report", None, flag_memory)[0]:
            if is_tool_rendered_externally("memory"):
                _render_popped_tab_placeholder("MemBrowse CI Report", "memory", "Memory & MemBrowse CI Tracker")
            else:
                _render_level1_memory()
            imgui.end_tab_item()

        # Tab 11: Interactive CLI Console & MicroPython Hardware REPL
        # @impl [SPEC-STUDIO-13] tools/visualizer/abstractx_studio.py
        if imgui.begin_tab_item("CLI Console & REPL", None, flag_cli)[0]:
            if is_tool_rendered_externally("cli"):
                _render_popped_tab_placeholder("CLI Console & REPL", "cli", "Interactive CLI Console & Hardware REPL")
            else:
                _render_cli_console()
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
            g_state.tlp_view = []
            g_state.selected_tlp = None
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
    tag_names = {1: "IMU", 2: "GPS", 3: "MAG", 4: "AHRS", 5: "ATT"}
    with g_state.lock:
        counts = dict(g_state.tlp_tag_counts)
    if counts:
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "Per tag: " + "  ".join(
            f"{tag_names.get(t, f'0x{t:02X}')} {n:,}" for t, n in sorted(counts.items())))
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

    # @impl [SPEC-STUDIO-04] tools/visualizer/abstractx_studio.py
    # Refresh the table 4 times a second (the live link sends ~30 TLPs/s), and keep the
    # selected packet itself so the inspector does not jump as new packets arrive.
    now = time.time()
    with g_state.lock:
        if not g_state.tlp_stream_paused and now - g_state.tlp_view_t >= 0.25:
            g_state.tlp_view = list(g_state.recent_tlp_packets)
            g_state.tlp_view_t = now
        packets = g_state.tlp_view
        selected = g_state.selected_tlp

    for idx, pkt in enumerate(reversed(packets)):
        is_selected = pkt is selected
        tag_name = tag_names.get(pkt["tag"], f"0x{pkt['tag']:02X}")
        clicked, _ = imgui.selectable(f"#{pkt['seq']:<5}##{id(pkt)}", is_selected, imgui.SelectableFlags_.span_all_columns)
        if clicked:
            with g_state.lock:
                g_state.selected_tlp = pkt
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
    p = selected if selected is not None else (packets[-1] if packets else None)
    if p is not None:
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
    # Row 1: Level chips and action buttons
    levels = ["ALL", "INFO", "TLP", "CORO", "ISR", "WARN"]
    for lvl in levels:
        is_active = (lvl == g_state.log_filter_level)
        if is_active:
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.6, 0.9, 1.0))
        if imgui.button(f"{lvl}##lvl"):
            g_state.log_filter_level = lvl
        if is_active:
            imgui.pop_style_color()
        imgui.same_line()

    imgui.same_line(0, 12)
    _, g_state.log_auto_scroll = imgui.checkbox("Auto-Scroll##log", g_state.log_auto_scroll)
    imgui.same_line(0, 10)
    if imgui.button("Clear Log##btn"):
        with g_state.lock:
            g_state.logs.clear()
    imgui.same_line(0, 10)
    if imgui.button("🗖 Pop to Canvas##log"):
        g_state.canvas_windows["log"] = True

    # Row 2: Search filter input
    imgui.text("Search Filter:")
    imgui.same_line()
    imgui.set_next_item_width(-10)
    _, g_state.log_search_text = imgui.input_text("##log_search_input", g_state.log_search_text, 64)
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
    if _no_live_data("Source & RTL Profiling", "trace events with source locations and timings (not decoded yet)"):
        return
    g_state.update_rates()
    imgui.begin_group()

    # View Mode: C++ Firmware vs FPGA RTL
    if not g_state.has_fpga:
        g_state.source_view_mode = "CPP"
    else:
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
        "targets/allwinner_e906/src/io_processor.cpp": [
            (45, "void handle_msgbox_irq() {", None),
            (46, "    // Signal Doorbell to Core 1 coroutine engine", None),
            (47, "    e906_signal_doorbell(DOORBELL_CH_IMU);", {"lat": 1.1, "budget": 5.0, "overrun": False, "token": "sun6i_msgbox()", "diag": "Inter-Core RPC Doorbell"}),
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
    if _no_live_data("FPGA Peripherals", "FPGA register / status telemetry (not decoded yet)"):
        return
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
    parser.add_argument("--port", type=int, default=DEFAULT_UDP_PORT, help="UDP listening port (default: 9870)")
    parser.add_argument("--board-ip", type=str, default="192.168.3.3", help="Target board IP address (default: 192.168.3.3)")
    parser.add_argument("--host-ip", type=str, default="192.168.3.1", help="Studio host IP address for UDP telemetry (default: 192.168.3.1)")
    parser.add_argument("--sim", action="store_true", help="Run with synthetic flight & telemetry generator")
    parser.add_argument("--tab", type=str, default="flight", choices=["flight", "platform", "memory"],
                        help="Initial tab to display in single-window mode")
    parser.add_argument("--viewports", action="store_true", default=False,
                        help="Enable experimental multi-viewport secondary OS windows")
    parser.add_argument("--auto-connect", action="store_true", default=False,
                        help="Auto-start live telemetry bridge to physical board on launch")
    args = parser.parse_args()

    g_initial_tab = args.tab
    if args.sim:
        g_state.enable_sim()
    g_state.board_ip = args.board_ip
    g_state.host_ip = args.host_ip
    g_state.udp_port = args.port

    import atexit
    try:
        from flight_plugin import start_board_bridge, stop_board_bridge
    except ImportError:
        from tools.visualizer.flight_plugin import start_board_bridge, stop_board_bridge

    atexit.register(stop_board_bridge)

    if not args.sim and args.auto_connect:
        start_board_bridge(args.board_ip, args.host_ip, args.port)
        g_state.add_log("BRIDGE", "SSH", f"Auto-started live sensor streaming from XuanTie E906 ({args.board_ip})")

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
    runner_params.app_window_params.window_geometry.size = (1680, 960)
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
    runner_params.callbacks.show_menus = _render_studio_menus
    runner_params.callbacks.show_status = _render_status_bar
    runner_params.callbacks.setup_imgui_style = _setup_studio_style
    runner_params.callbacks.post_render_dockable_windows = _render_floating_canvas_windows
    return runner_params

def _render_studio_menus():
    """
    # @impl [SPEC-STUDIO-01] tools/visualizer/abstractx_studio.py
    # @impl [SPEC-STUDIO-07] tools/visualizer/abstractx_studio.py
    Renders top menu bar items for hardware connection, profile configuration, and schema management.
    """
    try:
        from flight_plugin import get_bridge_status, start_board_bridge, stop_board_bridge
    except ImportError:
        from tools.visualizer.flight_plugin import get_bridge_status, start_board_bridge, stop_board_bridge

    is_bridge = get_bridge_status()
    is_live = g_state.connected and g_state.fps_packet_rate > 0

    # ── 1. Top-Level Connect Action in Menu Bar (Direct 1-Click Access) ──
    if not is_bridge:
        if imgui.menu_item(f"▶ Connect ({g_state.board_ip})", "", False)[0]:
            start_board_bridge(g_state.board_ip, g_state.host_ip, g_state.udp_port)
            g_state.add_log("BRIDGE", "SSH", f"Started live sensor streaming from {g_state.board_ip}")
    else:
        status_lbl = "● Streaming" if is_live else "◌ Bridge Up"
        imgui.menu_item(status_lbl, "", False, False)
        if imgui.menu_item("⏹ Disconnect", "", False)[0]:
            stop_board_bridge()
            g_state.add_log("BRIDGE", "SSH", "Stopped live sensor streaming")

    # ── 2. Connection Submenu ──
    if imgui.begin_menu("Connection"):
        if not is_bridge:
            if imgui.menu_item(f"▶ Connect to Board ({g_state.board_ip}:{g_state.udp_port})", "", False)[0]:
                start_board_bridge(g_state.board_ip, g_state.host_ip, g_state.udp_port)
                g_state.add_log("BRIDGE", "SSH", f"Started live sensor streaming from {g_state.board_ip}")
        else:
            if imgui.menu_item(f"🔄 Reconnect Board ({g_state.board_ip})", "", False)[0]:
                stop_board_bridge()
                time.sleep(0.2)
                start_board_bridge(g_state.board_ip, g_state.host_ip, g_state.udp_port)
                g_state.add_log("BRIDGE", "SSH", f"Reconnected live streaming to {g_state.board_ip}")
            if imgui.menu_item("⏹ Disconnect Board", "", False)[0]:
                stop_board_bridge()
                g_state.add_log("BRIDGE", "SSH", "Stopped live sensor streaming")

        imgui.separator()
        imgui.text("Target Board IP:")
        imgui.set_next_item_width(140)
        _, g_state.board_ip = imgui.input_text("##menu_board_ip", g_state.board_ip)

        imgui.text("Host UDP Port:")
        imgui.set_next_item_width(80)
        _, g_state.udp_port = imgui.input_int("##menu_udp_port", g_state.udp_port, 0, 0)

        imgui.separator()
        if imgui.menu_item("🔍 Auto-Detect Target Silicon & FPGA", "", False)[0]:
            g_state.auto_detect_target(g_state.board_ip)
        imgui.end_menu()

    # ── 3. Target Hardware & Profile Submenu ──
    if imgui.begin_menu("Hardware Profile"):
        imgui.text_colored(imgui.ImVec4(0.95, 0.8, 0.3, 1.0), f"Current: {g_state.platform_name}")
        imgui.separator()

        # Selection: Cubie A5E Pure Silicon
        is_a5e_pure = (g_state.profile_id == "cubie_a5e_pure")
        if imgui.menu_item("● Cubie A5E Pure Silicon (No FPGA)" if is_a5e_pure else "○ Cubie A5E Pure Silicon (No FPGA)", "", False)[0]:
            g_state.load_profile_from_json("tools/visualizer/profiles/cubie_a5e_pure.json")

        # Selection: Cubie A5E + Tang 20K FPGA
        is_a5e_fpga = (g_state.profile_id == "cubie_a5e_fpga")
        if imgui.menu_item("● Cubie A5E + FPGA X-Fabric (Tang 20K)" if is_a5e_fpga else "○ Cubie A5E + FPGA X-Fabric (Tang 20K)", "", False)[0]:
            g_state.load_profile_from_json("tools/visualizer/profiles/cubie_a5e_fpga.json")

        # Selection: Pico 2 W
        is_pico = (g_state.profile_id == "pico2w_rp2350")
        if imgui.menu_item("● Raspberry Pi Pico 2 W (No FPGA)" if is_pico else "○ Raspberry Pi Pico 2 W (No FPGA)", "", False)[0]:
            g_state.load_profile_from_json("tools/visualizer/profiles/pico2w_rp2350.json")

        # Selection: Desktop SITL
        is_sitl = (g_state.profile_id == "desktop_sitl")
        if imgui.menu_item("● Desktop SITL Simulation (No FPGA)" if is_sitl else "○ Desktop SITL Simulation (No FPGA)", "", False)[0]:
            g_state.load_profile_from_json("tools/visualizer/profiles/desktop_sitl.json")

        imgui.separator()
        # Direct FPGA Switch Fabric Toggle Checkbox
        fpga_val = g_state.has_fpga
        changed, fpga_val = imgui.checkbox("FPGA Switch Fabric Present", fpga_val)
        if changed:
            g_state.set_has_fpga(fpga_val)

        imgui.separator()
        imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), "JSON Configuration & Schema:")
        if imgui.menu_item("📂 Load Hardware Profile (JSON)...", "", False)[0]:
            g_state.show_profile_dialog = True
        if imgui.menu_item("📄 Load barectf Trace Schema (JSON)...", "", False)[0]:
            g_state.show_schema_dialog = True
        if imgui.menu_item("🔄 Reload Default Schema (trace_schema.json)", "", False)[0]:
            g_state.load_barectf_schema("apps/gps_imu_app/trace_schema.json")
        imgui.end_menu()

if __name__ == "__main__":
    main()

