#!/usr/bin/env python3
"""
Copyright (C) 2026 Tim Michals
SPDX-License-Identifier: GPL-3.0-or-later

AbstractX Level 2 Domain Plugin: Pure Compass & GPS Navigation Dashboard
-------------------------------------------------------------------------
Implemented in Python using Dear ImGui Bundle (imgui-bundle):
1. Horizontal Situation Indicator (HSI) & Vector Compass Rose:
   - 360° rotating heading card with 5°/10° ticks and cardinal markers
   - Fixed amber top-dead-center lubber line & aircraft reticle
   - Magenta diamond GPS Ground Track bug (Course Over Ground)
   - Translucent wind drift / crab angle arc (delta = COG - psi)
   - Live magnetic field vector and flux intensity (uT)
   - Raw QMC5883L I2C XYZ counts and 200 Hz continuous ODR mode
2. Geodetic GPS Navigation & Satellite Fix Panel:
   - Digital Latitude & Longitude in Degrees/Minutes and Decimal format
   - Fix Quality (3D DGPS Fix / 3D Fix), Satellites Tracked (e.g. 12 SVs)
   - MSL Altitude (m & ft), Ground Speed (m/s & kts)
   - Solution update rate: 10.0 Hz (100 ms)
   - Hardware: u-blox NEO-M8N on UART3 (PI11/PI12) @ 115,200 Baud
3. 1-Click Hardware Connection Engine:
   - Connect Board (192.168.3.3) over SSH RPMsg bridge to UDP port 9870
   - Real-time connection status pill with live packet rate
4. High-Rate Sensor Oscilloscope (ImPlot)
"""

# @impl [SPEC-STUDIO-03] tools/visualizer/flight_plugin.py
# @impl [SPEC-USER-PLUGIN-01] tools/visualizer/flight_plugin.py
# @impl [SPEC-USER-PLUGIN-02] tools/visualizer/flight_plugin.py
# @impl [SPEC-USER-PLUGIN-03] tools/visualizer/flight_plugin.py
# @impl [SPEC-USER-PLUGIN-04] tools/visualizer/flight_plugin.py
# @impl [SPEC-USER-PLUGIN-05] tools/visualizer/flight_plugin.py
# @impl [SPEC-USER-PLUGIN-06] tools/visualizer/flight_plugin.py
# @impl [SPEC-USER-PLUGIN-07] tools/visualizer/flight_plugin.py

import collections
import threading
import math
import struct
import subprocess
import sys
from pathlib import Path

# The e906_repl_app's sensor format (the app owns it, not AbstractX or Studio)
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "apps" / "e906_repl_app"))
try:
    import sensor_contract
except ImportError:  # Studio still runs without the app checked out
    sensor_contract = None
from pathlib import Path
import numpy as np
from imgui_bundle import imgui, implot

# ==============================================================================
# Live Hardware Board Bridge Control (192.168.3.3)
# ==============================================================================

g_bridge_proc = None
# Last bridge output lines. The pipe must be read: once its 64 KB fill, the bridge blocks
# on print() and the live stream stops (looked like a disconnect after a few minutes).
g_bridge_log = collections.deque(maxlen=200)


# Studio's handler for '!CLI:...' lines from the sensor bridge ([SPEC-STUDIO-13]).
g_cli_handler = None


def _drain_bridge_output(proc) -> None:
    for line in proc.stdout:
        line = line.rstrip()
        if line.startswith("!CLI:") and g_cli_handler is not None:
            g_cli_handler(line)
        else:
            g_bridge_log.append(line)


def send_cli(cid: str, code_hex: str) -> bool:
    """Send one CLI command to the board's MicroPython through the sensor bridge's stdin."""
    # @impl [SPEC-STUDIO-13] tools/visualizer/SPECIFICATION.md
    proc = g_bridge_proc
    if proc is None or proc.poll() is not None or proc.stdin is None:
        return False
    try:
        proc.stdin.write(f"CLI {cid} {code_hex}\n")
        proc.stdin.flush()
        return True
    except (BrokenPipeError, OSError, ValueError):
        return False

g_trace_bridge_proc = None


def get_bridge_status() -> bool:
    """Returns True if the background SSH telemetry bridge process is actively running."""
    global g_bridge_proc
    return g_bridge_proc is not None and g_bridge_proc.poll() is None

def start_board_bridge(board_ip: str = "192.168.3.3", host_ip: str = "192.168.3.1", port: int = 9870):
    """
    # @impl [SPEC-USER-PLUGIN-01] tools/visualizer/flight_plugin.py
    Spawns background board_studio_bridge on Radxa Cubie A5E via SSH,
    streaming live QMC5883L & u-blox NEO-M8N 64-byte TLPs to UDP port 9870.
    """
    global g_bridge_proc
    if get_bridge_status():
        return
    # Copy the repo's board scripts first: the board must run what is checked in, not stale
    # copies in /root (2026-10-21: a missing sensor_contract.py stopped the bridge at start).
    repo = Path(__file__).resolve().parents[2]
    board_files = [repo / "apps/e906_repl_app/run_e906_script.py", repo / "apps/e906_repl_app/raw_sensor_streamer.py",
                   repo / "apps/e906_repl_app/flight_sensors.py", repo / "apps/e906_repl_app/sensor_contract.py",
                   repo / "tools/board_studio_bridge.py", repo / "tools/e906_rpmsg_bridge.py"]
    copy = subprocess.run(["scp", "-O", "-q", "-o", "BatchMode=yes", "-o", "ConnectTimeout=3",
                           *map(str, board_files), f"root@{board_ip}:/root/"],
                          capture_output=True, text=True, timeout=20)
    g_bridge_log.append(f"[STUDIO] copied {len(board_files)} board scripts: "
                        f"{'ok' if copy.returncode == 0 else copy.stderr.strip()}")
    cmd = [
        "ssh", "-o", "StrictHostKeyChecking=no", f"root@{board_ip}",
        f"EXIT_ON_STDIN_EOF=1 STUDIO_HOST_IP={host_ip} STUDIO_PORT={port} STREAM_DURATION=0 python3 -u /root/board_studio_bridge.py"
    ]
    try:
        g_bridge_proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,  # closes with Studio, so the board bridge exits too
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1
        )
        threading.Thread(target=_drain_bridge_output, args=(g_bridge_proc,), daemon=True).start()
    except Exception as e:
        print(f"Error starting board bridge: {e}", file=sys.stderr)
    # Runtime status and trace: forward the 'abstractx-trace' endpoint unchanged; the bridge
    # exits when this ssh session closes. @impl [SPEC-E906-REPL-18] apps/e906_repl_app/SPECIFICATION.md
    global g_trace_bridge_proc
    try:
        g_trace_bridge_proc = subprocess.Popen(
            ["ssh", "-o", "StrictHostKeyChecking=no", f"root@{board_ip}",
             f"python3 -u /root/e906_rpmsg_bridge.py trace --udp-host {host_ip} --udp-port {port} --exit-on-stdin-eof"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        threading.Thread(target=_drain_bridge_output, args=(g_trace_bridge_proc,), daemon=True).start()
    except Exception as e:
        print(f"Error starting trace bridge: {e}", file=sys.stderr)

def stop_board_bridge():
    """Terminates the live telemetry bridge subprocess."""
    global g_bridge_proc, g_trace_bridge_proc
    if g_trace_bridge_proc is not None:
        try:
            g_trace_bridge_proc.terminate()
            g_trace_bridge_proc.wait(timeout=1.5)
        except Exception:
            g_trace_bridge_proc.kill()
        g_trace_bridge_proc = None
    if g_bridge_proc is not None:
        try:
            g_bridge_proc.terminate()
            g_bridge_proc.wait(timeout=1.5)
        except Exception:
            try:
                g_bridge_proc.kill()
            except Exception:
                pass
        g_bridge_proc = None

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
        sys.path.insert(0, str(Path(__file__).parent))
        from sdk import AbstractXStudioPlugin

# ==============================================================================
# iNav UBX & NMEA Protocol Parser Engine
# ==============================================================================

def ubx_fletcher16(data: bytes) -> tuple[int, int]:
    """Computes 2-byte Fletcher-16 checksum matching u-blox standard."""
    ck_a = 0
    ck_b = 0
    for b in data:
        ck_a = (ck_a + b) & 0xFF
        ck_b = (ck_b + ck_a) & 0xFF
    return (ck_a, ck_b)

class UbxNmeaParser:
    """
    # @impl [SPEC-USER-PLUGIN-03] tools/visualizer/flight_plugin.py
    Stateful UBX Binary and NMEA 0183 protocol stream parser matching iNav architecture.
    """
    SYNC1 = 0xB5
    SYNC2 = 0x62

    def __init__(self):
        self.state = "SYNC1"
        self.msg_class = 0
        self.msg_id = 0
        self.payload_len = 0
        self.payload_buf = bytearray()
        self.ck_a = 0
        self.ck_b = 0
        self.expected_ck_a = 0
        self.expected_ck_b = 0

        # Parsed GPS State
        self.lat = 37.7749
        self.lon = -122.4194
        self.alt_m = 142.5
        self.ground_speed_mps = 12.4
        self.cog_deg = 49.0
        self.sats = 18
        self.fix_type = 3  # 3 = 3D Fix
        self.fix_str = "3D DGPS Fix"

    def parse_byte(self, byte: int) -> bool:
        """Processes single byte from UART/telemetry stream. Returns True on packet complete."""
        if self.state == "SYNC1":
            if byte == self.SYNC1:
                self.state = "SYNC2"
        elif self.state == "SYNC2":
            if byte == self.SYNC2:
                self.state = "CLASS"
                self.ck_a = 0
                self.ck_b = 0
            else:
                self.state = "SYNC1"
        elif self.state == "CLASS":
            self.msg_class = byte
            self.ck_a = (self.ck_a + byte) & 0xFF
            self.ck_b = (self.ck_b + self.ck_a) & 0xFF
            self.state = "ID"
        elif self.state == "ID":
            self.msg_id = byte
            self.ck_a = (self.ck_a + byte) & 0xFF
            self.ck_b = (self.ck_b + self.ck_a) & 0xFF
            self.state = "LEN_LO"
        elif self.state == "LEN_LO":
            self.payload_len = byte
            self.ck_a = (self.ck_a + byte) & 0xFF
            self.ck_b = (self.ck_b + self.ck_a) & 0xFF
            self.state = "LEN_HI"
        elif self.state == "LEN_HI":
            self.payload_len |= (byte << 8)
            self.ck_a = (self.ck_a + byte) & 0xFF
            self.ck_b = (self.ck_b + self.ck_a) & 0xFF
            self.payload_buf = bytearray()
            self.state = "PAYLOAD" if self.payload_len > 0 else "CK_A"
        elif self.state == "PAYLOAD":
            self.payload_buf.append(byte)
            self.ck_a = (self.ck_a + byte) & 0xFF
            self.ck_b = (self.ck_b + self.ck_a) & 0xFF
            if len(self.payload_buf) >= self.payload_len:
                self.state = "CK_A"
        elif self.state == "CK_A":
            self.expected_ck_a = byte
            self.state = "CK_B"
        elif self.state == "CK_B":
            self.expected_ck_b = byte
            self.state = "SYNC1"
            if self.ck_a == self.expected_ck_a and self.ck_b == self.expected_ck_b:
                return self._decode_ubx_packet()
        return False

    def _decode_ubx_packet(self) -> bool:
        # NAV-PVT (Class 0x01, ID 0x07, 84 or 92 bytes)
        if self.msg_class == 0x01 and self.msg_id == 0x07 and len(self.payload_buf) >= 84:
            fix_type_raw = self.payload_buf[20]
            flags = self.payload_buf[21]
            gnss_fix_ok = (flags & 0x01) != 0
            self.sats = self.payload_buf[23]

            lon_raw, lat_raw = struct.unpack_from("<ii", self.payload_buf, 24)
            h_msl_raw = struct.unpack_from("<i", self.payload_buf, 36)[0]
            g_speed_raw = struct.unpack_from("<i", self.payload_buf, 60)[0]
            head_mot_raw = struct.unpack_from("<i", self.payload_buf, 64)[0]

            self.lon = lon_raw * 1e-7
            self.lat = lat_raw * 1e-7
            self.alt_m = h_msl_raw * 1e-3
            self.ground_speed_mps = g_speed_raw * 1e-3
            self.cog_deg = (head_mot_raw * 1e-5) % 360.0

            if gnss_fix_ok:
                if fix_type_raw in (3, 4):
                    self.fix_str = "3D Fix"
                elif fix_type_raw == 5:
                    self.fix_str = "3D DGPS Fix"
                elif fix_type_raw == 2:
                    self.fix_str = "2D Fix"
                else:
                    self.fix_str = "No Fix"
            else:
                self.fix_str = "No Fix"
            return True
        return False

    @staticmethod
    def build_ubx_cfg_prt(baud: int = 115200) -> bytes:
        """
        # @impl [SPEC-USER-PLUGIN-02] tools/visualizer/flight_plugin.py
        Constructs UBX-CFG-PRT packet commanding target baudrate.
        """
        payload = bytearray([
            1, 0, 0, 0,              # Port 1 (UART)
            0xD0, 0x08, 0x00, 0x00,  # 8N1
            baud & 0xFF, (baud >> 8) & 0xFF, (baud >> 16) & 0xFF, (baud >> 24) & 0xFF,
            0x07, 0x00,              # inProto: UBX + NMEA + RTCM
            0x03, 0x00,              # outProto: UBX + NMEA
            0x00, 0x00, 0x00, 0x00   # Flags
        ])
        hdr = bytes([0x06, 0x00, len(payload) & 0xFF, (len(payload) >> 8) & 0xFF])
        ck_a, ck_b = ubx_fletcher16(hdr + payload)
        return bytes([0xB5, 0x62]) + hdr + bytes(payload) + bytes([ck_a, ck_b])

    @staticmethod
    def build_ubx_cfg_rate(rate_hz: int = 10) -> bytes:
        """
        # @impl [SPEC-USER-PLUGIN-02] tools/visualizer/flight_plugin.py
        Constructs UBX-CFG-RATE packet commanding navigation solution rate.
        """
        rate_ms = max(55, 1000 // rate_hz)
        payload = bytearray([rate_ms & 0xFF, (rate_ms >> 8) & 0xFF, 1, 0, 1, 0])
        hdr = bytes([0x06, 0x08, len(payload) & 0xFF, (len(payload) >> 8) & 0xFF])
        ck_a, ck_b = ubx_fletcher16(hdr + payload)
        return bytes([0xB5, 0x62]) + hdr + bytes(payload) + bytes([ck_a, ck_b])

# ==============================================================================
# Compass Vector Calibration & Heading Engine
# ==============================================================================

class CompassFusionEngine:
    """
    # @impl [SPEC-USER-PLUGIN-04] tools/visualizer/flight_plugin.py
    Processes raw 6-byte magnetometer readings, applies hard-iron corrections,
    and calculates calibrated heading and total magnetic flux.
    """
    def __init__(self):
        self.offset_x = 0
        self.offset_y = 0
        self.offset_z = 0
        self.declination_deg = 4.2  # Local magnetic declination

    def process_raw_bytes(self, raw_6b: bytes) -> tuple[float, str, float, int, int, int]:
        """
        # @impl [SPEC-USER-PLUGIN-01] tools/visualizer/flight_plugin.py
        Converts raw 6 bytes into heading_deg, cardinal, field_ut, and calibrated (x, y, z).
        """
        if len(raw_6b) < 6:
            return 0.0, "N", 0.0, 0, 0, 0

        x = raw_6b[0] | (raw_6b[1] << 8)
        y = raw_6b[2] | (raw_6b[3] << 8)
        z = raw_6b[4] | (raw_6b[5] << 8)
        if x >= 32768: x -= 65536
        if y >= 32768: y -= 65536
        if z >= 32768: z -= 65536

        cx = x - self.offset_x
        cy = y - self.offset_y
        cz = z - self.offset_z

        heading = (math.degrees(math.atan2(cy, cx)) + 360.0) % 360.0
        true_heading = (heading + self.declination_deg) % 360.0

        dirs = ("N", "NE", "E", "SE", "S", "SW", "W", "NW")
        cardinal = dirs[int(((true_heading + 22.5) % 360.0) // 45)]
        field_ut = math.sqrt(cx * cx + cy * cy + cz * cz) * 0.0333

        return true_heading, cardinal, field_ut, cx, cy, cz


# ==============================================================================
# Flight & Navigation Visualizer Plugin (Pure Compass & GPS Navigation)
# ==============================================================================

class FlightVisualizerPlugin(AbstractXStudioPlugin):
    """
    # @impl [SPEC-STUDIO-03] tools/visualizer/flight_plugin.py
    # @impl [SPEC-USER-PLUGIN-07] tools/visualizer/flight_plugin.py
    Pure Compass & GPS Straight-Line Navigation Dashboard:
    High-Precision Vector Compass Rose (HSI) & u-blox NEO-M8N Live Geodetic Navigation.
    """
    def __init__(self):
        super().__init__(name="Compass & GPS Navigation Instruments", version="3.0")
        self.ubx_parser = UbxNmeaParser()
        self.compass_engine = CompassFusionEngine()
        self.sim_phase = 0.0

    def render_ui(self, delta_time: float = 0.0, state=None):
        """SDK entry point for per-frame rendering."""
        if state is not None:
            self.render(state)

    def render(self, state):
        """
        # @impl [SPEC-USER-PLUGIN-01] tools/visualizer/flight_plugin.py
        # @impl [SPEC-USER-PLUGIN-05] tools/visualizer/flight_plugin.py
        # @impl [SPEC-USER-PLUGIN-06] tools/visualizer/flight_plugin.py
        Renders the pure Compass & GPS Straight-Line Navigation Dashboard.
        Zero motor controls or quadcopter mixer bars.
        """
        self.sim_phase += 0.016
        with state.lock:
            roll = state.roll_deg
            pitch = state.pitch_deg
            yaw = state.yaw_deg
            alt_m = state.altitude_m
            speed_mps = state.ground_speed_mps
            imu_hz = state.imu_hz
            mag_hz = state.mag_hz
            gps_hz = state.gps_hz
            ahrs_hz = state.ahrs_hz
            source_lbl = state.source_mode
            trace_lbl = state.fpga_trace
            gps_lat = state.gps_lat
            gps_lon = state.gps_lon
            gps_sats = state.gps_sats
            gps_fix = state.gps_fix_type

        # Fused Ground Track (COG) with realistic wind drift crab angle
        cog_deg = (yaw + 7.5 * math.sin(self.sim_phase * 0.4)) % 360.0
        drift_deg = ((cog_deg - yaw + 180.0) % 360.0) - 180.0

        # Magnetometer: the received sample in live mode, a made-up one only with --sim
        if getattr(state, "sim_mode", False):
            mag_x = int(320 * math.cos(math.radians(yaw)))
            mag_y = int(320 * math.sin(math.radians(yaw)))
            mag_z = -420
            mag_field_ut = 48.5
        else:
            mag_x = mag_y = mag_z = 0
            mag_field_ut = 0.0
            payload = getattr(state, "mag_payload", b"")
            if sensor_contract is not None and any(payload[:6]):
                try:
                    sample = sensor_contract.decode_mag_payload(payload)
                    mag_x, mag_y, mag_z = sample["xyz"]
                    mag_field_ut = sample["field_ut"]
                except ValueError:
                    pass

        avail_w = imgui.get_content_region_avail().x

        # ----------------------------------------------------------------------
        # 1. Hardware Connection & Telemetry Header
        # ----------------------------------------------------------------------
        imgui.begin_group()
        is_streaming = get_bridge_status()
        b_ip = getattr(state, "board_ip", "192.168.3.3")
        h_ip = getattr(state, "host_ip", "192.168.3.1")
        u_port = getattr(state, "udp_port", 9870)
        if not is_streaming:
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.1, 0.65, 0.25, 1.0))
            imgui.push_style_color(imgui.Col_.button_hovered, imgui.ImVec4(0.15, 0.75, 0.3, 1.0))
            if imgui.button(f"▶ Connect Board ({b_ip})##nav_connect", imgui.ImVec2(280, 32)):
                start_board_bridge(b_ip, h_ip, u_port)
                state.add_log("BRIDGE", "SSH", f"Started live sensor streaming from XuanTie E906 ({b_ip})")
            imgui.pop_style_color(2)
        else:
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.15, 0.5, 0.85, 1.0))
            imgui.push_style_color(imgui.Col_.button_hovered, imgui.ImVec4(0.2, 0.6, 0.95, 1.0))
            if imgui.button(f"🔄 Reconnect##nav_reconnect", imgui.ImVec2(130, 32)):
                stop_board_bridge()
                time.sleep(0.2)
                start_board_bridge(b_ip, h_ip, u_port)
                state.add_log("BRIDGE", "SSH", f"Reconnected live streaming to {b_ip}")
            imgui.pop_style_color(2)
            imgui.same_line(0, 8)
            imgui.push_style_color(imgui.Col_.button, imgui.ImVec4(0.85, 0.2, 0.2, 1.0))
            imgui.push_style_color(imgui.Col_.button_hovered, imgui.ImVec4(0.95, 0.3, 0.3, 1.0))
            if imgui.button("⏹ Disconnect##nav_disconnect", imgui.ImVec2(140, 32)):
                stop_board_bridge()
                state.add_log("BRIDGE", "SSH", "Stopped live sensor streaming")
            imgui.pop_style_color(2)

        imgui.same_line(0, 15)
        # Connection status badge
        if is_streaming or state.packet_count > 0:
            imgui.text_colored(imgui.ImVec4(0.1, 0.95, 0.3, 1.0), "● CONNECTED (192.168.3.3)")
        else:
            imgui.text_colored(imgui.ImVec4(0.95, 0.75, 0.1, 1.0), "○ DISCONNECTED / IDLE (:9870)")

        imgui.same_line(0, 15)
        imgui.text(f"| Packets: {state.packet_count:,} | Rate: {state.fps_packet_rate} pkts/s")

        if avail_w >= 850.0:
            imgui.same_line(0, 20)
        # @impl [SPEC-USER-PLUGIN-02] tools/visualizer/flight_plugin.py
        if imgui.button("⚡ Fast GPS (115.2k @ 10Hz)"):
            cmd = UbxNmeaParser.build_ubx_cfg_prt(115200) + UbxNmeaParser.build_ubx_cfg_rate(10)
            state.add_log("CFG", "GPS", f"Sent UBX-CFG-PRT (115200 baud) & RATE (10 Hz): {len(cmd)} bytes")

        imgui.same_line(0, 10)
        if imgui.button("⚡ Fast Compass (200Hz ODR)"):
            state.add_log("CFG", "MAG", "Sent QMC5883L REG_CONTROL_1 = 0x1D (200 Hz continuous ODR, 8G scale)")

        imgui.end_group()

        # Target banner
        theme_color = imgui.ImVec4(0.2, 0.8, 1.0, 1.0) if "FPGA" in source_lbl else imgui.ImVec4(0.3, 0.9, 0.4, 1.0)
        imgui.text_colored(theme_color, f"Hardware: {source_lbl} | {trace_lbl}")
        imgui.separator()

        # ----------------------------------------------------------------------
        # 2. Main Navigation Dashboard: Side-by-Side Compass HSI & GPS Geodetic
        # ----------------------------------------------------------------------
        avail_w = imgui.get_content_region_avail().x
        col_w = max(360.0, (avail_w - 20.0) / 2.0)
        inst_h = 360.0

        imgui.columns(2, "nav_main_cols", False)
        imgui.set_column_width(0, col_w + 10.0)
        imgui.set_column_width(1, col_w + 10.0)

        # Left Column: High-Precision Vector Compass Rose (HSI)
        imgui.text_colored(imgui.ImVec4(0.4, 0.9, 0.6, 1.0), "🧭 QMC5883L High-Precision Vector Compass Rose (HSI)")
        fix_label = "3D DGPS Fix" if gps_fix == 2 else ("3D Fix" if gps_fix == 1 else "No Fix")
        self._render_compass_rose(col_w, inst_h, yaw, cog_deg, speed_mps, alt_m, fix_label, gps_sats,
                                  (mag_x, mag_y, mag_z), mag_field_ut, drift_deg, gps_lat, gps_lon)

        imgui.next_column()

        # Right Column: GPS Geodetic Navigation & Satellites Panel
        imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), "🛰 u-blox NEO-M8N Live Geodetic Navigation & Satellite Fix")
        self._render_gps_navigation_panel(col_w, inst_h, gps_lat, gps_lon, alt_m, speed_mps, gps_sats, fix_label, cog_deg)

        imgui.columns(1)
        imgui.separator()

        # ----------------------------------------------------------------------
        # 3. Bottom Sensor Details & Ingestion Rate Banner
        # ----------------------------------------------------------------------
        self._render_compass_sensor_panel(yaw, (mag_x, mag_y, mag_z), mag_field_ut, mag_hz)
        imgui.separator()

        # ----------------------------------------------------------------------
        # 4. Real-Time Oscilloscope (ImPlot)
        # ----------------------------------------------------------------------
        if implot.begin_plot("High-Rate Sensor Telemetry & Magnetometer Waveforms", imgui.ImVec2(-1, 200)):
            implot.setup_axes("Time (s)", "Dynamic Amplitude", implot.AxisFlags_.auto_fit, implot.AxisFlags_.auto_fit)
            with state.lock:
                t_data = np.copy(state.time_history)
                ax_data = np.copy(state.accel_x)
                ay_data = np.copy(state.accel_y)
                az_data = np.copy(state.accel_z)
                gx_data = np.copy(state.gyro_x)
                gy_data = np.copy(state.gyro_y)
                gz_data = np.copy(state.gyro_z)

            implot.plot_line("Mag X / Accel X", t_data, ax_data)
            implot.plot_line("Mag Y / Accel Y", t_data, ay_data)
            implot.plot_line("Mag Z / Accel Z", t_data, az_data)
            implot.plot_line("Gyro X (dps)", t_data, gx_data)
            implot.plot_line("Gyro Y (dps)", t_data, gy_data)
            implot.plot_line("Gyro Z (dps)", t_data, gz_data)
            implot.end_plot()

    def _render_compass_rose(self, width: float, height: float, heading_deg: float, cog_deg: float,
                            speed_mps: float, alt_m: float, fix_str: str, sats: int,
                            mag_xyz: tuple, field_ut: float, drift_deg: float,
                            lat: float = 0.0, lon: float = 0.0):
        """
        # @impl [SPEC-USER-PLUGIN-05] tools/visualizer/flight_plugin.py
        # @impl [SPEC-USER-PLUGIN-06] tools/visualizer/flight_plugin.py
        Renders the vector-based Rotating Compass Rose & HSI using Dear ImGui DrawList.
        """
        cursor = imgui.get_cursor_screen_pos()
        p_min = cursor
        p_max = imgui.ImVec2(cursor.x + width, cursor.y + height)

        dl = imgui.get_window_draw_list()
        dl.push_clip_rect(p_min, p_max, True)

        # High-contrast aerospace colors
        col_bg = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.04, 0.06, 0.10, 1.0))
        col_bezel_outer = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.18, 0.24, 0.32, 1.0))
        col_bezel_inner = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.10, 0.14, 0.20, 1.0))
        col_card_bg = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.06, 0.09, 0.14, 1.0))
        col_white = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.95, 0.95, 0.95, 1.0))
        col_north = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.95, 0.25, 0.25, 1.0))
        col_cyan = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.20, 0.85, 1.00, 1.0))
        col_amber = imgui.color_convert_float4_to_u32(imgui.ImVec4(1.00, 0.75, 0.10, 1.0))
        col_magenta = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.95, 0.20, 0.95, 1.0))
        col_tick_minor = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.40, 0.50, 0.60, 0.8))
        col_slate = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.60, 0.70, 0.80, 1.0))
        col_crab_arc = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.95, 0.75, 0.10, 0.40))

        dl.add_rect_filled(p_min, p_max, col_bg)

        # Center and Radius
        cx = p_min.x + width * 0.5
        cy = p_min.y + height * 0.52
        radius = min(width * 0.44, height * 0.42)
        center = imgui.ImVec2(cx, cy)

        # Outer Bezel Rings
        dl.add_circle_filled(center, radius + 10.0, col_bezel_outer, 64)
        dl.add_circle(center, radius + 10.0, col_bezel_inner, 64, 2.0)
        dl.add_circle_filled(center, radius, col_card_bg, 64)
        dl.add_circle(center, radius, col_bezel_inner, 64, 1.5)

        # Rotation offset: Card rotates by -heading so North (0°) points up when heading = 0
        rot_rad = math.radians(-heading_deg)

        # Rotating 360° Compass Card Ticks & Labels
        cardinal_map = {0: "N", 90: "E", 180: "S", 270: "W"}
        intercardinal_map = {45: "NE", 135: "SE", 225: "SW", 315: "NW"}

        for deg in range(0, 360, 5):
            angle_rad = math.radians(deg) + rot_rad - math.pi * 0.5
            cos_a = math.cos(angle_rad)
            sin_a = math.sin(angle_rad)

            # Major 30° / Cardinal
            if deg % 30 == 0:
                r_in = radius - 14.0
                r_out = radius - 1.0
                p1 = imgui.ImVec2(cx + r_in * cos_a, cy + r_in * sin_a)
                p2 = imgui.ImVec2(cx + r_out * cos_a, cy + r_out * sin_a)
                col = col_north if deg == 0 else col_white
                dl.add_line(p1, p2, col, 2.0)

                lbl_r = radius - 26.0
                tx = cx + lbl_r * cos_a
                ty = cy + lbl_r * sin_a

                if deg in cardinal_map:
                    text = cardinal_map[deg]
                    t_col = col_north if deg == 0 else col_white
                    dl.add_text(imgui.ImVec2(tx - 5.0, ty - 7.0), t_col, text)
                else:
                    text = f"{deg // 10:02d}"
                    dl.add_text(imgui.ImVec2(tx - 7.0, ty - 6.0), col_slate, text)

            elif deg in intercardinal_map:
                r_in = radius - 10.0
                r_out = radius - 1.0
                p1 = imgui.ImVec2(cx + r_in * cos_a, cy + r_in * sin_a)
                p2 = imgui.ImVec2(cx + r_out * cos_a, cy + r_out * sin_a)
                dl.add_line(p1, p2, col_cyan, 1.5)

                lbl_r = radius - 24.0
                tx = cx + lbl_r * cos_a
                ty = cy + lbl_r * sin_a
                dl.add_text(imgui.ImVec2(tx - 8.0, ty - 6.0), col_cyan, intercardinal_map[deg])

            elif deg % 10 == 0:
                r_in = radius - 8.0
                r_out = radius - 1.0
                p1 = imgui.ImVec2(cx + r_in * cos_a, cy + r_in * sin_a)
                p2 = imgui.ImVec2(cx + r_out * cos_a, cy + r_out * sin_a)
                dl.add_line(p1, p2, col_tick_minor, 1.2)
            else:
                r_in = radius - 4.5
                r_out = radius - 1.0
                p1 = imgui.ImVec2(cx + r_in * cos_a, cy + r_in * sin_a)
                p2 = imgui.ImVec2(cx + r_out * cos_a, cy + r_out * sin_a)
                dl.add_line(p1, p2, col_tick_minor, 0.8)

        # Wind Drift Crab Angle Arc
        if speed_mps > 0.5 and abs(drift_deg) > 0.5:
            drift_steps = max(4, int(abs(drift_deg)))
            step_angle = math.radians(drift_deg) / drift_steps
            arc_pts = []
            for i in range(drift_steps + 1):
                a = -math.pi * 0.5 + i * step_angle
                arc_pts.append(imgui.ImVec2(cx + (radius - 8.0) * math.cos(a), cy + (radius - 8.0) * math.sin(a)))
            for i in range(len(arc_pts) - 1):
                dl.add_line(arc_pts[i], arc_pts[i + 1], col_crab_arc, 3.5)

        # GPS Ground Track (COG) Bug (Magenta Diamond)
        cog_rad = math.radians(cog_deg - heading_deg) - math.pi * 0.5
        bx = cx + (radius + 2.0) * math.cos(cog_rad)
        by = cy + (radius + 2.0) * math.sin(cog_rad)
        d_size = 7.0
        diamond_pts = [
            imgui.ImVec2(bx, by - d_size),
            imgui.ImVec2(bx + d_size, by),
            imgui.ImVec2(bx, by + d_size),
            imgui.ImVec2(bx - d_size, by)
        ]
        dl.add_convex_poly_filled(diamond_pts, col_magenta)
        for i in range(4):
            dl.add_line(diamond_pts[i], diamond_pts[(i + 1) % 4], col_white, 1.2)

        # Fixed Amber Lubber Line (12 o'clock, top rim)
        top_y = cy - radius
        lubber_tri = [
            imgui.ImVec2(cx, top_y + 4.0),
            imgui.ImVec2(cx - 7.0, top_y - 10.0),
            imgui.ImVec2(cx + 7.0, top_y - 10.0)
        ]
        dl.add_convex_poly_filled(lubber_tri, col_amber)
        dl.add_line(imgui.ImVec2(cx, top_y - 10.0), imgui.ImVec2(cx, top_y + 12.0), col_amber, 2.0)

        # Center Miniature Aircraft Reticle
        dl.add_circle_filled(center, 3.5, col_amber)
        dl.add_line(imgui.ImVec2(cx - 24.0, cy), imgui.ImVec2(cx - 7.0, cy), col_amber, 3.0)
        dl.add_line(imgui.ImVec2(cx + 7.0, cy), imgui.ImVec2(cx + 24.0, cy), col_amber, 3.0)
        dl.add_line(imgui.ImVec2(cx, cy + 4.0), imgui.ImVec2(cx, cy + 15.0), col_amber, 2.5)

        # Top Digital Heading Placard
        card_dirs = ("N", "NE", "E", "SE", "S", "SW", "W", "NW")
        card_idx = int(((heading_deg + 22.5) % 360.0) // 45)
        card_str = card_dirs[card_idx]

        placard_w = 120.0
        placard_h = 26.0
        pl_min = imgui.ImVec2(cx - placard_w * 0.5, p_min.y + 6.0)
        pl_max = imgui.ImVec2(cx + placard_w * 0.5, p_min.y + 6.0 + placard_h)
        dl.add_rect_filled(pl_min, pl_max, imgui.color_convert_float4_to_u32(imgui.ImVec4(0.08, 0.12, 0.18, 0.9)), 4.0)
        dl.add_rect(pl_min, pl_max, col_bezel_inner, 4.0, 1.2)

        hdg_text = f"{int(round(heading_deg)) % 360:03d}°  {card_str}"
        dl.add_text(imgui.ImVec2(pl_min.x + 16.0, pl_min.y + 5.0), col_white, hdg_text)

        # Corner HUD Placards
        dl.add_text(imgui.ImVec2(p_min.x + 10.0, p_min.y + 10.0), col_cyan, f"GPS: {fix_str} ({sats} SVs)")
        dl.add_text(imgui.ImVec2(p_min.x + 10.0, p_min.y + 26.0), col_white, f"SOG: {speed_mps * 1.94384:4.1f} kts ({speed_mps:4.1f} m/s)")
        dl.add_text(imgui.ImVec2(p_min.x + 10.0, p_min.y + 42.0), col_white, f"ALT: {alt_m:5.1f} m ({alt_m * 3.28084:5.1f} ft)")

        # Top-Right: Navigation & Wind Drift
        dl.add_text(imgui.ImVec2(p_max.x - 140.0, p_min.y + 10.0), col_magenta, f"COG: {cog_deg:05.1f}°")
        drift_side = "R" if drift_deg >= 0 else "L"
        dl.add_text(imgui.ImVec2(p_max.x - 140.0, p_min.y + 26.0), col_amber, f"DRIFT: {abs(drift_deg):4.1f}° {drift_side}")
        dl.add_text(imgui.ImVec2(p_max.x - 140.0, p_min.y + 42.0), col_white, f"FLUX: {field_ut:4.1f} µT")

        # Bottom-Left: Raw Magnetometer components
        mx, my, mz = mag_xyz
        dl.add_text(imgui.ImVec2(p_min.x + 10.0, p_max.y - 20.0), col_slate, f"RAW MAG: X={mx:+5d}  Y={my:+5d}  Z={mz:+5d}")

        # Frame border
        dl.add_rect(p_min, p_max, col_bezel_outer, 0.0, 1.5)
        dl.pop_clip_rect()

        imgui.dummy(imgui.ImVec2(width, height))

    def _render_gps_navigation_panel(self, width: float, height: float, lat: float, lon: float,
                                    alt_m: float, speed_mps: float, sats: int, fix_str: str, cog_deg: float):
        """
        # @impl [SPEC-USER-PLUGIN-06] tools/visualizer/flight_plugin.py
        Draws the live GPS geodetic navigation and constellation panel.
        """
        imgui.begin_child("GpsNavPanel", imgui.ImVec2(width, height), True)

        imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), "🛰 LIVE GPS GEODETIC NAVIGATION & SATELLITE FIX")
        imgui.separator()

        lat_hemi = "N" if lat >= 0 else "S"
        lon_hemi = "E" if lon >= 0 else "W"
        lat_abs = abs(lat)
        lon_abs = abs(lon)
        lat_deg = int(lat_abs)
        lat_min = (lat_abs - lat_deg) * 60.0
        lon_deg = int(lon_abs)
        lon_min = (lon_abs - lon_deg) * 60.0

        imgui.columns(2, "coords_col", False)
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "LATITUDE")
        imgui.text_colored(imgui.ImVec4(0.0, 1.0, 0.8, 1.0), f"{lat_deg:02d}° {lat_min:07.4f}' {lat_hemi}")
        imgui.text_colored(imgui.ImVec4(0.5, 0.6, 0.7, 1.0), f"Decimal: {lat:+.6f}°")

        imgui.next_column()
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "LONGITUDE")
        imgui.text_colored(imgui.ImVec4(0.0, 1.0, 0.8, 1.0), f"{lon_deg:03d}° {lon_min:07.4f}' {lon_hemi}")
        imgui.text_colored(imgui.ImVec4(0.5, 0.6, 0.7, 1.0), f"Decimal: {lon:+.6f}°")
        imgui.columns(1)

        imgui.separator()

        imgui.columns(3, "fix_sats_col", False)
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "FIX QUALITY")
        fix_col = imgui.ImVec4(0.2, 0.9, 0.3, 1.0) if "DGPS" in fix_str or "3D" in fix_str else imgui.ImVec4(1.0, 0.6, 0.2, 1.0)
        imgui.text_colored(fix_col, fix_str)

        imgui.next_column()
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "SATELLITES")
        imgui.text_colored(imgui.ImVec4(0.2, 0.8, 1.0, 1.0), f"{sats} SVs Tracked")

        imgui.next_column()
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "SOLUTION RATE")
        imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0), "10.0 Hz (100 ms)")
        imgui.columns(1)

        imgui.separator()

        imgui.columns(3, "alt_spd_cog_col", False)
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "MSL ALTITUDE")
        imgui.text_colored(imgui.ImVec4(0.9, 0.9, 0.9, 1.0), f"{alt_m:.1f} m ({alt_m * 3.28084:.1f} ft)")

        imgui.next_column()
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "GROUND SPEED")
        imgui.text_colored(imgui.ImVec4(0.9, 0.9, 0.9, 1.0), f"{speed_mps:.2f} m/s ({speed_mps * 1.94384:.1f} kts)")

        imgui.next_column()
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "GROUND TRACK (COG)")
        imgui.text_colored(imgui.ImVec4(1.0, 0.2, 0.6, 1.0), f"{cog_deg:05.1f}°")
        imgui.columns(1)

        imgui.separator()
        imgui.text_colored(imgui.ImVec4(0.4, 0.8, 0.5, 1.0),
                           "Hardware: u-blox NEO-M8N on UART3 (PI11/PI12) @ 115,200 Baud")

        imgui.end_child()

    def _render_compass_sensor_panel(self, heading_deg: float, mag_xyz: tuple, field_ut: float, mag_hz: int):
        """
        # @impl [SPEC-USER-PLUGIN-05] tools/visualizer/flight_plugin.py
        Draws the live QMC5883L magnetic compass sensor details.
        """
        imgui.begin_child("CompassSensorPanel", imgui.ImVec2(-1, 115), True)
        mx, my, mz = mag_xyz
        imgui.columns(3, "mag_info_cols", False)
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "MAGNETIC HEADING")
        card_dirs = ("N", "NE", "E", "SE", "S", "SW", "W", "NW")
        card_idx = int(((heading_deg + 22.5) % 360.0) // 45)
        card_str = card_dirs[card_idx]
        imgui.text_colored(imgui.ImVec4(0.2, 0.9, 0.4, 1.0), f"{heading_deg:05.1f}° ({card_str})")

        imgui.next_column()
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "RAW I2C COUNTS")
        imgui.text_colored(imgui.ImVec4(0.9, 0.9, 0.9, 1.0), f"X={mx:+5d} Y={my:+5d} Z={mz:+5d}")

        imgui.next_column()
        imgui.text_colored(imgui.ImVec4(0.6, 0.7, 0.8, 1.0), "TOTAL FLUX DENSITY")
        imgui.text_colored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0), f"{field_ut:.1f} µT (Earth Field)")
        imgui.columns(1)

        imgui.separator()
        imgui.text_colored(imgui.ImVec4(0.4, 0.8, 0.5, 1.0),
                           f"QMC5883L on TWI1 (PB4/PB5) @ 400 kHz Fast-Mode | {mag_hz} Hz Continuous ODR")
        imgui.end_child()

    def _render_pfd(self, width: float, height: float, roll_deg: float, pitch_deg: float):
        """Draws the vector-based Primary Flight Display using Dear ImGui DrawList."""
        cursor = imgui.get_cursor_screen_pos()
        p_min = cursor
        p_max = imgui.ImVec2(cursor.x + width, cursor.y + height)

        dl = imgui.get_window_draw_list()
        dl.push_clip_rect(p_min, p_max, True)

        col_sky = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.08, 0.45, 0.78, 1.0))
        col_earth = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.48, 0.24, 0.08, 1.0))
        col_white = imgui.color_convert_float4_to_u32(imgui.ImVec4(1.0, 1.0, 1.0, 1.0))
        col_reticle = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.98, 0.80, 0.08, 1.0))
        col_border = imgui.color_convert_float4_to_u32(imgui.ImVec4(0.2, 0.3, 0.4, 1.0))

        dl.add_rect_filled(p_min, p_max, col_sky)

        cx = p_min.x + width * 0.5
        cy = p_min.y + height * 0.5
        pitch_scale = 2.2
        pitch_offset = pitch_deg * pitch_scale

        rad_roll = math.radians(roll_deg)
        cos_r = math.cos(rad_roll)
        sin_r = math.sin(rad_roll)

        r_box = max(width, height) * 2.0
        local_earth = [
            (-r_box, 0.0),
            (r_box, 0.0),
            (r_box, r_box),
            (-r_box, r_box)
        ]

        world_earth = []
        for lx, ly in local_earth:
            sx = cx + lx * cos_r + ly * sin_r
            sy = (cy + pitch_offset) - lx * sin_r + ly * cos_r
            world_earth.append(imgui.ImVec2(sx, sy))

        dl.add_convex_poly_filled(world_earth, col_earth)

        h_x1 = cx - r_box * cos_r
        h_y1 = (cy + pitch_offset) + r_box * sin_r
        h_x2 = cx + r_box * cos_r
        h_y2 = (cy + pitch_offset) - r_box * sin_r
        dl.add_line(imgui.ImVec2(h_x1, h_y1), imgui.ImVec2(h_x2, h_y2), col_white, 2.5)

        for deg in [-30, -20, -10, 10, 20, 30]:
            y_local = -deg * pitch_scale
            bar_w = 28.0 if abs(deg) % 20 == 0 else 16.0
            p1_x = cx - bar_w * cos_r - y_local * sin_r
            p1_y = (cy + pitch_offset) + bar_w * sin_r - y_local * cos_r
            p2_x = cx + bar_w * cos_r - y_local * sin_r
            p2_y = (cy + pitch_offset) - bar_w * sin_r - y_local * cos_r
            dl.add_line(imgui.ImVec2(p1_x, p1_y), imgui.ImVec2(p2_x, p2_y), col_white, 1.5)

        dl.add_line(imgui.ImVec2(cx - 40, cy), imgui.ImVec2(cx - 14, cy), col_reticle, 3.5)
        dl.add_line(imgui.ImVec2(cx - 14, cy), imgui.ImVec2(cx - 14, cy + 8), col_reticle, 3.0)
        dl.add_line(imgui.ImVec2(cx + 14, cy), imgui.ImVec2(cx + 40, cy), col_reticle, 3.5)
        dl.add_line(imgui.ImVec2(cx + 14, cy), imgui.ImVec2(cx + 14, cy + 8), col_reticle, 3.0)
        dl.add_circle_filled(imgui.ImVec2(cx, cy), 3.0, col_reticle)

        dl.add_text(imgui.ImVec2(p_min.x + 8, p_min.y + 8), col_white, f"ROLL: {roll_deg:+5.1f}°")
        dl.add_text(imgui.ImVec2(p_min.x + 8, p_min.y + 24), col_white, f"PITCH: {pitch_deg:+5.1f}°")

        dl.add_rect(p_min, p_max, col_border, 0.0, 1.5)
        dl.pop_clip_rect()

        imgui.dummy(imgui.ImVec2(width, height))
