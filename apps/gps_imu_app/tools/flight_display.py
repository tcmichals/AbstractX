#!/usr/bin/env python3
"""
Copyright (C) 2026 Tim Michals
SPDX-License-Identifier: GPL-3.0-or-later

AbstractX Flight Display & 3D Quadcopter Visualizer (flight_display.py)
----------------------------------------------------------------------
Live aviation Primary Flight Display (PFD) and 3D quadcopter attitude
visualizer for gps_imu_app.

Features:
1. Dynamic barectf/CTF 1.8 schema decoding using ctf_schema_loader.py.
2. Primary Flight Display (PFD): Artificial Horizon (Sky/Ground), Pitch Ladder, Roll Scale.
3. 3D Quadcopter Wireframe: Real-time 3D perspective rotation matrix driven by AHRS.
4. Vertical Tape Altimeter (MSL altitude) & Airspeed Tape.
5. Quad-X Motor Mixer Bar Gauges: M1 (FR), M2 (RL), M3 (FL), M4 (RR).
6. Multi-Rate Sensor Ingestion Meters (IMU 8 kHz, Mag 50 Hz, GPS 10 Hz).

Usage:
  python3 apps/gps_imu_app/tools/flight_display.py [--port 9870] [--sim]
"""

import sys
import time
import socket
import argparse
import threading
import numpy as np
from pathlib import Path

# Add visualizer tools directory to Python path for ctf_schema_loader
REPO_ROOT = Path(__file__).resolve().parents[3]
TOOLS_DIR = REPO_ROOT / "tools" / "visualizer"
sys.path.insert(0, str(TOOLS_DIR))

try:
    from ctf_schema_loader import CtfSchema
except ImportError:
    print("[WARN] ctf_schema_loader.py not found in tools/visualizer. Using fallback parser.")
    CtfSchema = None

import matplotlib
import matplotlib.pyplot as plt
import matplotlib.patches as patches
from matplotlib.animation import FuncAnimation

# Thread-safe flight telemetry state
class FlightState:
    def __init__(self):
        self.lock = threading.Lock()
        self.connected = False
        self.roll_deg = 0.0
        self.pitch_deg = 0.0
        self.yaw_deg = 0.0
        self.altitude_m = 0.0
        self.ground_speed_mps = 0.0
        self.motors = [500, 500, 500, 500]  # M1, M2, M3, M4 (100..1000)
        
        # Stream rates (Hz)
        self.imu_hz = 0
        self.mag_hz = 0
        self.gps_hz = 0
        self.ahrs_hz = 0
        
        # Internal rate counters
        self._imu_count = 0
        self._mag_count = 0
        self._gps_count = 0
        self._ahrs_count = 0
        self._last_rate_calc = time.time()

    def update_rates(self):
        with self.lock:
            now = time.time()
            dt = now - self._last_rate_calc
            if dt >= 1.0:
                self.imu_hz = int(self._imu_count / dt)
                self.mag_hz = int(self._mag_count / dt)
                self.gps_hz = int(self._gps_count / dt)
                self.ahrs_hz = int(self._ahrs_count / dt)
                self._imu_count = 0
                self._mag_count = 0
                self._gps_count = 0
                self._ahrs_count = 0
                self._last_rate_calc = now

g_state = FlightState()

def udp_telemetry_thread(port: int, sim_mode: bool, schema_path: str):
    """Background thread listening for 64B TLP UDP frames or synthesizing flight data."""
    schema = None
    if CtfSchema and Path(schema_path).exists():
        try:
            schema = CtfSchema(schema_path)
            print(f"[Telemetry] Loaded CTF Schema from {schema_path}")
        except Exception as e:
            print(f"[Telemetry] Failed to load schema: {e}")

    if sim_mode:
        print("[Telemetry] Starting synthetic simulation mode (200 Hz)...")
        t0 = time.time()
        while True:
            t = time.time() - t0
            # Synthesize quadcopter hovering with gentle bank and pitch oscillations
            roll = 15.0 * np.sin(t * 1.2)
            pitch = 8.0 * np.cos(t * 0.9)
            yaw = (t * 20.0) % 360.0
            alt = 10.0 + 2.0 * np.sin(t * 0.5)
            spd = 2.5 + 1.0 * np.cos(t * 0.8)
            
            # Quad-X motor demands
            u_roll = roll * 4.0
            u_pitch = pitch * 4.0
            m1 = int(np.clip(500 - u_roll + u_pitch, 100, 1000))
            m2 = int(np.clip(500 + u_roll - u_pitch, 100, 1000))
            m3 = int(np.clip(500 + u_roll + u_pitch, 100, 1000))
            m4 = int(np.clip(500 - u_roll - u_pitch, 100, 1000))
            
            with g_state.lock:
                g_state.connected = True
                g_state.roll_deg = roll
                g_state.pitch_deg = pitch
                g_state.yaw_deg = yaw
                g_state.altitude_m = alt
                g_state.ground_speed_mps = spd
                g_state.motors = [m1, m2, m3, m4]
                g_state._ahrs_count += 1
                g_state._imu_count += 40  # 8 kHz simulated equivalent
                g_state._mag_count += 1
                if int(t * 10) % 20 == 0:
                    g_state._gps_count += 1

            time.sleep(0.005)
    else:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("0.0.0.0", port))
        sock.settimeout(1.0)
        print(f"[Telemetry] Listening for AbstractX TLPs on UDP 0.0.0.0:{port}...")

        while True:
            try:
                data, _ = sock.recvfrom(2048)
                if len(data) >= 64:
                    for off in range(0, len(data) - 63, 64):
                        frame = data[off:off+64]
                        if schema:
                            decoded = schema.decode_tlp(frame)
                            if decoded and decoded.get("valid"):
                                ev_name = decoded.get("event")
                                fields = decoded.get("fields", {})
                                with g_state.lock:
                                    g_state.connected = True
                                    if ev_name == "ahrs_state" or decoded.get("tag") == 4:
                                        g_state._ahrs_count += 1
                                        if "roll_cdeg" in fields:
                                            g_state.roll_deg = fields["roll_cdeg"]["value"]
                                        if "pitch_cdeg" in fields:
                                            g_state.pitch_deg = fields["pitch_cdeg"]["value"]
                                        if "yaw_cdeg" in fields:
                                            g_state.yaw_deg = fields["yaw_cdeg"]["value"]
                                        if "alt_mm" in fields:
                                            g_state.altitude_m = fields["alt_mm"]["value"]
                                        if "speed_cm_s" in fields:
                                            g_state.ground_speed_mps = fields["speed_cm_s"]["value"]
                                        m1 = fields.get("m1_throttle", {}).get("value", 500)
                                        m2 = fields.get("m2_throttle", {}).get("value", 500)
                                        m3 = fields.get("m3_throttle", {}).get("value", 500)
                                        m4 = fields.get("m4_throttle", {}).get("value", 500)
                                        g_state.motors = [m1, m2, m3, m4]
                                    elif ev_name == "imu_sample" or decoded.get("tag") == 1:
                                        g_state._imu_count += 1
                                    elif ev_name == "gps_fix" or decoded.get("tag") == 2:
                                        g_state._gps_count += 1
                                    elif ev_name == "mag_sample" or decoded.get("tag") == 3:
                                        g_state._mag_count += 1
            except socket.timeout:
                pass
            except Exception as e:
                time.sleep(0.01)

def rotation_matrix(roll, pitch, yaw):
    """Computes 3D Tait-Bryan rotation matrix."""
    r = np.radians(roll)
    p = np.radians(pitch)
    y = np.radians(yaw)
    
    Rx = np.array([[1, 0, 0], [0, np.cos(r), -np.sin(r)], [0, np.sin(r), np.cos(r)]])
    Ry = np.array([[np.cos(p), 0, np.sin(p)], [0, 1, 0], [-np.sin(p), 0, np.cos(p)]])
    Rz = np.array([[np.cos(y), -np.sin(y), 0], [np.sin(y), np.cos(y), 0], [0, 0, 1]])
    return Rz @ Ry @ Rx

def create_flight_display():
    """Builds and launches the 4-panel matplotlib flight dashboard."""
    fig = plt.figure(figsize=(14, 8), facecolor="#0b0f19")
    fig.canvas.manager.set_window_title("AbstractX Flight Display & AHRS Visualizer")
    
    gs = fig.add_gridspec(2, 2, hspace=0.25, wspace=0.2)
    ax_pfd   = fig.add_subplot(gs[0, 0])
    ax_3d    = fig.add_subplot(gs[0, 1], projection='3d')
    ax_alt   = fig.add_subplot(gs[1, 0])
    ax_motor = fig.add_subplot(gs[1, 1])

    # 3D Quadcopter Model Geometry (Body Frame)
    arm_len = 1.0
    hub_pts = np.array([[-0.2, -0.2, 0], [0.2, -0.2, 0], [0.2, 0.2, 0], [-0.2, 0.2, 0], [-0.2, -0.2, 0]]).T
    arm_x = np.array([[-arm_len, -arm_len, 0], [arm_len, arm_len, 0]]).T  # M2 to M1
    arm_y = np.array([[-arm_len, arm_len, 0], [arm_len, -arm_len, 0]]).T  # M3 to M4
    
    # Motor Propeller Discs
    prop_theta = np.linspace(0, 2*np.pi, 20)
    prop_r = 0.35
    prop_circle = np.array([prop_r * np.cos(prop_theta), prop_r * np.sin(prop_theta), np.zeros_like(prop_theta)])

    def update_frame(frame):
        g_state.update_rates()
        with g_state.lock:
            roll = g_state.roll_deg
            pitch = g_state.pitch_deg
            yaw = g_state.yaw_deg
            alt = g_state.altitude_m
            spd = g_state.ground_speed_mps
            motors = list(g_state.motors)
            imu_hz = g_state.imu_hz
            mag_hz = g_state.mag_hz
            gps_hz = g_state.gps_hz
            ahrs_hz = g_state.ahrs_hz

        # -------------------------------------------------------------
        # 1. Primary Flight Display (PFD) / Artificial Horizon
        # -------------------------------------------------------------
        ax_pfd.clear()
        ax_pfd.set_facecolor("#111827")
        ax_pfd.set_xlim(-50, 50)
        ax_pfd.set_ylim(-35, 35)
        ax_pfd.axis('off')
        
        # Horizon Line & Sky/Ground Tilt
        rad_roll = np.radians(-roll)
        dy = 50 * np.sin(rad_roll)
        dx = 50 * np.cos(rad_roll)
        pitch_offset = -pitch * 0.8
        
        # Sky (Cyan/Blue) and Earth (Brown/Orange) Polygons
        sky = patches.Polygon([(-50, 50), (50, 50), (dx, dy + pitch_offset), (-dx, -dy + pitch_offset)],
                              color="#0284c7", alpha=0.9)
        earth = patches.Polygon([(-50, -50), (50, -50), (dx, dy + pitch_offset), (-dx, -dy + pitch_offset)],
                                color="#78350f", alpha=0.9)
        ax_pfd.add_patch(sky)
        ax_pfd.add_patch(earth)
        
        # Horizon White Centerline
        ax_pfd.plot([-dx, dx], [-dy + pitch_offset, dy + pitch_offset], color="#ffffff", lw=3)
        
        # Aircraft Reticle (Fixed center yellow crosshairs)
        ax_pfd.plot([-15, -5], [0, 0], color="#facc15", lw=4)
        ax_pfd.plot([5, 15], [0, 0], color="#facc15", lw=4)
        ax_pfd.plot([0, 0], [-2, 2], color="#facc15", lw=4)
        ax_pfd.plot([-5, 0, 5], [-5, 0, -5], color="#facc15", lw=3)
        
        # Pitch Ladder (+10°, +20°, -10°, -20°)
        for deg in [-20, -10, 10, 20]:
            y_pos = deg * 0.8 + pitch_offset
            w = 8 if abs(deg) == 10 else 12
            ax_pfd.plot([-w, w], [y_pos, y_pos], color="#e2e8f0", lw=1.5, ls="--")
            ax_pfd.text(w + 1, y_pos - 1, f"{deg}°", color="#ffffff", fontsize=8)

        ax_pfd.set_title(f"Primary Flight Display (PFD) | Roll: {roll:+.1f}° | Pitch: {pitch:+.1f}°",
                         color="#38bdf8", fontsize=11, fontweight="bold", pad=8)

        # -------------------------------------------------------------
        # 2. 3D Quadcopter Perspective Wireframe
        # -------------------------------------------------------------
        ax_3d.clear()
        ax_3d.set_facecolor("#0b0f19")
        ax_3d.grid(False)
        ax_3d.set_xticks([])
        ax_3d.set_yticks([])
        ax_3d.set_zticks([])
        
        R = rotation_matrix(roll, pitch, yaw)
        
        # Rotate Hub & Arms
        r_hub = R @ hub_pts
        r_x = R @ arm_x
        r_y = R @ arm_y
        
        ax_3d.plot(r_hub[0], r_hub[1], r_hub[2], color="#94a3b8", lw=2)
        ax_3d.plot(r_x[0], r_x[1], r_x[2], color="#38bdf8", lw=3) # Front-Right to Rear-Left
        ax_3d.plot(r_y[0], r_y[1], r_y[2], color="#f43f5e", lw=3) # Front-Left to Rear-Right
        
        # Rotate Motor Discs
        motor_pos = [
            np.array([arm_len, arm_len, 0]),     # M1 FR
            np.array([-arm_len, -arm_len, 0]),   # M2 RL
            np.array([-arm_len, arm_len, 0]),    # M3 FL
            np.array([arm_len, -arm_len, 0])     # M4 RR
        ]
        
        colors = ["#22c55e", "#22c55e", "#eab308", "#eab308"]
        for idx, m_body in enumerate(motor_pos):
            disc = R @ (prop_circle + m_body[:, None])
            ax_3d.plot(disc[0], disc[1], disc[2], color=colors[idx], lw=2)
            # Motor label
            lbl_pos = R @ (m_body + np.array([0, 0, 0.2]))
            ax_3d.text(lbl_pos[0], lbl_pos[1], lbl_pos[2], f"M{idx+1}", color="#ffffff", fontsize=8)

        ax_3d.set_xlim(-1.8, 1.8)
        ax_3d.set_ylim(-1.8, 1.8)
        ax_3d.set_zlim(-1.5, 1.5)
        ax_3d.set_title(f"3D Attitude Model | Yaw: {yaw:.1f}° (North Referenced)",
                        color="#a855f7", fontsize=11, fontweight="bold", pad=8)

        # -------------------------------------------------------------
        # 3. Altimeter, Speed & Compass Ribbon
        # -------------------------------------------------------------
        ax_alt.clear()
        ax_alt.set_facecolor("#111827")
        ax_alt.set_xlim(0, 100)
        ax_alt.set_ylim(0, 100)
        ax_alt.axis('off')
        
        # Digital Flight Gauges
        ax_alt.text(10, 80, "MSL ALTITUDE", color="#94a3b8", fontsize=9, fontweight="bold")
        ax_alt.text(10, 60, f"{alt:5.1f} m", color="#38bdf8", fontsize=18, fontweight="bold")
        ax_alt.text(10, 45, f"({alt * 3.28084:5.1f} ft)", color="#64748b", fontsize=10)

        ax_alt.text(55, 80, "GROUND SPEED", color="#94a3b8", fontsize=9, fontweight="bold")
        ax_alt.text(55, 60, f"{spd:4.1f} m/s", color="#4ade80", fontsize=18, fontweight="bold")
        ax_alt.text(55, 45, f"({spd * 1.94384:4.1f} kts)", color="#64748b", fontsize=10)

        # Multi-Rate Ingestion Health Box
        ax_alt.plot([5, 95], [35, 35], color="#334155", lw=1)
        ax_alt.text(10, 22, "SENSOR RATES:", color="#cbd5e1", fontsize=9, fontweight="bold")
        ax_alt.text(10, 8, f"IMU: {imu_hz} Hz", color="#22c55e", fontsize=10, fontweight="bold")
        ax_alt.text(35, 8, f"MAG: {mag_hz} Hz", color="#c084fc", fontsize=10, fontweight="bold")
        ax_alt.text(58, 8, f"GPS: {gps_hz} Hz", color="#fbbf24", fontsize=10, fontweight="bold")
        ax_alt.text(80, 8, f"AHRS: {ahrs_hz} Hz", color="#38bdf8", fontsize=10, fontweight="bold")

        ax_alt.set_title("Navigation Telemetry & Multi-Rate Health", color="#4ade80", fontsize=11, fontweight="bold")

        # -------------------------------------------------------------
        # 4. Quad-X Motor Mixer Demands
        # -------------------------------------------------------------
        ax_motor.clear()
        ax_motor.set_facecolor("#111827")
        ax_motor.set_ylim(0, 1050)
        ax_motor.set_xlim(-0.5, 3.5)
        
        labels = ["M1 (FR CCW)", "M2 (RL CCW)", "M3 (FL CW)", "M4 (RR CW)"]
        bar_colors = ["#38bdf8", "#38bdf8", "#f43f5e", "#f43f5e"]
        bars = ax_motor.bar(range(4), motors, color=bar_colors, width=0.55, edgecolor="#ffffff", lw=1)
        
        # Hover Reference line (500 us)
        ax_motor.axhline(500, color="#facc15", ls="--", lw=1.5, alpha=0.7, label="Base Hover (50%)")
        ax_motor.set_xticks(range(4))
        ax_motor.set_xticklabels(labels, color="#cbd5e1", fontsize=8)
        ax_motor.tick_params(colors="#94a3b8")
        ax_motor.legend(loc="upper right", facecolor="#1e293b", edgecolor="#475569", labelcolor="#ffffff", fontsize=8)
        
        # Value tags above bars
        for b, m in zip(bars, motors):
            ax_motor.text(b.get_x() + b.get_width()/2.0, m + 25, f"{int(m)}",
                          ha="center", color="#ffffff", fontsize=9, fontweight="bold")

        ax_motor.set_title("Cascaded PID Motor Mixer Outputs (100..1000 µs)",
                           color="#facc15", fontsize=11, fontweight="bold")

    ani = FuncAnimation(fig, update_frame, interval=40, cache_frame_data=False)
    plt.show()

def main():
    parser = argparse.ArgumentParser(description="AbstractX Flight Display & 3D Visualizer")
    parser.add_argument("--port", type=int, default=9870, help="UDP telemetry port (default: 9870)")
    parser.add_argument("--sim", action="store_true", help="Run in simulation mode without hardware")
    parser.add_argument("--schema", type=str,
                        default=str(Path(__file__).resolve().parent.parent / "trace_schema.json"),
                        help="Path to trace schema (YAML or JSON)")
    args = parser.parse_args()

    # Start UDP listener in background thread
    t = threading.Thread(target=udp_telemetry_thread, args=(args.port, args.sim, args.schema), daemon=True)
    t.start()

    # Launch GUI
    print("[Display] Launching Flight Display & 3D Quadcopter Visualizer...")
    create_flight_display()

if __name__ == "__main__":
    main()
