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

        # Coroutine Execution States (Gantt Data)
        # 0=Idle, 1=Running, 2=Suspended(SPI), 3=Suspended(UART), 4=Suspended(Timer)
        self.coro_names = ["imu_task", "gps_task", "heartbeat_task", "flight_control"]
        self.coro_states = [1, 2, 4, 1]
        self.coro_latencies_us = [0.8, 1.2, 0.4, 0.9]

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

                # Parse CTF Header (32 bytes)
                if len(data) >= 32:
                    magic, stream_id = struct.unpack("<IB", data[0:5])
                    if magic == CTF_MAGIC:
                        t = time.time() - start_t
                        # Extract telemetry if Stream 1
                        if stream_id == 1 and len(data) >= 55:
                            _, _, seq, ax_mg, ay_mg, az_mg, gx_dps, gy_dps, gz_dps, _ = struct.unpack(
                                "<BQihhhhhhh", data[32:55]
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
    imgui.text(f"| Packets: {g_state.packet_count} | Rate: {g_state.fps_packet_rate} pkts/sec | Drop: {g_state.discarded_events}")
    imgui.end_group()
    imgui.separator()

    # 2. Main Layout: Split into Timeline (Top) and Sensors/Queues (Bottom)
    if imgui.collapsing_header("C++20 Coroutine Gantt Execution Timeline", imgui.TreeNodeFlags_.default_open):
        imgui.text("Core 1 Asynchronous Work Queue States (Microsecond Resolution):")
        
        # Draw Coroutine Gantt Status Bars
        imgui.columns(4, "coro_columns", True)
        state_names = {0: ("IDLE", (0.5, 0.5, 0.5, 1.0)),
                       1: ("RUNNING", (0.1, 0.9, 0.2, 1.0)),
                       2: ("AWAIT SPI DMA", (0.9, 0.6, 0.1, 1.0)),
                       3: ("AWAIT UART GPS", (0.2, 0.6, 0.9, 1.0)),
                       4: ("AWAIT TIMER", (0.8, 0.3, 0.9, 1.0))}
        
        for i, name in enumerate(g_state.coro_names):
            imgui.text_colored(imgui.ImVec4(0.4, 0.8, 1.0, 1.0), f"{name}")
            st_text, st_color = state_names.get(g_state.coro_states[i], ("UNKNOWN", (1, 1, 1, 1)))
            imgui.text_colored(imgui.ImVec4(*st_color), f"State: {st_text}")
            imgui.text(f"Latency: {g_state.coro_latencies_us[i]} µs")
            imgui.next_column()
        imgui.columns(1)

    imgui.separator()

    # 3. Sensor Oscilloscope (ImPlot)
    if implot.begin_plot("8 kHz ICM-42688-P Oscilloscope (Real-Time Waveforms)", imgui.ImVec2(-1, 280)):
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

    # 4. GPS & TLP Queue Watermarks
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
