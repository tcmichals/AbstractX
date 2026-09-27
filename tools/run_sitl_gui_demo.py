#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
AbstractX SITL & Cocotb Telemetry Bridge to GUI Visualizer
==========================================================
Bridges SITL simulation (C++ CppUTest or Cocotb Verilator RTL) to
the live Aviation Primary Flight Display (flight_display.py) over UDP port 9870.

Demonstrates that Software C++20 SITL and FPGA RTL Verilator Auto-DMA are
symmetrical mirrors of each other: both emit identical 64-byte TLPs.

Usage:
  python3 tools/run_sitl_gui_demo.py --mode cpputest    # Run CppUTest SITL unit tests
  python3 tools/run_sitl_gui_demo.py --mode cocotb      # Run Cocotb Verilator RTL testbench
  python3 tools/run_sitl_gui_demo.py --mode cocotb-live # Stream FPGA RTL Auto-DMA to GUI
  python3 tools/run_sitl_gui_demo.py --mode sitl-live   # Stream C++ SITL telemetry to GUI
  python3 tools/run_sitl_gui_demo.py --mode display     # Launch GUI flight visualizer
"""

import os
import sys
import time
import socket
import argparse
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TABBYPY = Path("/home/tcmichals/.tools/oss-cad-suite/bin/tabbypy3")
FLIGHT_DISPLAY = REPO_ROOT / "apps" / "gps_imu_app" / "tools" / "flight_display.py"
BUILD_HOST = REPO_ROOT / "build_host"

def run_cpputest():
    print("\n=======================================================")
    print("  Running AbstractX SITL CppUTest Suites (Host)")
    print("=======================================================\n")
    tests = [
        BUILD_HOST / "tests" / "test_sitl_imu",
        BUILD_HOST / "tests" / "test_sitl_gps",
        BUILD_HOST / "tests" / "test_sitl_fusion"
    ]
    all_ok = True
    for t in tests:
        if not t.exists():
            print(f"[ERROR] Test binary not found: {t}. Run cmake --build build_host first.")
            return 1
        print(f"--- Running {t.name} ---")
        ret = subprocess.run([str(t), "-v"])
        if ret.returncode != 0:
            all_ok = False
    return 0 if all_ok else 1

def run_cocotb():
    print("\n=======================================================")
    print("  Running Cocotb Verilator RTL Co-Simulation Testbench")
    print("=======================================================\n")
    sim_dir = REPO_ROOT / "sim" / "cocotb"
    env = os.environ.copy()
    env["PATH"] = f"/home/tcmichals/.tools/oss-cad-suite/bin:{env.get('PATH', '')}"
    cmd = [
        "make", "-C", str(sim_dir),
        "SIM=verilator",
        "TOPLEVEL=asp_top",
        "MODULE=test_asp_tlp_64b_cocotb"
    ]
    res = subprocess.run(cmd, env=env)
    return res.returncode

def run_cocotb_live():
    print("\n=======================================================")
    print("  Streaming FPGA Verilator RTL Auto-DMA to GUI (:9870) ")
    print("=======================================================\n")
    print("Streaming FPGA RTL TLPs with live doorbell trace to 127.0.0.1:9870.")
    print("In another terminal, run: python3 tools/run_sitl_gui_demo.py --mode display\n")
    sim_dir = REPO_ROOT / "sim" / "cocotb"
    env = os.environ.copy()
    env["PATH"] = f"/home/tcmichals/.tools/oss-cad-suite/bin:{env.get('PATH', '')}"
    env["STREAM_TO_GUI"] = "1"
    env["STREAM_FRAMES"] = "1000"
    cmd = [
        "make", "-C", str(sim_dir),
        "SIM=verilator",
        "TOPLEVEL=asp_top",
        "MODULE=test_asp_tlp_64b_cocotb",
        "COCOTB_TESTCASE=test_fpga_live_gui_stream"
    ]
    res = subprocess.run(cmd, env=env)
    return res.returncode

def launch_gui():
    py = str(TABBYPY) if TABBYPY.exists() else sys.executable
    print(f"\n[Display] Launching Flight Display GUI ({py})...")
    subprocess.run([py, str(FLIGHT_DISPLAY), "--port", "9870"])

def run_sitl_live():
    app = BUILD_HOST / "apps" / "gps_imu_app" / "gps_imu_app"
    if not app.exists():
        print(f"[ERROR] Binary {app} not found. Build with: cmake --build build_host --target gps_imu_app")
        return 1
    print("\n=======================================================")
    print("  Starting AbstractX C++20 SITL Node (Streaming to :9870)")
    print("=======================================================\n")
    print("Streaming 8 kHz IMU, 10 Hz GPS, and AHRS state to 127.0.0.1:9870.")
    print("In another terminal, run: python3 tools/run_sitl_gui_demo.py --mode display\n")
    subprocess.run([str(app)])

def main():
    parser = argparse.ArgumentParser(description="AbstractX SITL / Cocotb & GUI Bridge")
    parser.add_argument("--mode", choices=["cpputest", "cocotb", "cocotb-live", "sitl-live", "display"],
                        default="cpputest", help="Operation mode")
    args = parser.parse_args()

    if args.mode == "cpputest":
        sys.exit(run_cpputest())
    elif args.mode == "cocotb":
        sys.exit(run_cocotb())
    elif args.mode == "cocotb-live":
        sys.exit(run_cocotb_live())
    elif args.mode == "sitl-live":
        sys.exit(run_sitl_live())
    elif args.mode == "display":
        launch_gui()

if __name__ == "__main__":
    main()
