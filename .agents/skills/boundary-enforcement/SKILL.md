---
name: boundary-enforcement
runs_on: host
verifies: "AbstractX Core Framework (include/abstractx/, src/, targets/, rtl/) contains strictly framework primitives and pure abstract HAL interfaces, while all concrete off-chip peripheral drivers (C++ and MicroPython) reside in the Application Library (applib/) ([SPEC-ARCH-01], [SPEC-ARCH-12])."
description: "Check skill. Verifies architectural layer boundaries: strictly prohibits concrete peripheral chip drivers (ICM42688P, QMC5883L, Ublox, BMP388) from existing inside include/abstractx/ or targets/. All concrete chip drivers must reside in applib/. Run when adding, moving, or refactoring drivers or framework headers."
---

# Skill: boundary-enforcement

**What it verifies**: The core framework (`include/abstractx/`, `src/`, `targets/`, `rtl/`) is strictly separated from application-level peripheral drivers. AbstractX delivers the coroutine runtime, reactor, dispatcher, memory pools, TLP crossbar fabric, and pure abstract interfaces (`ISpi`, `II2c`, `IUart`, `IImuDriver`, `IIoProcessor`). All concrete peripheral drivers (C++ and MicroPython) live in the Application Library (`applib/`).

## How to run

```bash
python3 tools/audit_boundaries.py
```

## Passing criteria

* Output displays `[PASS] Core framework is 100% pure! Zero concrete peripheral drivers in core.`.
* Exit code is `0`.
