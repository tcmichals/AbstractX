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
    """
    # @impl [SPEC-STUDIO-01] tools/visualizer/SPECIFICATION.md
    Launches AbstractX Visualizer Studio configured with Level 2 Flight Instruments.
    """
    new_args = [sys.argv[0]]
    has_tab = False
    for a in sys.argv[1:]:
        if a == "--studio":
            new_args.extend(["--tab", "platform"])
            has_tab = True
        elif a.startswith("--tab"):
            has_tab = True
            new_args.append(a)
        else:
            new_args.append(a)
    if not has_tab:
        new_args.extend(["--tab", "flight"])
    sys.argv = new_args
    abstractx_studio.main()

if __name__ == "__main__":
    main()
