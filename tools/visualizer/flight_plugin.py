#!/usr/bin/env python3
"""
Copyright (C) 2026 Tim Michals
SPDX-License-Identifier: GPL-3.0-or-later

AbstractX Level 2 Domain Plugin: Flight Display & 3D Attitude Visualizer
------------------------------------------------------------------------
Implemented in Python using Dear ImGui Bundle (imgui-bundle):
1. Primary Flight Display (PFD):
   - Vector-rendered Artificial Horizon (Sky/Earth tilt polygons)
   - Pitch ladder (+/-10°, +/-20°, +/-30°)
   - Center aircraft reticle crosshairs & roll pointer
2. 3D Quadcopter Perspective Wireframe:
   - Real-time 3D Tait-Bryan rotation matrix (Yaw, Pitch, Roll)
   - Color-coded arms (Front: Cyan, Rear: Coral)
   - Rotating motor disc rings (M1..M4) with dynamic labels
3. Quad-X Motor Mixer Demands:
   - M1 (FR CCW), M2 (RL CCW), M3 (FL CW), M4 (RR CW) with 500 µs hover reference
4. Aviation Navigation Gauges & Stream Diagnostics:
   - Digital MSL Altitude (m & ft), Ground Speed (m/s & kts)
   - Ingestion rates: IMU (8 kHz), Mag (50 Hz), GPS (10 Hz), AHRS (100 Hz)
5. 8 kHz ICM-42688-P IMU Real-Time Oscilloscope (ImPlot)
"""

import math
import numpy as np
from imgui_bundle import imgui, implot

def rotation_matrix(roll_deg: float, pitch_deg: float, yaw_deg: float) -> np.ndarray:
    """Computes 3D Tait-Bryan rotation matrix."""
    r = math.radians(roll_deg)
    p = math.radians(pitch_deg)
    y = math.radians(yaw_deg)

    rx = np.array([[1.0, 0.0, 0.0],
                   [0.0, math.cos(r), -math.sin(r)],
                   [0.0, math.sin(r), math.cos(r)]], dtype=np.float64)

    ry = np.array([[math.cos(p), 0.0, math.sin(p)],
                   [0.0, 1.0, 0.0],
                   [-math.sin(p), 0.0, math.cos(p)]], dtype=np.float64)

    rz = np.array([[math.cos(y), -math.sin(y), 0.0],
                   [math.sin(y), math.cos(y), 0.0],
                   [0.0, 0.0, 1.0]], dtype=np.float64)

    return rz @ ry @ rx

try:
    from .sdk import AbstractXStudioPlugin
except (ImportError, ValueError):
    try:
        from sdk import AbstractXStudioPlugin
    except ImportError:
        import sys
        from pathlib import Path
        sys.path.insert(0, str(Path(__file__).parent))
        from sdk import AbstractXStudioPlugin

class FlightVisualizerPlugin(AbstractXStudioPlugin):
    """
    # @impl [SPEC-STUDIO-03] tools/visualizer/flight_plugin.py
    User Domain Instruments: Primary Flight Display (PFD), 3D Attitude Wireframe, Motor Demands, and 8 kHz IMU Oscilloscope.
    """
    def __init__(self):
        super().__init__(name="Flight Instruments & Attitude", version="2.0")
        # 3D Quadcopter Geometry Model
        self.arm_len = 1.0
        self.hub_pts = np.array([
            [0.25, 0.25, 0.0],
            [-0.25, 0.25, 0.0],
            [-0.25, -0.25, 0.0],
            [0.25, -0.25, 0.0],
            [0.25, 0.25, 0.0]
        ], dtype=np.float64)

        # Arms: Front-Right to Rear-Left, Front-Left to Rear-Right
        self.arm_x = np.array([[self.arm_len, self.arm_len, 0.0],
                               [-self.arm_len, -self.arm_len, 0.0]], dtype=np.float64)
        self.arm_y = np.array([[-self.arm_len, self.arm_len, 0.0],
                               [self.arm_len, -self.arm_len, 0.0]], dtype=np.float64)

        # Propeller circle template (radius 0.32)
        theta = np.linspace(0, 2 * np.pi, 16)
        self.prop_circle = np.zeros((16, 3), dtype=np.float64)
        self.prop_circle[:, 0] = 0.32 * np.cos(theta)
        self.prop_circle[:, 1] = 0.32 * np.sin(theta)

        # Motor centers in body frame (Quad-X configuration)
        self.motor_offsets = [
            np.array([self.arm_len, self.arm_len, 0.0]),    # M1 Front-Right (CCW)
            np.array([-self.arm_len, -self.arm_len, 0.0]),  # M2 Rear-Left (CCW)
            np.array([-self.arm_len, self.arm_len, 0.0]),   # M3 Front-Left (CW)
            np.array([self.arm_len, -self.arm_len, 0.0])    # M4 Rear-Right (CW)
        ]

    def render_ui(self, delta_time: float = 0.0, state=None):
        """SDK entry point for per-frame rendering."""
        if state is not None:
            self.render(state)

    def render(self, state):
        """Renders the entire Level 2 Flight & IMU domain dashboard."""
        with state.lock:
            roll = state.roll_deg
            pitch = state.pitch_deg
            yaw = state.yaw_deg
            alt_m = state.altitude_m
            speed_mps = state.ground_speed_mps
            motors = list(state.motors)
            imu_hz = state.imu_hz
            mag_hz = state.mag_hz
            gps_hz = state.gps_hz
            ahrs_hz = state.ahrs_hz
            source_lbl = state.source_mode
            trace_lbl = state.fpga_trace

        # Status Banner
        theme_color = imgui.ImVec4(0.2, 0.8, 1.0, 1.0) if "FPGA" in source_lbl else imgui.ImVec4(0.3, 0.9, 0.4, 1.0)
        imgui.text_colored(theme_color, f"Mode: {source_lbl}")
        imgui.same_line()
        imgui.text_colored(imgui.ImVec4(0.7, 0.7, 0.7, 1.0), f"| {trace_lbl}")
        imgui.separator()

        # Two-column layout for Primary Instruments
        avail_w = imgui.get_content_region_avail().x
        col_w = max(340.0, (avail_w - 20.0) / 2.0)
        pfd_h = 280.0

        imgui.columns(2, "flight_inst_cols", False)
        imgui.set_column_width(0, col_w + 10.0)
        imgui.set_column_width(1, col_w + 10.0)

        # -------------------------------------------------------------
        # 1. Primary Flight Display (PFD)
        # -------------------------------------------------------------
        imgui.text_colored(imgui.ImVec4(0.3, 0.8, 1.0, 1.0), "Primary Flight Display (PFD) - Artificial Horizon")
        self._render_pfd(col_w, pfd_h, roll, pitch)

        imgui.next_column()

        # -------------------------------------------------------------
        # 2. 3D Quadcopter Perspective Wireframe
        # -------------------------------------------------------------
        imgui.text_colored(imgui.ImVec4(0.7, 0.5, 1.0, 1.0), f"3D Attitude Model - Yaw: {yaw:.1f}°")
        self._render_3d_quadcopter(col_w, pfd_h, roll, pitch, yaw)

        imgui.columns(1)
        imgui.separator()

        # -------------------------------------------------------------
        # 3. Flight Gauges & Quad-X Motor Mixer Demands
        # -------------------------------------------------------------
        imgui.columns(2, "flight_lower_cols", False)
        imgui.set_column_width(0, col_w + 10.0)
        imgui.set_column_width(1, col_w + 10.0)

        # Navigation & Rates
        imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0), "[Aviation Navigation Gauges & Stream Health]")
        imgui.begin_child("NavGauges", imgui.ImVec2(-1, 140), True)
        
        c1_w = col_w * 0.45
        imgui.columns(2, "alt_spd_cols", False)
        imgui.set_column_width(0, c1_w)
        
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "MSL ALTITUDE")
        imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), f"{alt_m:6.2f} m")
        imgui.text_colored(imgui.ImVec4(0.5, 0.5, 0.5, 1.0), f"({alt_m * 3.28084:6.1f} ft)")
        
        imgui.next_column()
        
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "GROUND SPEED")
        imgui.text_colored(imgui.ImVec4(0.3, 0.9, 0.4, 1.0), f"{speed_mps:5.2f} m/s")
        imgui.text_colored(imgui.ImVec4(0.5, 0.5, 0.5, 1.0), f"({speed_mps * 1.94384:5.1f} kts / {speed_mps * 3.6:5.1f} km/h)")
        
        imgui.columns(1)
        imgui.separator()
        
        imgui.text_colored(imgui.ImVec4(0.8, 0.8, 0.8, 1.0), "Multi-Rate Stream Rates:")
        imgui.same_line()
        imgui.text_colored(imgui.ImVec4(0.2, 1.0, 0.4, 1.0),
                           f"IMU: {imu_hz} Hz | MAG: {mag_hz} Hz | GPS: {gps_hz} Hz | AHRS: {ahrs_hz} Hz")
        imgui.end_child()

        imgui.next_column()

        # Quad-X Motor Mixer Outputs
        imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0), "[Quad-X Motor Mixer Demands (100..1000 µs)]")
        imgui.begin_child("MotorMixer", imgui.ImVec2(-1, 140), True)
        
        motor_labels = [
            ("M1 (FR CCW)", imgui.ImVec4(0.2, 0.8, 1.0, 1.0)),
            ("M2 (RL CCW)", imgui.ImVec4(0.2, 0.8, 1.0, 1.0)),
            ("M3 (FL CW) ", imgui.ImVec4(0.9, 0.3, 0.4, 1.0)),
            ("M4 (RR CW) ", imgui.ImVec4(0.9, 0.3, 0.4, 1.0))
        ]
        
        for idx, (label, col) in enumerate(motor_labels):
            val = motors[idx] if idx < len(motors) else 500
            fraction = min(max((val - 100) / 900.0, 0.0), 1.0)
            imgui.text_colored(col, label)
            imgui.same_line(110.0)
            imgui.progress_bar(fraction, imgui.ImVec2(-1, 18), f"{val} µs ({int(fraction * 100)}%)")

        imgui.end_child()

        imgui.columns(1)
        imgui.separator()

        # -------------------------------------------------------------
        # 4. 8 kHz IMU Oscilloscope (ImPlot)
        # -------------------------------------------------------------
        if implot.begin_plot("8 kHz ICM-42688-P High-Rate Sensor Waveforms", imgui.ImVec2(-1, 220)):
            implot.setup_axes("Time (s)", "Dynamic Amplitude", implot.AxisFlags_.auto_fit, implot.AxisFlags_.auto_fit)
            with state.lock:
                t_data = np.copy(state.time_history)
                ax_data = np.copy(state.accel_x)
                ay_data = np.copy(state.accel_y)
                az_data = np.copy(state.accel_z)
                gx_data = np.copy(state.gyro_x)
                gy_data = np.copy(state.gyro_y)
                gz_data = np.copy(state.gyro_z)

            implot.plot_line("Accel X (g)", t_data, ax_data)
            implot.plot_line("Accel Y (g)", t_data, ay_data)
            implot.plot_line("Accel Z (g)", t_data, az_data)
            implot.plot_line("Gyro X (dps)", t_data, gx_data)
            implot.plot_line("Gyro Y (dps)", t_data, gy_data)
            implot.plot_line("Gyro Z (dps)", t_data, gz_data)
            implot.end_plot()

    def _render_pfd(self, width: float, height: float, roll_deg: float, pitch_deg: float):
        """Draws the vector-based Primary Flight Display using Dear ImGui DrawList."""
        cursor = imgui.get_cursor_screen_pos()
        p_min = cursor
        p_max = imgui.ImVec2(cursor.x + width, cursor.y + height)

        dl = imgui.get_window_draw_list()
        dl.push_clip_rect(p_min, p_max, True)

        # Colors
        col_sky = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.08, 0.45, 0.78, 1.0))
        col_earth = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.48, 0.24, 0.08, 1.0))
        col_white = imgui.color_convert_float4_to_u32(imgui.ImVec4(1.0, 1.0, 1.0, 1.0))
        col_reticle = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.98, 0.80, 0.08, 1.0))
        col_border = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.2, 0.3, 0.4, 1.0))

        # Base background (Sky)
        dl.add_rect_filled(p_min, p_max, col_sky)

        # Center point
        cx = p_min.x + width * 0.5
        cy = p_min.y + height * 0.5

        # Pitch scale: 2.2 pixels per degree
        pitch_scale = 2.2
        pitch_offset = pitch_deg * pitch_scale

        # Roll rotation angle in radians
        rad_roll = math.radians(roll_deg)
        cos_r = math.cos(rad_roll)
        sin_r = math.sin(rad_roll)

        # Large radius bounding quad for rotated Earth
        r_box = max(width, height) * 2.0

        # In local horizon frame, Earth is y > 0
        local_earth = [
            (-r_box, 0.0),
            (r_box, 0.0),
            (r_box, r_box),
            (-r_box, r_box)
        ]

        # Transform local earth polygon to screen
        world_earth = []
        for lx, ly in local_earth:
            # Rotate by roll and translate to horizon center
            # Screen y is inverted (down is positive)
            sx = cx + lx * cos_r + ly * sin_r
            sy = (cy + pitch_offset) - lx * sin_r + ly * cos_r
            world_earth.append(imgui.ImVec2(sx, sy))

        dl.add_convex_poly_filled(world_earth, col_earth)

        # White horizon line
        h_x1 = cx - r_box * cos_r
        h_y1 = (cy + pitch_offset) + r_box * sin_r
        h_x2 = cx + r_box * cos_r
        h_y2 = (cy + pitch_offset) - r_box * sin_r
        dl.add_line(imgui.ImVec2(h_x1, h_y1), imgui.ImVec2(h_x2, h_y2), col_white, 2.5)

        # Pitch ladder ticks (+/-10°, +/-20°, +/-30°)
        for deg in [-30, -20, -10, 10, 20, 30]:
            y_local = -deg * pitch_scale
            bar_w = 28.0 if abs(deg) % 20 == 0 else 16.0
            
            p1_x = cx - bar_w * cos_r - y_local * sin_r
            p1_y = (cy + pitch_offset) + bar_w * sin_r - y_local * cos_r
            p2_x = cx + bar_w * cos_r - y_local * sin_r
            p2_y = (cy + pitch_offset) - bar_w * sin_r - y_local * cos_r
            dl.add_line(imgui.ImVec2(p1_x, p1_y), imgui.ImVec2(p2_x, p2_y), col_white, 1.5)

            # Degree text label
            lbl = f"{abs(deg)}°"
            tx = p2_x + 4.0 * cos_r
            ty = p2_y - 4.0 * sin_r - 6.0
            dl.add_text(imgui.ImVec2(tx, ty), col_white, lbl)

        # Fixed Aircraft Reticle (Yellow crosshairs at center screen)
        # Left Wing
        dl.add_line(imgui.ImVec2(cx - 40, cy), imgui.ImVec2(cx - 14, cy), col_reticle, 3.5)
        dl.add_line(imgui.ImVec2(cx - 14, cy), imgui.ImVec2(cx - 14, cy + 8), col_reticle, 3.0)
        # Right Wing
        dl.add_line(imgui.ImVec2(cx + 14, cy), imgui.ImVec2(cx + 40, cy), col_reticle, 3.5)
        dl.add_line(imgui.ImVec2(cx + 14, cy), imgui.ImVec2(cx + 14, cy + 8), col_reticle, 3.0)
        # Center Pip
        dl.add_circle_filled(imgui.ImVec2(cx, cy), 3.0, col_reticle)

        # Top Roll Triangle Reticle
        top_tri = [
            imgui.ImVec2(cx, p_min.y + 6),
            imgui.ImVec2(cx - 6, p_min.y + 16),
            imgui.ImVec2(cx + 6, p_min.y + 16)
        ]
        dl.add_convex_poly_filled(top_tri, col_reticle)

        # Readout text overlays
        dl.add_text(imgui.ImVec2(p_min.x + 8, p_min.y + 8), col_white, f"ROLL: {roll_deg:+5.1f}°")
        dl.add_text(imgui.ImVec2(p_min.x + 8, p_min.y + 24), col_white, f"PITCH: {pitch_deg:+5.1f}°")

        # Frame border
        dl.add_rect(p_min, p_max, col_border, rounding=0.0, thickness=1.5)
        dl.pop_clip_rect()

        # Advance imgui cursor
        imgui.dummy(imgui.ImVec2(width, height))

    def _render_3d_quadcopter(self, width: float, height: float, roll_deg: float, pitch_deg: float, yaw_deg: float):
        """Draws the real-time 3D perspective quadcopter wireframe using Dear ImGui DrawList."""
        cursor = imgui.get_cursor_screen_pos()
        p_min = cursor
        p_max = imgui.ImVec2(cursor.x + width, cursor.y + height)

        dl = imgui.get_window_draw_list()
        dl.push_clip_rect(p_min, p_max, True)

        col_bg = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.04, 0.06, 0.10, 1.0))
        col_border = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.2, 0.3, 0.4, 1.0))
        col_hub = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.6, 0.7, 0.8, 1.0))
        col_front = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.2, 0.8, 1.0, 1.0)) # Cyan
        col_rear = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.95, 0.25, 0.35, 1.0)) # Coral
        col_m_disc = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.2, 0.9, 0.3, 0.9)) # Green prop
        col_white = imgui.color_convert_float4_to_u32(imgui.ImVec4(1.0, 1.0, 1.0, 1.0))

        dl.add_rect_filled(p_min, p_max, col_bg)

        cx = p_min.x + width * 0.5
        cy = p_min.y + height * 0.5

        # 3D Tait-Bryan rotation matrix
        R = rotation_matrix(roll_deg, pitch_deg, yaw_deg)

        # Perspective projection parameters
        cam_dist = 4.2
        scale = width * 0.35

        def project(pt3d):
            # Camera view transform: Isometric tilt looking slightly down (-30° elevation)
            view_rot = np.array([
                [1.0, 0.0, 0.0],
                [0.0, 0.866, 0.5],
                [0.0, -0.5, 0.866]
            ])
            p_rot = view_rot @ (R @ pt3d)
            x, y, z = p_rot[0], p_rot[1], p_rot[2]
            denom = cam_dist - z
            denom = max(denom, 0.5)
            sx = cx + (x / denom) * scale
            sy = cy - (y / denom) * scale
            return imgui.ImVec2(sx, sy)

        # 1. Draw Center Hub
        hub_screen = [project(p) for p in self.hub_pts]
        for i in range(len(hub_screen) - 1):
            dl.add_line(hub_screen[i], hub_screen[i+1], col_hub, 2.0)

        # 2. Draw Quad Arms
        # Front-Right to Rear-Left
        p_fr = project(self.arm_x[0])
        p_rl = project(self.arm_x[1])
        p_center = project(np.array([0.0, 0.0, 0.0]))
        dl.add_line(p_center, p_fr, col_front, 3.0)
        dl.add_line(p_center, p_rl, col_rear, 3.0)

        # Front-Left to Rear-Right
        p_fl = project(self.arm_y[0])
        p_rr = project(self.arm_y[1])
        dl.add_line(p_center, p_fl, col_front, 3.0)
        dl.add_line(p_center, p_rr, col_rear, 3.0)

        # 3. Draw Motor Propellers
        motor_labels = ["M1 (FR)", "M2 (RL)", "M3 (FL)", "M4 (RR)"]
        for idx, offset in enumerate(self.motor_offsets):
            disc_pts = [project(offset + p) for p in self.prop_circle]
            for i in range(len(disc_pts) - 1):
                dl.add_line(disc_pts[i], disc_pts[i+1], col_m_disc, 1.8)
            dl.add_line(disc_pts[-1], disc_pts[0], col_m_disc, 1.8)

            # Motor Center Hub
            m_center = project(offset)
            dl.add_circle_filled(m_center, 4.0, col_white)
            dl.add_text(imgui.ImVec2(m_center.x + 6, m_center.y - 12), col_white, motor_labels[idx])

        # Heading readout
        dl.add_text(imgui.ImVec2(p_min.x + 8, p_min.y + 8), col_white, f"HEADING: {yaw_deg:05.1f}°")
        dl.add_text(imgui.ImVec2(p_min.x + 8, p_min.y + 24), col_front, "Cyan: Front  |  Red: Rear")

        dl.add_rect(p_min, p_max, col_border, rounding=0.0, thickness=1.5)
        dl.pop_clip_rect()

        imgui.dummy(imgui.ImVec2(width, height))
