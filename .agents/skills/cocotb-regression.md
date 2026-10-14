---
name: cocotb-regression
verifies: RTL behaviour per module and end to end (TLP over Dual-SPI into `asp_top`).
---

# Skill: cocotb-regression

**What it verifies**: RTL behaviour per module and end to end (TLP over Dual-SPI into `asp_top`).

## How to run

```bash
source ~/.tools/oss-cad-suite/environment
cd sim/cocotb && make TOPLEVEL=<module> MODULE=<test>   # pairs listed in sim/CMakeLists.txt
```

## Pass criteria

Every test prints `PASS`; a run with no `TESTS=` line is a failure (it did not build).

## Evidence

`results.xml` and the summary line.
