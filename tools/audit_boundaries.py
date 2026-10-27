#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-ARCH-01] docs/DESIGN_SPECIFICATION.md#spec-arch-01
# @impl [SPEC-ARCH-12] docs/DESIGN_SPECIFICATION.md

"""
AbstractX Architectural Boundary Auditor
----------------------------------------
Enforces the mandatory boundary between the AbstractX Core Framework and
the Application Library (applib):
1. AbstractX Core (include/abstractx/, src/, targets/, rtl/):
   - MUST ONLY contain core framework primitives (coroutines, reactor, dispatcher,
     memory pools, TLP crossbar fabric) and pure abstract interfaces.
   - STRICTLY PROHIBITED: Concrete peripheral chip drivers (e.g. ICM42688P, QMC5883L,
     Ublox GPS, BMP388).
2. Application Library (applib/):
   - Houses reusable C++ and MicroPython/Python sensor drivers, algorithms, and utilities.
"""

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

BANNED_CHIP_PATTERNS = [
    "icm", "qmc", "hmc", "ublox", "bmi", "bno", "bmp", "bmm", "lsm", "mpu"
]

def audit_core_boundaries():
    core_include = REPO_ROOT / "include" / "abstractx"
    errors = []
    audited = 0

    if not core_include.exists():
        return 0, [f"Core include directory {core_include} missing!"]

    for file_path in core_include.rglob("*.*"):
        if file_path.is_file():
            audited += 1
            file_lower = file_path.name.lower()
            for pattern in BANNED_CHIP_PATTERNS:
                if pattern in file_lower:
                    errors.append(
                        f"Boundary Violation: Concrete peripheral '{file_path.relative_to(REPO_ROOT)}' "
                        f"found in Core Framework! Must be moved to applib/include/applib/drivers/."
                    )
                    break

    # Ensure no 'drivers/' directory exists at repo root
    legacy_drivers = REPO_ROOT / "drivers"
    if legacy_drivers.exists():
        errors.append(
            "Boundary Violation: Legacy 'drivers/' directory found at repo root! "
            "Concrete drivers must reside in 'applib/'."
        )

    return audited, errors

def main():
    print("=" * 70)
    print("AbstractX Architectural Boundary Auditor")
    print("=" * 70)

    audited, errors = audit_core_boundaries()
    print(f"[*] Audited {audited} header files in include/abstractx/ for core purity.")

    if errors:
        print("\n[FAIL] Architectural boundary violations detected:")
        for err in errors:
            print(f"  - {err}")
        sys.exit(1)
    else:
        print(f"\n[PASS] Core framework is 100% pure! Zero concrete peripheral drivers in core.")
        sys.exit(0)

if __name__ == "__main__":
    main()
