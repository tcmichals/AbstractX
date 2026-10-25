#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-ARCH-01] docs/DESIGN_SPECIFICATION.md#spec-arch-01

"""
AbstractX Driver & Target Interface Compliance Auditor
------------------------------------------------------
Verifies that all hardware peripheral drivers, target HAL abstractions,
and visualizer plugins adhere to standardized polymorphic interfaces:
- IMU drivers under drivers/.../imu/ MUST inherit from IImuDriver
- Magnetometer drivers under drivers/.../mag/ MUST inherit from IMagDriver
- GPS drivers under drivers/.../gps/ MUST inherit from IGpsDriver
- Drivers MUST expose asynchronous coroutine lifecycle (init_async)
- Studio plugins MUST inherit from AbstractXStudioPlugin
"""

import sys
import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

def audit_cpp_drivers():
    drivers_dir = REPO_ROOT / "applib" / "include" / "applib" / "drivers"
    if not drivers_dir.exists():
        print(f"Error: {drivers_dir} does not exist.")
        return 0, [f"Missing applib drivers directory: {drivers_dir}"]

    errors = []
    audited = 0

    domain_rules = {
        "imu": ("IImuDriver", ["init_async", "next_sample_async"]),
        "mag": ("IMagDriver", ["init_async", "read_sample_async"]),
        "gps": ("IGpsDriver", ["init_async", "next_fix_async"]),
    }

    for domain, (required_base, required_methods) in domain_rules.items():
        sub_dir = drivers_dir / domain
        if not sub_dir.exists():
            continue
        for header in sub_dir.glob("*.hpp"):
            if header.name in ("sensor_interface.hpp", "sensor_types.hpp"):
                continue

            content = header.read_text(encoding="utf-8")
            audited += 1

            # Match class definitions
            classes = re.findall(r"class\s+([A-Za-z0-9_]+)\s*:\s*(public\s+[^{]+)\{", content)
            if not classes:
                # Check if it has a class without inheritance
                bare_classes = re.findall(r"class\s+([A-Za-z0-9_]+)\s*\{", content)
                if bare_classes:
                    errors.append(f"{header.relative_to(REPO_ROOT)}: Class '{bare_classes[0]}' does NOT inherit from {required_base}")
                continue

            for class_name, inheritance in classes:
                if required_base not in inheritance:
                    errors.append(f"{header.relative_to(REPO_ROOT)}: Class '{class_name}' inherits from '{inheritance.strip()}' but must inherit from {required_base}")

            # Check required async methods
            for method in required_methods:
                if method not in content:
                    errors.append(f"{header.relative_to(REPO_ROOT)}: Missing required asynchronous coroutine method '{method}()'")

    return audited, errors

def audit_python_plugins():
    plugins_dir = REPO_ROOT / "tools" / "visualizer"
    if not plugins_dir.exists():
        return 0, []

    errors = []
    audited = 0

    for py_file in plugins_dir.glob("*_plugin.py"):
        content = py_file.read_text(encoding="utf-8")
        audited += 1
        classes = re.findall(r"class\s+([A-Za-z0-9_]+)\(([^)]+)\):", content)
        for class_name, inheritance in classes:
            if "AbstractXStudioPlugin" not in inheritance:
                errors.append(f"{py_file.relative_to(REPO_ROOT)}: Plugin '{class_name}' must inherit from AbstractXStudioPlugin")

    return audited, errors

def main():
    print("=" * 70)
    print("AbstractX Driver & Target Interface Compliance Auditor")
    print("=" * 70)

    cpp_count, cpp_errors = audit_cpp_drivers()
    py_count, py_errors = audit_python_plugins()

    total_audited = cpp_count + py_count
    all_errors = cpp_errors + py_errors

    print(f"[*] Audited {cpp_count} C++ hardware driver(s) across IMU, Magnetometer, and GPS.")
    print(f"[*] Audited {py_count} Python Studio plugin(s).")

    if all_errors:
        print("\n[FAIL] Interface compliance violations detected:")
        for err in all_errors:
            print(f"  - {err}")
        sys.exit(1)
    else:
        print(f"\n[PASS] All {total_audited} components strictly adhere to standard polymorphic interfaces!")
        sys.exit(0)

if __name__ == "__main__":
    main()
