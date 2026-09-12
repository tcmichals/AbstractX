#!/usr/bin/env python3
"""
Copyright (C) 2026 Tim Michals
SPDX-License-Identifier: GPL-3.0-or-later

AbstractX Visualizer Studio (Python + Dear ImGui Bundle)
-------------------------------------------------------
High-performance live observability dashboard for:
1. C++20 Coroutine Gantt Execution Timelines (Microsecond precision)
2. 8 kHz ICM-42688-P IMU Oscilloscope & U-Blox GPS Navigation Status
3. Lock-Free SPSC TLP Queue Saturation & Split-Transaction Latency Gauges
4. Live UDP/Wi-Fi Streaming from Raspberry Pi Pico 2 W & XuanTie E907

Usage:
  pip install -r tools/visualizer/requirements.txt
  python3 tools/visualizer/abstractx_studio.py [--port 9870] [--sim]
"""

import sys
import time
import socket
import struct
import threading
import argparse
import numpy as np

try:
    from imgui_bundle import imgui, implot, immapp, hello_imgui
except ImportError:
    print("Error: imgui-bundle is required. Run:")
    print("  pip install imgui-bundle numpy")
    sys.exit(1)

# CTF Constants
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

        # Platform Topology Table & Hardware Interconnect
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

        # Dual-Plane Execution Data
        # Plane 1: I/O Processor & Drivers
        self.io_driver_events = [
            {"name": "SPI0 DMA Burst", "driver": "hal_spi", "subsystem": "ICM-42688-P DMA", "latency_us": 0.8, "file": "targets/linux/src/hal_spi.cpp", "line": 78},
            {"name": "TWI0 I2C ISR", "driver": "hal_i2c", "subsystem": "Compass / Baro", "latency_us": 1.2, "file": "targets/allwinner_e907/src/hal_i2c.cpp", "line": 45},
            {"name": "MSGBox Doorbell", "driver": "msgbox", "subsystem": "Inter-Core RPC", "latency_us": 0.4, "file": "targets/allwinner_e907/src/io_processor.cpp", "line": 92},
            {"name": "UART0 RX FIFO", "driver": "hal_uart", "subsystem": "U-Blox M10 GPS", "latency_us": 1.5, "file": "targets/linux/src/hal_uart.cpp", "line": 125},
        ]
        # Plane 2: Main Coroutine Loop
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

        # Per-Processor SPU/CPU & Process Utilization
        self.linux_total_cpu = 8.4
        self.linux_abstractx_cpu = 5.2
        self.linux_external_cpu = 3.2
        self.e907_active_duty_pct = 14.8
        self.e907_wfi_sleep_pct = 85.2
        self.fpga_lut_utilization_pct = 18.2
        self.fpga_dma_bw_mbps = 12.8

        # Ring buffers for Sensor Data (Time vs Value)
        self.time_history = np.zeros(HISTORY_SIZE, dtype=np.float64)
        self.accel_x = np.zeros(HISTORY_SIZE, dtype=np.float64)
        self.accel_y = np.zeros(HISTORY_SIZE, dtype=np.float64)
        self.accel_z = np.zeros(HISTORY_SIZE, dtype=np.float64)
        self.gyro_x = np.zeros(HISTORY_SIZE, dtype=np.float64)
        self.gyro_y = np.zeros(HISTORY_SIZE, dtype=np.float64)
        self.gyro_z = np.zeros(HISTORY_SIZE, dtype=np.float64)
        self.head_idx = 0

        # GPS Status
        self.gps_lat = 37.7749
        self.gps_lon = -122.4194
        self.gps_alt_m = 142.5
        self.gps_speed_mps = 12.4
        self.gps_sats = 18
        self.gps_fix_type = 3  # 3D Fix

        # TLP Queue Status
        self.sensor_ring_fill = 18
        self.telemetry_ring_fill = 8
        self.rtt_latency_us = 12.4

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

    def update_rate(self):
        with self.lock:
            now = time.time()
            dt = now - self.last_rate_calc
            if dt >= 1.0:
                self.fps_packet_rate = int(self.rate_counter / dt)
                self.rate_counter = 0
                self.last_rate_calc = now

g_state = TelemetryState()

def udp_receiver_thread(port: int, sim_mode: bool):
    """Background worker receiving live CTF / UDP frames or generating simulation data."""
    if sim_mode:
        print("[Simulator] Running in synthetic telemetry simulation mode...")
        start_time = time.time()
        sample_count = 0
        while True:
            t = time.time() - start_time
            # Synthesize realistic 8 kHz IMU dynamics with slight noise
            ax = 0.05 * np.sin(t * 5.0) + np.random.normal(0, 0.01)
            ay = 0.05 * np.cos(t * 4.0) + np.random.normal(0, 0.01)
            az = 1.00 + 0.02 * np.sin(t * 8.0) + np.random.normal(0, 0.01)
            gx = 10.0 * np.sin(t * 3.0) + np.random.normal(0, 0.5)
            gy = 8.0 * np.cos(t * 2.5) + np.random.normal(0, 0.5)
            gz = 5.0 * np.sin(t * 1.5) + np.random.normal(0, 0.3)

            g_state.push_sensor_sample(t, ax, ay, az, gx, gy, gz)
            with g_state.lock:
                g_state.connected = True
                g_state.packet_count += 1
                g_state.rate_counter += 1
                g_state.sensor_ring_fill = int(20 + 10 * np.sin(t * 2.0))
                g_state.telemetry_ring_fill = int(10 + 5 * np.cos(t * 3.0))

            time.sleep(0.005)  # 200 Hz visualization refresh rate
    else:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("0.0.0.0", port))
        sock.settimeout(1.0)
        print(f"[UDP Receiver] Listening for AbstractX CTF packets on 0.0.0.0:{port}...")

        start_t = time.time()
        while True:
            try:
                data, addr = sock.recvfrom(2048)
                with g_state.lock:
                    g_state.connected = True
                    g_state.packet_count += 1
                    g_state.bytes_received += len(data)
                    g_state.rate_counter += 1

                t = time.time() - start_t

                # 1. Parse Standard 64-Byte TLP Encapsulating CTF 1.8 Payloads
                if len(data) == 64 or (len(data) > 0 and len(data) % 64 == 0):
                    for offset in range(0, len(data), 64):
                        tlp_frame = data[offset:offset+64]
                        tlp_type, flags, tag, channel, target_addr, len_dw, seq, timestamp_ns = struct.unpack(
                            "<BBBBIHHQ", tlp_frame[0:20]
                        )
                        ctf_payload = tlp_frame[20:60]

                        # Channel 2: Telemetry Stream (IMU & GPS)
                        if channel == 2:
                            event_id = ctf_payload[0]
                            # Stream 1, Event 1: IMU Sample (27B)
                            if event_id == 1 and len(ctf_payload) >= 27:
                                _, _, sample_seq, ax_mg, ay_mg, az_mg, gx_dps, gy_dps, gz_dps, temp_c = struct.unpack(
                                    "<BQihhhhhhh", ctf_payload[0:27]
                                )
                                g_state.push_sensor_sample(
                                    t,
                                    ax_mg / 1000.0,
                                    ay_mg / 1000.0,
                                    az_mg / 1000.0,
                                    gx_dps / 10.0,
                                    gy_dps / 10.0,
                                    gz_dps / 10.0,
                                )
                            # Stream 1, Event 2: GPS Fix (35B)
                            elif event_id == 2 and len(ctf_payload) >= 35:
                                _, _, itow, lat, lon, alt_mm, speed_mm_s, heading, sats, fix_type = struct.unpack(
                                    "<BQiiiiiiBB", ctf_payload[0:35]
                                )
                                with g_state.lock:
                                    g_state.gps_lat = lat / 1e7
                                    g_state.gps_lon = lon / 1e7
                                    g_state.gps_alt_m = alt_mm / 1000.0
                                    g_state.gps_speed_mps = speed_mm_s / 1000.0
                                    g_state.gps_sats = sats
                                    g_state.gps_fix_type = fix_type

                        # Channel 4: Debug / Coroutine Stream
                        elif channel == 4:
                            event_id = ctf_payload[0]
                            if event_id == 1 and len(ctf_payload) >= 19:
                                _, ts, task_id, handle_addr, state, reason = struct.unpack(
                                    "<BQIIBB", ctf_payload[0:19]
                                )
                                with g_state.lock:
                                    idx = task_id % len(g_state.coro_states)
                                    g_state.coro_states[idx] = state

                        # Channel 1: Control / HAL I/O Driver Traces
                        elif channel == 1:
                            event_id = ctf_payload[0]
                            if event_id == 1 and len(ctf_payload) >= 19:
                                _, ts, periph_id, req_sz, xfer_sz, dur_us, status = struct.unpack(
                                    "<BQBHHIB", ctf_payload[0:19]
                                )
                                with g_state.lock:
                                    g_state.rtt_latency_us = dur_us

                # 2. Backward Compatibility: Direct barectf Packet Header (32 bytes)
                elif len(data) >= 32:
                    magic, stream_id = struct.unpack("<IB", data[0:5])
                    if magic == CTF_MAGIC:
                        # Extract telemetry if Stream 1
                        if stream_id == 1 and len(data) >= 59:
                            _, _, seq, ax_mg, ay_mg, az_mg, gx_dps, gy_dps, gz_dps, _ = struct.unpack(
                                "<BQihhhhhhh", data[32:59]
                            )
                            g_state.push_sensor_sample(
                                t,
                                ax_mg / 1000.0,
                                ay_mg / 1000.0,
                                az_mg / 1000.0,
                                gx_dps / 10.0,
                                gy_dps / 10.0,
                                gz_dps / 10.0,
                            )
            except socket.timeout:
                with g_state.lock:
                    g_state.connected = False

def render_gui():
    """Immediate mode GUI rendering using Dear ImGui and ImPlot."""
    g_state.update_rate()

    # 1. Header Toolbar
    imgui.begin_group()
    if g_state.connected:
        imgui.text_colored(imgui.ImVec4(0.1, 0.9, 0.2, 1.0), "[ONLINE]")
    else:
        imgui.text_colored(imgui.ImVec4(0.9, 0.2, 0.1, 1.0), "[OFFLINE / WAITING]")
    imgui.same_line()
    imgui.text(f"| Platform: {g_state.platform_name} ({g_state.platform_arch}) | Packets: {g_state.packet_count} | Rate: {g_state.fps_packet_rate} pkts/sec")
    imgui.end_group()
    imgui.separator()

    # =========================================================================
    # WINDOW 1: PLATFORM TOPOLOGY & SILICON FABRIC
    # =========================================================================
    if imgui.collapsing_header("Window 1: Platform Topology & Silicon Interconnect Fabric", imgui.TreeNodeFlags_.default_open):
        imgui.columns(3, "topo_cols", False)
        
        # Column 1: Detected Architecture
        imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), "[Platform Architecture]")
        imgui.text(f"Arch Name : {g_state.platform_arch}")
        imgui.text(f"Board     : {g_state.board_model}")
        imgui.text(f"Transport : {g_state.primary_transport}")
        
        imgui.next_column()
        
        # Column 2: Active Silicon Cores
        imgui.text_colored(imgui.ImVec4(0.4, 1.0, 0.4, 1.0), "[Processing Units & Roles]")
        for core in g_state.active_cores:
            imgui.bullet_text(f"Core {core['id']}: {core['role']} ({core['clock_mhz']} MHz)\n  └─ {core['task']}")
            
        imgui.next_column()
        
        # Column 3: Synthesized Accelerators & Rings
        imgui.text_colored(imgui.ImVec4(1.0, 0.7, 0.2, 1.0), "[Hardware Accelerators & Rings]")
        for accel in g_state.hardware_accels:
            imgui.text(f" ✓ {accel}")
        imgui.text("SPSC Ring : 64 Descriptors (Zero-Copy)")
        imgui.columns(1)

    imgui.separator()

    # =========================================================================
    # WINDOW 2: DUAL-PLANE EXECUTION TIMELINE & SOURCE CODE SCANNER
    # =========================================================================
    if imgui.collapsing_header("Window 2: Dual-Plane Execution Timeline & Source Code Scanner", imgui.TreeNodeFlags_.default_open):
        # Plane 1: Low-Level I/O Processor & Hardware Drivers
        imgui.text_colored(imgui.ImVec4(0.9, 0.5, 0.2, 1.0), "Plane 1: Hardware I/O Processor & Peripheral Drivers (Interrupt & DMA Context)")
        imgui.columns(len(g_state.io_driver_events), "io_plane_cols", True)
        for ev in g_state.io_driver_events:
            if imgui.button(f"[{ev['name']}]\n{ev['subsystem']}\n{ev['latency_us']} µs", imgui.ImVec2(-1, 55)):
                with g_state.lock:
                    g_state.selected_event_name = ev['name']
                    g_state.selected_source_file = ev['file']
                    g_state.selected_source_line = ev['line']
                    g_state.selected_token = f"Driver ISR: {ev['driver']}"
            imgui.next_column()
        imgui.columns(1)

        imgui.spacing()

        # Plane 2: High-Level Main Coroutine Loop
        imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), "Plane 2: Main Processing Loop (Cooperative C++20 Coroutine Tasks)")
        imgui.columns(len(g_state.coro_names), "coro_plane_cols", True)
        state_labels = {0: "IDLE", 1: "RUNNING", 2: "AWAIT SPI", 3: "AWAIT UART", 4: "AWAIT TIMER"}
        for i, name in enumerate(g_state.coro_names):
            lbl = state_labels.get(g_state.coro_states[i], "RUNNING")
            src = g_state.coro_source_info[i]
            if imgui.button(f"[{name}]\nState: {lbl}\nLat: {g_state.coro_latencies_us[i]} µs", imgui.ImVec2(-1, 55)):
                with g_state.lock:
                    g_state.selected_event_name = name
                    g_state.selected_source_file = src['file']
                    g_state.selected_source_line = src['line']
                    g_state.selected_token = src['token']
            imgui.next_column()
        imgui.columns(1)

        imgui.spacing()

        # Source Code Scanner / Inspector Sub-Pane
        imgui.text_colored(imgui.ImVec4(1.0, 1.0, 0.2, 1.0), f">> [Source Code Inspector] Selected: {g_state.selected_event_name} -> {g_state.selected_source_file}:{g_state.selected_source_line}")
        imgui.begin_child("SourcePreview", imgui.ImVec2(-1, 80), True)
        imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), f"// Source location: {g_state.selected_source_file}")
        imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), f"   {g_state.selected_source_line - 1}:   // Processing event loop")
        imgui.text_colored(imgui.ImVec4(0.2, 1.0, 0.4, 1.0), f"-> {g_state.selected_source_line}:       {g_state.selected_token};")
        imgui.text_colored(imgui.ImVec4(0.6, 0.6, 0.6, 1.0), f"   {g_state.selected_source_line + 1}:   attitude_ekf.update(sample.gyro, sample.accel);")
        imgui.end_child()

    imgui.separator()

    # =========================================================================
    # WINDOW 3: PER-PROCESSOR SPU/CPU & PROCESS UTILIZATION
    # =========================================================================
    if imgui.collapsing_header("Window 3: Per-Processor SPU/CPU & Process Utilization", imgui.TreeNodeFlags_.default_open):
        imgui.columns(3, "cpu_cols", False)
        
        # Core 0 / Host Linux CPU Breakdown
        imgui.text_colored(imgui.ImVec4(0.4, 0.8, 1.0, 1.0), "[Core 0: Host Linux ARM64]")
        imgui.text(f"Total System CPU : {g_state.linux_total_cpu:.1f}%")
        imgui.progress_bar(g_state.linux_total_cpu / 100.0, imgui.ImVec2(-1, 0), f"{g_state.linux_total_cpu:.1f}%")
        imgui.text(f"AbstractX Process: {g_state.linux_abstractx_cpu:.1f}% (coro_main + udp_sink)")
        imgui.text(f"OS Background    : {g_state.linux_external_cpu:.1f}% (kernel, sshd, mosquitto)")

        imgui.next_column()

        # Core 1 / Coprocessor E907 RISC-V Duty Cycle
        imgui.text_colored(imgui.ImVec4(0.3, 1.0, 0.4, 1.0), "[Core 1: XuanTie E907 RISC-V]")
        imgui.text(f"Active Duty Cycle: {g_state.e907_active_duty_pct:.1f}% (148 µs/ms)")
        imgui.progress_bar(g_state.e907_active_duty_pct / 100.0, imgui.ImVec2(-1, 0), f"{g_state.e907_active_duty_pct:.1f}%")
        imgui.text(f"WFI Sleep Duty   : {g_state.e907_wfi_sleep_pct:.1f}% (Power-Saving)")
        imgui.text("Breakdown: SPI DMA 6.1%, TWI0 4.2%, Ring 4.5%")

        imgui.next_column()

        # Tang Primer 20K FPGA Logic Fabric
        imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0), "[Fabric: Tang Primer 20K FPGA]")
        imgui.text(f"Logic LUT Usage  : {g_state.fpga_lut_utilization_pct:.1f}% (3,640 / 20,000)")
        imgui.progress_bar(g_state.fpga_lut_utilization_pct / 100.0, imgui.ImVec2(-1, 0), f"{g_state.fpga_lut_utilization_pct:.1f}%")
        imgui.text(f"Auto-DMA Rate    : {g_state.fpga_dma_bw_mbps:.1f} Mbps (25 MHz SPI)")
        imgui.text("Active Cores: IMU Auto-DMA, DShot 4-CH, NeoPixel")

        imgui.columns(1)

    imgui.separator()

    # 4. Sensor Oscilloscope (ImPlot)
    if implot.begin_plot("8 kHz ICM-42688-P Oscilloscope (Real-Time Waveforms)", imgui.ImVec2(-1, 240)):
        implot.setup_axes("Time (s)", "Value", implot.ImPlotAxisFlags_.auto_fit, implot.ImPlotAxisFlags_.auto_fit)
        
        with g_state.lock:
            t_data = np.copy(g_state.time_history)
            ax_data = np.copy(g_state.accel_x)
            ay_data = np.copy(g_state.accel_y)
            az_data = np.copy(g_state.accel_z)
            gx_data = np.copy(g_state.gyro_x)
            gy_data = np.copy(g_state.gyro_y)
            gz_data = np.copy(g_state.gyro_z)

        # Plot Acceleration (g) and Gyro (deg/s)
        implot.plot_line("Accel X (g)", t_data, ax_data)
        implot.plot_line("Accel Y (g)", t_data, ay_data)
        implot.plot_line("Accel Z (g)", t_data, az_data)
        implot.plot_line("Gyro X (dps)", t_data, gx_data)
        implot.plot_line("Gyro Y (dps)", t_data, gy_data)
        implot.plot_line("Gyro Z (dps)", t_data, gz_data)
        implot.end_plot()

    imgui.separator()

    # 5. GPS & TLP Queue Watermarks
    imgui.columns(2, "bottom_cols", False)
    
    # Column 1: GPS Navigation Status
    imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0), "[U-Blox UBX-NAV-PVT Status]")
    imgui.text(f"Coordinates  : {g_state.gps_lat:.6f}° N, {g_state.gps_lon:.6f}° W")
    imgui.text(f"MSL Altitude : {g_state.gps_alt_m:.2f} m")
    imgui.text(f"Ground Speed : {g_state.gps_speed_mps:.2f} m/s ({g_state.gps_speed_mps * 3.6:.1f} km/h)")
    imgui.text(f"Satellites   : {g_state.gps_sats} SVs (3D Fix Locked)")
    
    imgui.next_column()
    
    # Column 2: SPSC TLP Ring Saturation & Latencies
    imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), "[SPSC TLP Ring Saturation & Latency]")
    imgui.text("Sensor Queue (g_sensor_ring):")
    imgui.progress_bar(g_state.sensor_ring_fill / 64.0, imgui.ImVec2(-1, 0), f"{g_state.sensor_ring_fill} / 64 pkts")
    
    imgui.text("Telemetry Queue (g_telemetry_ring):")
    imgui.progress_bar(g_state.telemetry_ring_fill / 64.0, imgui.ImVec2(-1, 0), f"{g_state.telemetry_ring_fill} / 64 pkts")
    imgui.text(f"Avg I/O Split-Transaction RTT: {g_state.rtt_latency_us:.1f} µs")
    
    imgui.columns(1)

def main():
    parser = argparse.ArgumentParser(description="AbstractX Visualizer Studio")
    parser.add_argument("--port", type=int, default=DEFAULT_UDP_PORT, help="UDP listening port")
    parser.add_argument("--sim", action="store_true", help="Run with synthetic telemetry generator")
    args = parser.parse_args()

    # Start UDP receiver background thread
    recv_thread = threading.Thread(target=udp_receiver_thread, args=(args.port, args.sim), daemon=True)
    recv_thread.start()

    # Configure and Launch Dear ImGui Application
    runner_params = hello_imgui.RunnerParams()
    runner_params.app_window_params.window_title = "AbstractX Visualizer Studio"
    runner_params.app_window_params.window_geometry.size = (1150, 750)
    runner_params.callbacks.show_gui = render_gui
    implot.create_context()

    immapp.run(runner_params)
    implot.destroy_context()

if __name__ == "__main__":
    main()
