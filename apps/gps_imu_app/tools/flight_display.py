#!/usr/bin/env python3
"""
Copyright (C) 2026 Tim Michals
SPDX-License-Identifier: GPL-3.0-or-later

AbstractX Flight Display & 3D Quadcopter Visualizer (flight_display.py)
----------------------------------------------------------------------
Aviation Primary Flight Display (PFD) and 3D quadcopter attitude visualizer
for gps_imu_app, powered by Dear ImGui Bundle (imgui-bundle).

Part of the AbstractX Two-Level GUI Architecture:
- Level 1: Standard AbstractX Studio (Platform Topology, Coroutines, Memory)
- Level 2: Domain Extensible Flight Instruments (PFD, 3D Attitude, Mixer)

Usage:
  python3 apps/gps_imu_app/tools/flight_display.py [--port 9870] [--sim] [--studio]
"""

import sys
import argparse
from pathlib import Path

# Add visualizer tools directory to Python path
REPO_ROOT = Path(__file__).resolve().parents[3]
VISUALIZER_DIR = REPO_ROOT / "tools" / "visualizer"
sys.path.insert(0, str(VISUALIZER_DIR))

try:
    import abstractx_studio
except ImportError as e:
    print(f"Error loading visualizer studio: {e}")
    print("Ensure imgui-bundle is installed: pip install imgui-bundle numpy")
    sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="AbstractX Flight Display (imgui-bundle)")
    parser.add_argument("--port", type=int, default=abstractx_studio.DEFAULT_UDP_PORT,
                        help="UDP telemetry port (default: 9870)")
    parser.add_argument("--sim", action="store_true",
                        help="Run in synthetic simulation mode without external target")
    parser.add_argument("--studio", action="store_true",
                        help="Start on Level 1 Platform Studio tab instead of Level 2 Flight Display")
    args = parser.parse_args()

    # Pass configuration to studio
    abstractx_studio.g_initial_tab = "platform" if args.studio else "flight"

    # Start UDP receiver background thread
    recv_thread = abstractx_studio.threading.Thread(
        target=abstractx_studio.udp_receiver_thread,
        args=(args.port, args.sim),
        daemon=True
    )
    recv_thread.start()

    # Launch Dear ImGui application
    runner_params = abstractx_studio.hello_imgui.RunnerParams()
    runner_params.app_window_params.window_title = "AbstractX Flight Display & Attitude Visualizer (imgui-bundle)"
    runner_params.app_window_params.window_geometry.size = (1180, 780)
    runner_params.app_window_params.resizable = True
    runner_params.app_window_params.restore_previous_geometry = True
    runner_params.callbacks.show_gui = abstractx_studio.render_gui
    abstractx_studio.implot.create_context()

    print("[Flight Display] Launching high-performance Dear ImGui Bundle visualizer...")
    abstractx_studio.immapp.run(runner_params)
    abstractx_studio.implot.destroy_context()

if __name__ == "__main__":
    main()
