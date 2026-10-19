#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
test_e906_sensors_board.py - Live Hardware Sensor Verification on XuanTie E906

@impl [SPEC-E906-REPL-06] [SPEC-GPS-01] [SPEC-E906-REPL-12] apps/e906_repl_app/SPECIFICATION.md

Connects to the Radxa Cubie A5E board (192.168.3.3 or local) over SSH/RPMsg,
executes flight_sensors.py on the XuanTie E906 RISC-V co-processor, asserts
sensor ingestion criteria, and records the test results into the SpecTrace evidence ledger.
"""

import sys
import os
import time
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from trac_lib import ledger

BOARD_IP = os.environ.get("BOARD_IP", "192.168.3.3")

def run_remote_sensors_test():
    start_time = time.time()
    # 1. Run flight_sensors.py on the board
    cmd = [
        "ssh", "-o", "ConnectTimeout=5", f"root@{BOARD_IP}",
        "python3 /root/run_e906_script.py /root/flight_sensors.py"
    ]
    res = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    duration_s = time.time() - start_time
    output = res.stdout

    # 2. Parse and assert live criteria
    has_compass = "Compass: 359 deg" in output or "Compass:" in output
    has_gps = "GPS (115200 baud @ 10Hz):" in output
    has_3d_fix = "3D SPS Fix" in output or "DGPS Fix" in output or "3D Fix" in output
    has_completion = "Execution Complete:" in output

    passed = (res.returncode == 0) and has_compass and has_gps and has_3d_fix

    metrics = {
        "board": "radxa_cubie_a5e",
        "coprocessor": "xuantie_e906",
        "i2c_compass_detected": has_compass,
        "gps_serial_115200_active": has_gps,
        "gps_3d_fix_acquired": has_3d_fix,
        "duration_s": round(duration_s, 2),
    }

    print(f"=== E906 Live Board Sensor Test Result: {'PASS' if passed else 'FAIL'} ===")
    print(f"  Duration: {duration_s:.2f}s")
    print(f"  Compass active: {has_compass}")
    print(f"  GPS 115200 baud active: {has_gps}")
    print(f"  GPS 3D Fix acquired: {has_3d_fix}")

    # 3. Record evidence in SpecTrace ledger
    record = ledger.make_record(
        root=ROOT,
        family="test",
        check="board:e906:sensors",
        result="pass" if passed else "fail",
        metrics=metrics,
        requirements=["SPEC-E906-REPL-06", "SPEC-GPS-01", "SPEC-E906-REPL-12"],
        duration_s=duration_s,
        tool="run_e906_script.py",
        notes="Live silicon verification of I2C1 compass and UART3 GPS at 115200 baud"
    )
    path = ledger.append(ROOT, record)
    print(f"Recorded evidence in {path}")
    return 0 if passed else 1

if __name__ == "__main__":
    sys.exit(run_remote_sensors_test())
