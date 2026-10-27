---
name: rtl-lint
runs_on: rtl-sim
verifies: "RTL is free of multi-driven signals, width errors and incomplete cases that simulation hides."
description: "Check skill. RTL is free of multi-driven signals, width errors and incomplete cases that simulation hides. Run when a requirement names this skill in its Verified by line or a change touches what it checks."
---

# Skill: rtl-lint

**What it verifies**: RTL is free of multi-driven signals, width errors and incomplete cases that simulation hides.

## How to run

```bash
source ~/.tools/oss-cad-suite/environment
INCS="-Irtl -Irtl/imu -Irtl/motor -Irtl/led -Irtl/spi -Irtl/pwm -Irtl/esc -Irtl/serial -Irtl/pio -Ihw/zynq7000/qmtech_zynq7020/rtl"
for f in rtl/*.sv rtl/*/*.sv hw/**/rtl/*.sv; do [ -f "$f" ] && verilator --lint-only -Wall -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL \
  -Wno-UNUSEDPARAM -Wno-PINCONNECTEMPTY -Wno-WIDTHTRUNC -Wno-WIDTHEXPAND $INCS $f --top-module $(basename $f .sv); done
```

## Pass criteria

No `MULTIDRIVEN`, `SYNCASYNCNET` or `CASEINCOMPLETE`; width warnings reviewed.

## Evidence

Lint output in the commit message.
