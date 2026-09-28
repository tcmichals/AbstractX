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

        # Logging & TLP Debugger State
        self.logs = []
        self.recent_tlp_packets = []
        self.selected_tlp_idx = 0
        self.tlp_stream_paused = False
        self.log_filter_level = "ALL"
        self.log_search_text = ""
        self.log_auto_scroll = True

        # Tracealyzer & CPU Line Chart History (Last 30 seconds, 60 samples at 2 Hz)
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

        # Simple Trace Viewer State
        self.simple_trace_events = []
        self.simple_trace_paused = False
        self.simple_trace_filter_core = "ALL"
        self.simple_trace_search = ""
        self.simple_trace_auto_scroll = True
        self.selected_trace_idx = 0
        self._seed_initial_trace_events()

        # Tracealyzer Multi-Track Timing Diagram & Drill-Down State
        self.active_layout_preset = "balanced"
        self.source_code_files = [
            "apps/gps_imu_app/src/main.cpp",
            "include/abstractx/drivers/imu/icm42688p.hpp",
            "include/abstractx/fusion/attitude_filter.hpp",
            "targets/allwinner_e907/main.cpp"
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

        self.timing_spans = [
            # Track 0: Core 0 [Host Linux / M33]
            {"id": "c0_sup", "track": 0, "name": "linux_supervisor", "start_us": 0.0, "dur_us": 45.0, "budget_us": 80.0, "overrun": False, "col": (0.2, 0.6, 0.9, 0.9), "file": "apps/gps_imu_app/src/main.cpp", "line": 34, "token": "DomainDispatcher::step() event loop", "pred": "Linux scheduler", "succ": "udp_sink", "diag": "Normal supervisor cadence (100 Hz)"},
            {"id": "c0_udp", "track": 0, "name": "udp_telemetry_sink", "start_us": 120.0, "dur_us": 28.0, "budget_us": 40.0, "overrun": False, "col": (0.3, 0.7, 1.0, 0.9), "file": "apps/gps_imu_app/src/main.cpp", "line": 216, "token": "sock.sendto(tlp_frame)", "pred": "telemetry_egress", "succ": "Network egress", "diag": "64B TLP emitted over UDP :9870"},
            {"id": "c0_dma", "track": 0, "name": "dma_bridge_worker", "start_us": 220.0, "dur_us": 18.0, "budget_us": 35.0, "overrun": False, "col": (0.2, 0.5, 0.8, 0.9), "file": "targets/allwinner_e907/main.cpp", "line": 47, "token": "remoteproc shared SRAM drain", "pred": "E907 Doorbell", "succ": "Linux user ring", "diag": "Zero-copy shared SRAM A3/C latch"},

            # Track 1: Core 1 [Coroutine Engine]
            {"id": "c1_imu1", "track": 1, "name": "imu_pipeline", "start_us": 15.0, "dur_us": 12.5, "budget_us": 15.0, "overrun": False, "col": (0.2, 0.8, 1.0, 0.9), "file": "apps/gps_imu_app/src/main.cpp", "line": 42, "token": "co_await g_sensor_ring.pop()", "pred": "spi0_dma_tc_isr", "succ": "attitude_ekf", "diag": "8 kHz primary pacer tick on schedule"},
            {"id": "c1_ekf1", "track": 1, "name": "attitude_ekf", "start_us": 35.0, "dur_us": 18.2, "budget_us": 20.0, "overrun": False, "col": (0.3, 0.9, 0.4, 0.9), "file": "include/abstractx/fusion/attitude_filter.hpp", "line": 88, "token": "attitude_ekf.update(gyro, accel)", "pred": "imu_pipeline", "succ": "flight_control", "diag": "Mahony quaternion kinematics convergence"},
            {"id": "c1_ctl1", "track": 1, "name": "flight_control", "start_us": 62.0, "dur_us": 22.0, "budget_us": 25.0, "overrun": False, "col": (1.0, 0.7, 0.2, 0.9), "file": "apps/gps_imu_app/src/main.cpp", "line": 98, "token": "quad_mixer.compute_demands(tau)", "pred": "attitude_ekf", "succ": "dshot_pulse", "diag": "Cascaded rate/attitude PID solver"},
            {"id": "c1_aux", "track": 1, "name": "mag_gps_drain", "start_us": 95.0, "dur_us": 6.5, "budget_us": 10.0, "overrun": False, "col": (0.4, 0.8, 0.6, 0.9), "file": "apps/gps_imu_app/src/main.cpp", "line": 55, "token": "while (g_mag_channel.try_pop(m))", "pred": "I2C ISR", "succ": "imu_pipeline", "diag": "Non-blocking auxiliary drain"},
            {"id": "imu_overrun", "track": 1, "name": "imu_pipeline [OVERRUN]", "start_us": 140.0, "dur_us": 23.4, "budget_us": 15.0, "overrun": True, "col": (1.0, 0.3, 0.3, 0.95), "file": "apps/gps_imu_app/src/main.cpp", "line": 42, "token": "co_await g_sensor_ring.pop() [OVERRUN: 23.4 µs vs 15.0 µs budget]", "pred": "spi0_dma_tc_isr", "succ": "attitude_ekf", "diag": "ALERT: Task overran 15.0 µs deadline by +8.4 µs due to lock-free ring head contention!"},
            {"id": "c1_ekf2", "track": 1, "name": "attitude_ekf", "start_us": 175.0, "dur_us": 14.1, "budget_us": 20.0, "overrun": False, "col": (0.3, 0.9, 0.4, 0.9), "file": "include/abstractx/fusion/attitude_filter.hpp", "line": 88, "token": "attitude_ekf.update(gyro, accel)", "pred": "imu_pipeline", "succ": "telemetry_egress", "diag": "Recovered baseline timing window"},

            # Track 2: SPU [FPGA Hardware Fabric]
            {"id": "spu_dma", "track": 2, "name": "spi0_auto_dma", "start_us": 2.0, "dur_us": 8.5, "budget_us": 10.0, "overrun": False, "col": (0.9, 0.5, 0.2, 0.9), "file": "include/abstractx/drivers/imu/icm42688p.hpp", "line": 54, "token": "Hardware Auto-DMA 14B Burst", "pred": "DRDY pin trigger", "succ": "spi0_dma_tc_isr", "diag": "Hardware SPI DMA burst @ 10 MHz"},
            {"id": "spu_rt", "track": 2, "name": "asp_router_crossbar", "start_us": 72.0, "dur_us": 4.2, "budget_us": 8.0, "overrun": False, "col": (0.8, 0.4, 1.0, 0.9), "file": "sim/cocotb/test_asp_sys_regs_cocotb.py", "line": 80, "token": "asp_router.sv AXI-Stream Crossbar", "pred": "Ring push", "succ": "Host shared SRAM", "diag": "64-byte TLP crossbar routing (zero-copy)"},
            {"id": "spu_dshot", "track": 2, "name": "dshot600_pulse", "start_us": 105.0, "dur_us": 16.0, "budget_us": 20.0, "overrun": False, "col": (0.7, 0.3, 0.9, 0.9), "file": "apps/gps_imu_app/src/main.cpp", "line": 152, "token": "4-CH DShot300/600 hardware burst", "pred": "flight_control", "succ": "Physical ESCs", "diag": "16-bit CRC hardware pulse generation"},

            # Track 3: Interrupts [Hardware PLIC & Mailbox Doorbells]
            {"id": "isr_spi", "track": 3, "name": "spi0_dma_tc_isr", "start_us": 10.5, "dur_us": 2.4, "budget_us": 4.0, "overrun": False, "col": (1.0, 0.4, 0.4, 0.9), "file": "include/abstractx/drivers/imu/icm42688p.hpp", "line": 54, "token": "PLIC ISR: SPI0 DMA Complete", "pred": "spi0_auto_dma", "succ": "imu_pipeline", "diag": "Interrupt response latency 0.8 µs"},
            {"id": "isr_mb", "track": 3, "name": "sun6i_msgbox_doorbell", "start_us": 52.0, "dur_us": 1.8, "budget_us": 3.0, "overrun": False, "col": (1.0, 0.5, 0.3, 0.9), "file": "targets/allwinner_e907/main.cpp", "line": 47, "token": "sun6i-msgbox hardware doorbell IRQ", "pred": "Core 0 signal", "succ": "dma_bridge_worker", "diag": "Inter-core doorbell latency 1.1 µs"},
            {"id": "isr_uart", "track": 3, "name": "uart0_gps_rx_isr", "start_us": 92.0, "dur_us": 3.1, "budget_us": 5.0, "overrun": False, "col": (0.9, 0.3, 0.5, 0.9), "file": "include/abstractx/drivers/gps/ublox_gps.hpp", "line": 55, "token": "UART0 FIFO RX threshold ISR", "pred": "U-Blox M10 byte", "succ": "mag_gps_drain", "diag": "UBX protocol packet decoded"},
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
                           q_sensor: float, q_telem: float):
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

            # Periodically update Tracealyzer chart metrics and simple trace events (at 20 Hz)
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

                g_state.push_chart_metrics(t, c0, c1, spu, lat_imu, lat_ekf, lat_ctl, lat_dma, q_sensor, q_telem)

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

def apply_docking_layout(preset: str):
    """
    # @impl [SPEC-STUDIO-01] tools/visualizer/abstractx_studio.py
    Dynamically switches docking layout presets to eliminate screen clutter:
    - 'balanced'        : Standard multi-pane overview workbench.
    - 'studio_workbench': Studio diagnostic workbench (Core + TLP + Log + Source + FPGA) with User Flight Canvas popped out.
    - 'user_focus'      : User Domain Instruments expanded to 100% full screen.
    - 'core_focus'      : AbstractX Core Studio expanded to 100% full screen.
    - 'source_focus'    : Source Code & Performance Inspector alongside Core Studio.
    - 'fpga_focus'      : FPGA & Hardware Peripherals alongside TLP Bus Debugger.
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
        if w_source: w_source.is_visible = True
        if w_fpga: w_fpga.is_visible = True
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
        if w_tlp: w_tlp.is_visible = False
        if w_log: w_log.is_visible = False
        if w_source: w_source.is_visible = False
        if w_fpga: w_fpga.is_visible = False
    elif preset == "source_focus":
        if w_core: w_core.is_visible = True
        if w_user: w_user.is_visible = False
        if w_tlp: w_tlp.is_visible = False
        if w_log: w_log.is_visible = False
        if w_source: w_source.is_visible = True
        if w_fpga: w_fpga.is_visible = False
    elif preset == "fpga_focus":
        if w_core: w_core.is_visible = False
        if w_user: w_user.is_visible = False
        if w_tlp: w_tlp.is_visible = True
        if w_log: w_log.is_visible = False
        if w_source: w_source.is_visible = False
        if w_fpga: w_fpga.is_visible = True

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
    continuous width & height pixel sliders, and a 1-click re-dock button.
    When docked: provides 1-click pop-out to its own dedicated canvas.
    """
    is_docked = imgui.is_window_docked()
    cur_size = imgui.get_window_size()

    imgui.begin_group()
    if not is_docked:
        imgui.text_colored(imgui.ImVec4(0.95, 0.82, 0.25, 1.0), "🗖 POPPED OUT CANVAS")
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
                imgui.set_window_size(imgui.ImVec2(float(w), float(h)), 0)
            imgui.same_line()

        # Continuous width & height sliders
        imgui.push_item_width(90)
        ch_w, new_w = imgui.slider_int(f"W##{window_key}", int(cur_size.x), 500, 2560)
        imgui.same_line()
        ch_h, new_h = imgui.slider_int(f"H##{window_key}", int(cur_size.y), 350, 1600)
        imgui.pop_item_width()
        if ch_w or ch_h:
            imgui.set_window_size(imgui.ImVec2(float(new_w), float(new_h)), 0)

        imgui.same_line(0, 14)
        if imgui.button(f"🗗 Re-Dock into Studio##{window_key}"):
            apply_docking_layout("balanced")
    else:
        # Window is docked inside workbench
        if imgui.button(f"🗖 Pop Out to Own Canvas##{window_key}"):
            decouple_window(window_title, default_w, default_h)
        imgui.same_line()
        imgui.text_colored(imgui.ImVec4(0.5, 0.6, 0.7, 0.8), "| Multi-monitor independent canvas")

    imgui.end_group()
    imgui.separator()

def _setup_studio_style():
    """
    Configures Dear ImGui style for AbstractX Studio:
    Increases window border hover padding to 10px so resizing floating/popped-out windows
    from any edge or corner is easy and forgiving.
    """
    style = imgui.get_style()
    style.window_border_hover_padding = 10.0
    style.window_border_size = 2.0

def _render_status_bar():
    """Renders bottom status bar with quick dynamic window layout presets and canvas pop-out."""
    if g_state.connected:
        imgui.text_colored(imgui.ImVec4(0.1, 0.9, 0.2, 1.0), " [ONLINE] ")
    else:
        imgui.text_colored(imgui.ImVec4(0.9, 0.2, 0.1, 1.0), " [OFFLINE / WAITING :9870] ")
    imgui.same_line()
    imgui.text(f"| Platform: {g_state.platform_name} ({g_state.platform_arch}) | Packets: {g_state.packet_count:,} | Rate: {g_state.fps_packet_rate} pkts/s | Dynamic Heap: 0 B")

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
        if pr == g_state.active_layout_preset:
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.6, 0.9, 1.0))
        if imgui.button(lbl):
            apply_docking_layout(pr)
        if pr == g_state.active_layout_preset:
            imgui.pop_style_color()
        imgui.same_line()

    imgui.same_line(0, 16)
    if imgui.button("🗖 Pop Out Flight Canvas"):
        decouple_window("User Domain Instruments", 1280.0, 820.0)

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
    """Renders Silicon Cores, Radial CPU Gauges, Platform Architecture, and CPU Load Line Chart."""
    # Top Section: 3 Radial CPU Dial Gauges
    imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), "[Silicon Processor Real-Time Load Gauges]")
    imgui.columns(3, "cpu_gauge_cols", False)
    
    # Col 1: Core 0
    cur_pos = imgui.get_cursor_screen_pos()
    col_w = imgui.get_column_width()
    draw_radial_gauge(cur_pos.x + col_w / 2.0, cur_pos.y + 45.0, 36.0, 
                      g_state.linux_total_cpu, "Core 0 (Host)", f"Linux {g_state.linux_total_cpu:.1f}%")
    imgui.dummy(imgui.ImVec2(col_w, 120.0))
    imgui.next_column()

    # Col 2: Core 1
    cur_pos = imgui.get_cursor_screen_pos()
    col_w = imgui.get_column_width()
    draw_radial_gauge(cur_pos.x + col_w / 2.0, cur_pos.y + 45.0, 36.0, 
                      g_state.e907_active_duty_pct, "Core 1 (Coro)", f"Duty {g_state.e907_active_duty_pct:.1f}%")
    imgui.dummy(imgui.ImVec2(col_w, 120.0))
    imgui.next_column()

    # Col 3: SPU
    cur_pos = imgui.get_cursor_screen_pos()
    col_w = imgui.get_column_width()
    draw_radial_gauge(cur_pos.x + col_w / 2.0, cur_pos.y + 45.0, 36.0, 
                      g_state.fpga_lut_utilization_pct, "SPU (FPGA)", f"Logic {g_state.fpga_lut_utilization_pct:.1f}%")
    imgui.dummy(imgui.ImVec2(col_w, 120.0))
    imgui.next_column()

    imgui.columns(1)
    imgui.separator()

    # Line Chart: Per-Processor CPU Load History (Last 30s)
    if implot.begin_plot("Silicon Cores CPU Load History (Last 30s)", imgui.ImVec2(-1, 180)):
        implot.setup_axes("Time (s)", "CPU / Duty (%)", implot.AxisFlags_.auto_fit, implot.AxisFlags_.none)
        implot.setup_axis_limits(implot.ImAxis_.y1, 0.0, 100.0, imgui.Cond_.always)
        with g_state.lock:
            t_data = np.copy(g_state.chart_time)
            c0_data = np.copy(g_state.chart_cpu_c0)
            c1_data = np.copy(g_state.chart_cpu_c1)
            spu_data = np.copy(g_state.chart_cpu_spu)
        implot.plot_line("Core 0 (Host Linux / M33)", t_data, c0_data)
        implot.plot_line("Core 1 (Coroutine Engine)", t_data, c1_data)
        implot.plot_line("SPU (FPGA Switch Fabric)", t_data, spu_data)
        implot.end_plot()

    imgui.separator()
    # Middle Section: Platform Topology & Processing Roles
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

def _render_tracealyzer_and_charts():
    """
    # @impl [SPEC-STUDIO-02] tools/visualizer/abstractx_studio.py
    Renders FreeRTOS Tracealyzer style Multi-Track Execution Timing Diagram,
    Interactive Issue Drill-Down Inspector, and Synchronized Real-Time Line Charts.
    """
    imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), "Tracealyzer Multi-Track Timing Diagram & Issue Drill-Down")
    imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0),
                       "Deterministic execution swimlanes across silicon cores. Click any task span or overrun to drill down.")
    imgui.separator()

    # Toolbar: Zoom controls, Pan scrubber, Pause & Overrun Filters, Jump to Issue
    imgui.begin_group()
    imgui.text_colored(imgui.ImVec4(0.9, 0.7, 0.2, 1.0), "Timeline Controls:")
    imgui.same_line()
    zoom_levels = [0.5, 1.0, 2.0, 4.0]
    for z in zoom_levels:
        if abs(g_state.timing_zoom - z) < 0.05:
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.6, 0.9, 1.0))
        if imgui.button(f"{z}x"):
            g_state.timing_zoom = z
        if abs(g_state.timing_zoom - z) < 0.05:
            imgui.pop_style_color()
        imgui.same_line()

    imgui.same_line(0, 12)
    imgui.set_next_item_width(140)
    changed_pan, g_state.timing_pan_us = imgui.slider_float("Pan (µs)", g_state.timing_pan_us, 0.0, 200.0, "%.0f µs")
    imgui.same_line(0, 12)
    _, g_state.timing_paused = imgui.checkbox("Freeze Timeline", g_state.timing_paused)
    imgui.same_line(0, 10)
    _, g_state.timing_show_overruns_only = imgui.checkbox("Overruns Only", g_state.timing_show_overruns_only)

    imgui.same_line(0, 15)
    # Quick Jump to Overrun button (Styled in alert red/coral)
    imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.85, 0.25, 0.25, 0.9))
    imgui.push_style_color(imgui.Col_.button_hovered, imgui.ImVec4(1.0, 0.35, 0.35, 1.0))
    if imgui.button("🚨 Jump to Overrun (+8.4µs)"):
        g_state.timing_pan_us = 50.0
        g_state.timing_zoom = 1.2
        g_state.selected_span_id = "imu_overrun"
        g_state.selected_event_name = "imu_pipeline [OVERRUN]"
        g_state.selected_source_file = "apps/gps_imu_app/src/main.cpp"
        g_state.selected_source_line = 42
        g_state.selected_token = "co_await g_sensor_ring.pop() [OVERRUN: 23.4 µs vs 15.0 µs budget]"
    imgui.pop_style_color(2)

    imgui.same_line(0, 8)
    if imgui.button("Reset View"):
        g_state.timing_pan_us = 0.0
        g_state.timing_zoom = 1.0
        g_state.timing_show_overruns_only = False
    imgui.end_group()
    imgui.separator()

    # Multi-Track Swimlanes (Tracealyzer style)
    tracks = [
        {"id": 0, "name": "Core 0", "sub": "Host Linux / M33", "col": imgui.ImVec4(0.2, 0.6, 0.9, 1.0)},
        {"id": 1, "name": "Core 1", "sub": "Coroutine Engine", "col": imgui.ImVec4(0.2, 0.9, 0.5, 1.0)},
        {"id": 2, "name": "SPU", "sub": "FPGA Switch & DMA", "col": imgui.ImVec4(0.8, 0.4, 1.0, 1.0)},
        {"id": 3, "name": "Interrupts", "sub": "PLIC & Doorbells", "col": imgui.ImVec4(1.0, 0.4, 0.4, 1.0)},
    ]

    sidebar_w = 160.0
    ruler_h = 24.0
    track_h = 32.0
    total_canvas_h = ruler_h + len(tracks) * track_h + 8.0

    canvas_pos = imgui.get_cursor_screen_pos()
    avail_w = imgui.get_content_region_avail().x
    timeline_w = max(avail_w - sidebar_w - 12.0, 240.0)

    draw_list = imgui.get_window_draw_list()

    # Base background for timing canvas
    p_min = canvas_pos
    p_max = imgui.ImVec2(canvas_pos.x + avail_w, canvas_pos.y + total_canvas_h)
    draw_list.add_rect_filled(p_min, p_max, imgui.get_color_u32(imgui.ImVec4(0.06, 0.09, 0.14, 0.95)), 4.0)
    draw_list.add_rect(p_min, p_max, imgui.get_color_u32(imgui.ImVec4(0.2, 0.28, 0.38, 0.8)), 4.0, 1.0)

    # Time scale & calculations
    visible_us = 260.0 / max(g_state.timing_zoom, 0.1)
    scale_px = timeline_w / visible_us
    t_min = g_state.timing_pan_us
    t_max = t_min + visible_us

    timeline_start_x = canvas_pos.x + sidebar_w

    # Draw Time Ruler at the top
    ruler_bg_min = imgui.ImVec2(timeline_start_x, canvas_pos.y)
    ruler_bg_max = imgui.ImVec2(canvas_pos.x + avail_w, canvas_pos.y + ruler_h)
    draw_list.add_rect_filled(ruler_bg_min, ruler_bg_max, imgui.get_color_u32(imgui.ImVec4(0.1, 0.15, 0.22, 0.9)), 0.0)
    draw_list.add_line(imgui.ImVec2(canvas_pos.x, canvas_pos.y + ruler_h),
                       imgui.ImVec2(canvas_pos.x + avail_w, canvas_pos.y + ruler_h),
                       imgui.get_color_u32(imgui.ImVec4(0.3, 0.4, 0.5, 0.8)), 1.0)

    # Draw Ruler Ticks and Vertical Grid Lines
    tick_step = 25.0 if visible_us <= 150.0 else (50.0 if visible_us <= 350.0 else 100.0)
    curr_tick = (int(t_min / tick_step)) * tick_step
    while curr_tick <= t_max + tick_step:
        x_tick = timeline_start_x + (curr_tick - t_min) * scale_px
        if timeline_start_x <= x_tick <= canvas_pos.x + avail_w:
            # Vertical grid line across all tracks
            draw_list.add_line(imgui.ImVec2(x_tick, canvas_pos.y + ruler_h),
                               imgui.ImVec2(x_tick, canvas_pos.y + total_canvas_h - 4.0),
                               imgui.get_color_u32(imgui.ImVec4(0.25, 0.35, 0.45, 0.25)), 1.0)
            # Ruler tick mark & label
            draw_list.add_line(imgui.ImVec2(x_tick, canvas_pos.y + ruler_h - 6.0),
                               imgui.ImVec2(x_tick, canvas_pos.y + ruler_h),
                               imgui.get_color_u32(imgui.ImVec4(0.6, 0.7, 0.8, 0.9)), 1.0)
            draw_list.add_text(imgui.ImVec2(x_tick + 3.0, canvas_pos.y + 4.0),
                               imgui.get_color_u32(imgui.ImVec4(0.7, 0.8, 0.9, 0.9)),
                               f"+{curr_tick:.0f}µs")
        curr_tick += tick_step

    # Draw Swimlane Rows
    tracks_y = canvas_pos.y + ruler_h
    for trk in tracks:
        row_y0 = tracks_y + trk["id"] * track_h
        row_y1 = row_y0 + track_h
        row_bg = imgui.ImVec4(0.08, 0.12, 0.18, 0.6) if trk["id"] % 2 == 0 else imgui.ImVec4(0.05, 0.08, 0.12, 0.6)
        draw_list.add_rect_filled(imgui.ImVec2(canvas_pos.x, row_y0), imgui.ImVec2(canvas_pos.x + avail_w, row_y1),
                                  imgui.get_color_u32(row_bg), 0.0)
        draw_list.add_line(imgui.ImVec2(canvas_pos.x, row_y1), imgui.ImVec2(canvas_pos.x + avail_w, row_y1),
                           imgui.get_color_u32(imgui.ImVec4(0.2, 0.25, 0.35, 0.4)), 1.0)

        # Track Label in Sidebar
        draw_list.add_text(imgui.ImVec2(canvas_pos.x + 8.0, row_y0 + 4.0),
                           imgui.get_color_u32(trk["col"]), f"[{trk['name']}]")
        draw_list.add_text(imgui.ImVec2(canvas_pos.x + 8.0, row_y0 + 17.0),
                           imgui.get_color_u32(imgui.ImVec4(0.5, 0.6, 0.7, 0.8)), trk["sub"])

    # Draw Task Execution Spans (Clipped to Timeline Area)
    draw_list.push_clip_rect(imgui.ImVec2(timeline_start_x, tracks_y),
                             imgui.ImVec2(canvas_pos.x + avail_w - 4.0, canvas_pos.y + total_canvas_h - 4.0), True)

    with g_state.lock:
        spans = list(g_state.timing_spans)
        sel_span_id = g_state.selected_span_id
        show_overruns_only = g_state.timing_show_overruns_only

    hovered_span = None

    for sp in spans:
        if show_overruns_only and not sp["overrun"]:
            continue
        
        # Calculate screen coordinates
        x0 = timeline_start_x + (sp["start_us"] - t_min) * scale_px
        x1 = x0 + max(sp["dur_us"] * scale_px, 6.0)
        y0 = tracks_y + sp["track"] * track_h + 3.0
        y1 = y0 + track_h - 6.0

        if x1 < timeline_start_x or x0 > canvas_pos.x + avail_w:
            continue

        is_selected = (sp["id"] == sel_span_id)
        is_overrun = sp["overrun"]

        # Color scheme
        if is_overrun:
            fill_col = imgui.ImVec4(0.92, 0.25, 0.25, 0.95)
            border_col = imgui.ImVec4(1.0, 0.95, 0.4, 1.0) if is_selected else imgui.ImVec4(1.0, 0.6, 0.6, 0.9)
            border_thick = 2.5 if is_selected else 1.8
        else:
            c = sp["col"]
            fill_col = imgui.ImVec4(c[0], c[1], c[2], 0.85)
            border_col = imgui.ImVec4(1.0, 0.95, 0.4, 1.0) if is_selected else imgui.ImVec4(1.0, 1.0, 1.0, 0.3)
            border_thick = 2.0 if is_selected else 1.0

        # Draw Span Box
        draw_list.add_rect_filled(imgui.ImVec2(x0, y0), imgui.ImVec2(x1, y1), imgui.get_color_u32(fill_col), 3.0)
        draw_list.add_rect(imgui.ImVec2(x0, y0), imgui.ImVec2(x1, y1), imgui.get_color_u32(border_col), 3.0, border_thick)

        # Label inside span if wide enough
        box_w = x1 - x0
        if box_w >= 28.0:
            prefix = "⚠️ " if is_overrun else ""
            txt = f"{prefix}{sp['name']}"
            if box_w < 70.0 and len(txt) > 8:
                txt = txt[:8] + ".."
            draw_list.add_text(imgui.ImVec2(x0 + 4.0, y0 + (track_h - 6.0 - 14.0) * 0.5),
                               imgui.get_color_u32(imgui.ImVec4(1.0, 1.0, 1.0, 0.95)), txt)

        # Check Mouse Hover & Click
        if imgui.is_mouse_hovering_rect(imgui.ImVec2(x0, y0), imgui.ImVec2(x1, y1)):
            hovered_span = sp
            if imgui.is_mouse_clicked(0):
                with g_state.lock:
                    g_state.selected_span_id = sp["id"]
                    g_state.selected_event_name = sp["name"]
                    g_state.selected_source_file = sp["file"]
                    g_state.selected_source_line = sp["line"]
                    g_state.selected_token = sp["token"]

    draw_list.pop_clip_rect()

    # Advance ImGui layout cursor beyond the custom canvas
    imgui.dummy(imgui.ImVec2(-1, total_canvas_h))

    # Render Hover Tooltip
    if hovered_span is not None:
        imgui.begin_tooltip()
        trk_name = tracks[hovered_span["track"]]["name"]
        if hovered_span["overrun"]:
            imgui.text_colored(imgui.ImVec4(1.0, 0.3, 0.3, 1.0), f"🚨 [OVERRUN] {hovered_span['name']}")
        else:
            imgui.text_colored(imgui.ImVec4(0.3, 0.9, 0.4, 1.0), f"✓ {hovered_span['name']}")
        imgui.separator()
        imgui.text(f"Domain    : {trk_name} ({tracks[hovered_span['track']]['sub']})")
        imgui.text(f"Timestamp : t = +{hovered_span['start_us']:.1f} µs")
        imgui.text(f"Duration  : {hovered_span['dur_us']:.1f} µs  (Budget: {hovered_span['budget_us']:.1f} µs)")
        if hovered_span["overrun"]:
            imgui.text_colored(imgui.ImVec4(1.0, 0.4, 0.4, 1.0),
                               f"Overrun   : +{hovered_span['dur_us'] - hovered_span['budget_us']:.1f} µs (+{(hovered_span['dur_us'] / hovered_span['budget_us'] - 1.0) * 100:.1f}%)")
        else:
            imgui.text_colored(imgui.ImVec4(0.4, 0.9, 0.5, 1.0),
                               f"Margin    : {hovered_span['budget_us'] - hovered_span['dur_us']:.1f} µs within limit")
        imgui.separator()
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), f"Predecessor: {hovered_span['pred']}")
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), f"Successor  : {hovered_span['succ']}")
        imgui.text_colored(imgui.ImVec4(1.0, 0.9, 0.4, 1.0), "👉 Click to inspect issue & C++ source")
        imgui.end_tooltip()

    imgui.spacing()
    # Find selected span
    sel_sp = next((s for s in spans if s["id"] == sel_span_id), spans[4])

    # Header Alert Card
    if sel_sp["overrun"]:
        imgui.push_style_color(imgui.Col_.child_bg, imgui.ImVec4(0.25, 0.08, 0.08, 0.7))
        imgui.begin_child("IssueAlertHeader", imgui.ImVec2(-1, 32), True)
        imgui.text_colored(imgui.ImVec4(1.0, 0.35, 0.35, 1.0),
                           f"🚨 CRITICAL TIMING OVERRUN: '{sel_sp['name']}' exceeded {sel_sp['budget_us']:.1f} µs deadline by +{sel_sp['dur_us'] - sel_sp['budget_us']:.1f} µs ({(sel_sp['dur_us'] / sel_sp['budget_us']) * 100.0:.1f}% budget)")
        imgui.end_child()
        imgui.pop_style_color()
    else:
        imgui.push_style_color(imgui.Col_.child_bg, imgui.ImVec4(0.08, 0.2, 0.12, 0.7))
        imgui.begin_child("IssueAlertHeader", imgui.ImVec2(-1, 32), True)
        imgui.text_colored(imgui.ImVec4(0.3, 0.95, 0.5, 1.0),
                           f"✅ NOMINAL EXECUTION: '{sel_sp['name']}' completed within deadline ({sel_sp['dur_us']:.1f} µs / {sel_sp['budget_us']:.1f} µs, {(sel_sp['dur_us'] / sel_sp['budget_us']) * 100.0:.1f}% budget)")
        imgui.end_child()
        imgui.pop_style_color()

    # Drill-Down Breakdown (2 Columns)
    imgui.columns(2, "drill_down_cols", True)

    # Left Column: Metrics & Budget Utilization
    imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), "[Execution Timing Metrics]")
    imgui.text(f"Task Name     : {sel_sp['name']}")
    imgui.text(f"Execution Time: {sel_sp['dur_us']:.1f} µs")
    imgui.text(f"Deadline Limit: {sel_sp['budget_us']:.1f} µs")
    if sel_sp["overrun"]:
        imgui.text_colored(imgui.ImVec4(1.0, 0.3, 0.3, 1.0), f"Deadline Delta: +{sel_sp['dur_us'] - sel_sp['budget_us']:.1f} µs (VIOLATION)")
    else:
        imgui.text_colored(imgui.ImVec4(0.3, 0.9, 0.4, 1.0), f"Deadline Delta: -{sel_sp['budget_us'] - sel_sp['dur_us']:.1f} µs (Margin OK)")
    
    util_ratio = sel_sp["dur_us"] / max(sel_sp["budget_us"], 0.1)
    bar_prog = min(util_ratio, 1.0)
    bar_lbl = f"{util_ratio * 100.0:.1f}% Budget"
    if util_ratio > 1.0:
        imgui.push_style_color(imgui.Col_.plot_histogram, imgui.ImVec4(0.9, 0.2, 0.2, 1.0))
    elif util_ratio > 0.8:
        imgui.push_style_color(imgui.Col_.plot_histogram, imgui.ImVec4(0.9, 0.7, 0.2, 1.0))
    else:
        imgui.push_style_color(imgui.Col_.plot_histogram, imgui.ImVec4(0.2, 0.8, 0.4, 1.0))
    imgui.progress_bar(bar_prog, imgui.ImVec2(-1, 20), bar_lbl)
    imgui.pop_style_color()

    imgui.next_column()

    # Right Column: Tracealyzer Causality Chain & Diagnostic
    imgui.text_colored(imgui.ImVec4(0.9, 0.7, 0.2, 1.0), "[Tracealyzer Causality Chain]")
    # Flow breadcrumbs
    imgui.text_colored(imgui.ImVec4(0.5, 0.7, 0.9, 1.0), f"1. Trigger / Predecessor : {sel_sp['pred']}")
    active_col = imgui.ImVec4(1.0, 0.3, 0.3, 1.0) if sel_sp["overrun"] else imgui.ImVec4(0.3, 0.9, 0.4, 1.0)
    imgui.text_colored(active_col, f"2. Active Task Execution : {sel_sp['name']}  ──▶")
    imgui.text_colored(imgui.ImVec4(0.5, 0.7, 0.9, 1.0), f"3. Delayed / Successor   : {sel_sp['succ']}")
    imgui.spacing()
    imgui.text_colored(imgui.ImVec4(1.0, 0.85, 0.3, 1.0), "[Root Cause Diagnosis]")
    imgui.bullet_text(sel_sp["diag"])

    imgui.columns(1)
    imgui.separator()

    # Interactive Source Code Scanner & Context Preview
    imgui.text_colored(imgui.ImVec4(1.0, 1.0, 0.2, 1.0),
                       f">> [C++ Source Code Inspector] {sel_sp['file']}:{sel_sp['line']}")
    imgui.begin_child("TracealyzerSourcePreview", imgui.ImVec2(-1, 88), True)
    imgui.text_colored(imgui.ImVec4(0.5, 0.5, 0.5, 1.0), f"// Source: {sel_sp['file']}")
    imgui.text_colored(imgui.ImVec4(0.5, 0.5, 0.5, 1.0), f"   {sel_sp['line'] - 1}:   // Processing event loop")
    hl_col = imgui.ImVec4(1.0, 0.3, 0.3, 1.0) if sel_sp["overrun"] else imgui.ImVec4(0.2, 1.0, 0.4, 1.0)
    imgui.text_colored(hl_col, f"-> {sel_sp['line']}:       {sel_sp['token']};")
    imgui.text_colored(imgui.ImVec4(0.5, 0.5, 0.5, 1.0), f"   {sel_sp['line'] + 1}:   attitude_ekf.update(sample.gyro, sample.accel);")
    imgui.end_child()
    imgui.separator()

    # Line Chart 1: Coroutine Task Latency & Suspension History with Deadline Limits
    if implot.begin_plot("Tracealyzer Coroutine Latency & Suspension Duration (µs)", imgui.ImVec2(-1, 180)):
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

    # Line Chart 2: SPSC Lock-Free Interconnect Queue Saturation
    if implot.begin_plot("SPSC Lock-Free Interconnect Saturation (pkts / 64)", imgui.ImVec2(-1, 150)):
        implot.setup_axes("Time Window (s)", "Queue Depth", implot.AxisFlags_.auto_fit, implot.AxisFlags_.none)
        implot.setup_axis_limits(implot.ImAxis_.y1, 0.0, 64.0, imgui.Cond_.always)
        with g_state.lock:
            q_sensor = np.copy(g_state.chart_ring_sensor)
            q_telem = np.copy(g_state.chart_ring_telem)
        implot.plot_line("Sensor Queue (g_sensor_ring)", t_data, q_sensor)
        implot.plot_line("Telemetry Queue (g_telemetry_ring)", t_data, q_telem)
        # 75% High Watermark Warning Reference (48 pkts)
        t_ref = np.array([-30.0, 0.0], dtype=np.float64)
        q_warn = np.array([48.0, 48.0], dtype=np.float64)
        implot.plot_line("Warning Limit (75% = 48 pkts)", t_ref, q_warn)
        implot.end_plot()

def _render_simple_trace_view():
    """Renders strace / Tracealyzer style simple execution trace table with click-to-inspect."""
    imgui.begin_group()
    cores = ["ALL", "Core 0", "Core 1", "SPU", "ISR"]
    for c in cores:
        if c == g_state.simple_trace_filter_core:
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.6, 0.9, 1.0))
        if imgui.button(c):
            g_state.simple_trace_filter_core = c
        if c == g_state.simple_trace_filter_core:
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

def _render_level1_memory():
    """
    # @impl [SPEC-STUDIO-10] tools/visualizer/abstractx_studio.py
    Renders Level 1 Core: Memory Observability, Static Section Budgets, and MemBrowse Status.
    """
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

def _render_core_studio_window():
    """
    # @impl [SPEC-STUDIO-02] tools/visualizer/abstractx_studio.py
    Renders Window 1: AbstractX Core Studio (Platform, CPU, Timeline, MemBrowse).
    """
    g_state.update_rates()
    imgui.begin_group()
    if g_state.active_layout_preset == "core_focus":
        if imgui.button("🗗 Restore All Panes##core"):
            apply_docking_layout("balanced")
    else:
        if imgui.button("⛶ Expand Window##core"):
            apply_docking_layout("core_focus")
    imgui.same_line()
    if imgui.button("🗖 Pop Out Window##core"):
        decouple_window("AbstractX Core Studio")
    imgui.same_line()
    imgui.text_colored(imgui.ImVec4(0.5, 0.6, 0.7, 0.8), "| Multi-monitor decoupling enabled")
    imgui.end_group()
    imgui.separator()

    if imgui.begin_tab_bar("CoreStudioTabBar"):
        if imgui.begin_tab_item("CPU Gauges & Topology")[0]:
            _render_core_cpu_and_topology()
            imgui.end_tab_item()

        if imgui.begin_tab_item("Tracealyzer & Line Charts")[0]:
            _render_tracealyzer_and_charts()
            imgui.end_tab_item()

        if imgui.begin_tab_item("Simple Trace Viewer")[0]:
            _render_simple_trace_view()
            imgui.end_tab_item()

        if imgui.begin_tab_item("Dual-Plane Timeline")[0]:
            _render_core_timeline()
            imgui.end_tab_item()

        if imgui.begin_tab_item("MemBrowse Memory")[0]:
            _render_level1_memory()
            imgui.end_tab_item()

        imgui.end_tab_bar()

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
    if imgui.button("🗖 Pop Out##tlp"):
        decouple_window("TLP Bus Debugger")
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
        if lvl == g_state.log_filter_level:
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.6, 0.9, 1.0))
        if imgui.button(lvl):
            g_state.log_filter_level = lvl
        if lvl == g_state.log_filter_level:
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
    Renders Window 5: Source Code Performance & Hotspot Inspector.
    """
    g_state.update_rates()
    imgui.begin_group()
    # File selector combo
    files = g_state.source_code_files
    curr_f = files[g_state.selected_code_file_idx]
    imgui.set_next_item_width(320)
    if imgui.begin_combo("Source File", curr_f.split("/")[-1]):
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
    if imgui.button("🚨 Jump to Overrun Hotspot (Line 42)"):
        g_state.selected_code_file_idx = 0
        g_state.selected_source_file = "apps/gps_imu_app/src/main.cpp"
        g_state.selected_source_line = 42
        g_state.selected_span_id = "imu_overrun"
        g_state.selected_event_name = "imu_pipeline [OVERRUN]"
    imgui.pop_style_color()

    imgui.same_line(0, 10)
    if imgui.button("⛶ Expand Window##source"):
        apply_docking_layout("source_focus")
    imgui.same_line(0, 6)
    if imgui.button("🗖 Pop Out##source"):
        decouple_window("Source Code & Performance Inspector")

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
        ]
    }

    curr_lines = code_snippets.get(curr_f, code_snippets["apps/gps_imu_app/src/main.cpp"])

    imgui.columns(3, "code_inspect_cols", True)
    imgui.set_column_width(0, 50)
    imgui.set_column_width(1, 460)
    imgui.text("Line")
    imgui.next_column()
    imgui.text("C++ Source Code Context")
    imgui.next_column()
    imgui.text("Execution Metrics & Profiling Badges")
    imgui.next_column()
    imgui.separator()

    for line_no, code_txt, prof in curr_lines:
        is_sel_line = (line_no == g_state.selected_source_line and curr_f == g_state.selected_source_file)
        
        # Line number column
        num_str = f"{line_no:4d}"
        if prof and prof["overrun"]:
            imgui.text_colored(imgui.ImVec4(1.0, 0.3, 0.3, 1.0), f"🚨{num_str}")
        elif is_sel_line:
            imgui.text_colored(imgui.ImVec4(1.0, 0.9, 0.2, 1.0), f"👉{num_str}")
        else:
            imgui.text_colored(imgui.ImVec4(0.4, 0.5, 0.6, 1.0), num_str)
        imgui.next_column()

        # Code text column
        if prof and prof["overrun"]:
            imgui.text_colored(imgui.ImVec4(1.0, 0.4, 0.4, 1.0), code_txt)
        elif is_sel_line:
            imgui.text_colored(imgui.ImVec4(0.3, 1.0, 0.5, 1.0), code_txt)
        else:
            imgui.text(code_txt)
        imgui.next_column()

        # Profiling badges column
        if prof is not None:
            if prof["overrun"]:
                imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.9, 0.2, 0.2, 0.9))
                badge_lbl = f"⚠️ {prof['lat']:.1f} µs [OVERRUN +{prof['lat'] - prof['budget']:.1f} µs]"
            else:
                imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.2, 0.6, 0.3, 0.8))
                badge_lbl = f"✓ {prof['lat']:.1f} µs [Limit: {prof['budget']:.1f} µs]"

            if imgui.button(f"{badge_lbl}##btn_{line_no}"):
                with g_state.lock:
                    g_state.selected_source_line = line_no
                    g_state.selected_source_file = curr_f
                    g_state.selected_token = prof["token"]
                    if prof["overrun"]:
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
    if imgui.button("🗖 Pop Out##fpga"):
        decouple_window("FPGA & Hardware Peripherals")
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

        opened, _ = imgui.begin_tab_item("TLP Bus Debugger", None, 0)
        if opened:
            _render_tlp_debugger_window()
            imgui.end_tab_item()

        opened, _ = imgui.begin_tab_item("System Event Log", None, 0)
        if opened:
            _render_event_log_window()
            imgui.end_tab_item()

        opened, _ = imgui.begin_tab_item("Source Code Inspector", None, 0)
        if opened:
            _render_source_inspector_window()
            imgui.end_tab_item()

        opened, _ = imgui.begin_tab_item("FPGA Peripherals", None, 0)
        if opened:
            _render_fpga_peripherals_window()
            imgui.end_tab_item()

        opened, _ = imgui.begin_tab_item("MemBrowse Memory", None, flags_memory)
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
    Creates and configures HelloImGui 6-window dynamic docking layout for AbstractX Studio.
    Defaults to single dedicated OS window for rock-solid portability across Windows and Linux,
    with support for internal floating windows with dedicated size controls and presets.
    """
    runner_params = hello_imgui.RunnerParams()
    runner_params.app_window_params.window_title = "AbstractX Studio & User Domain Workbench"
    runner_params.app_window_params.window_geometry.size = (1560, 920)

    # Enable full screen docking layout
    runner_params.imgui_window_params.default_imgui_window_type = (
        hello_imgui.DefaultImGuiWindowType.provide_full_screen_dock_space
    )
    runner_params.imgui_window_params.enable_viewports = enable_viewports
    runner_params.imgui_window_params.show_menu_bar = True
    runner_params.imgui_window_params.show_menu_view = True
    runner_params.imgui_window_params.show_status_bar = True
    runner_params.imgui_window_params.menu_app_title = "AbstractX"

    # Define the 6 dedicated dockable windows
    win_user = hello_imgui.DockableWindow()
    win_user.label = "User Domain Instruments"
    win_user.dock_space_name = "MainDockSpace"
    win_user.window_size = imgui.ImVec2(1280, 820)
    win_user.gui_function = _render_user_domain_window

    win_core = hello_imgui.DockableWindow()
    win_core.label = "AbstractX Core Studio"
    win_core.dock_space_name = "LeftSpace"
    win_core.gui_function = _render_core_studio_window

    win_tlp = hello_imgui.DockableWindow()
    win_tlp.label = "TLP Bus Debugger"
    win_tlp.dock_space_name = "BottomSpace"
    win_tlp.gui_function = _render_tlp_debugger_window

    win_log = hello_imgui.DockableWindow()
    win_log.label = "System Event Log"
    win_log.dock_space_name = "BottomRightSpace"
    win_log.gui_function = _render_event_log_window

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
    # 1. LeftSpace (36% width) on the left for AbstractX Core Studio
    split_left = hello_imgui.DockingSplit()
    split_left.initial_dock = "MainDockSpace"
    split_left.new_dock = "LeftSpace"
    split_left.direction = imgui.Dir_.left
    split_left.ratio = 0.36

    # 2. BottomSpace (36% height) at the bottom for TLP Debugger & Event Log
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
    return runner_params

if __name__ == "__main__":
    main()

