---
name: interface-compliance
runs_on: host
verifies: "Hardware peripheral drivers and Studio visualizer plugins inherit from standardized polymorphic base classes (IImuDriver, IMagDriver, IGpsDriver, AbstractXStudioPlugin) with asynchronous coroutine lifecycles ([SPEC-ARCH-01])."
description: "Check skill. Verifies driver and target interface compliance: ensures all sensor drivers inherit from domain abstract interfaces (IImuDriver, IMagDriver, IGpsDriver), expose async coroutine methods (init_async), and visualizer plugins inherit from AbstractXStudioPlugin. Run when touching drivers, targets, or visualizer plugins."
---

# Skill: interface-compliance

**What it verifies**: Hardware peripheral drivers and Studio visualizer plugins inherit from standardized polymorphic base classes (`IImuDriver`, `IMagDriver`, `IGpsDriver`, `AbstractXStudioPlugin`) with asynchronous coroutine lifecycles (`init_async()`, `next_sample_async()`).

## How to run

```bash
python3 tools/audit_interfaces.py
```

## Passing criteria

* Output displays `[PASS] All N components strictly adhere to standard polymorphic interfaces!`.
* Exit code is `0`.
