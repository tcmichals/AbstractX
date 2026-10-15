---
name: cocotb-regression
verifies: RTL behaviour per module and end to end (TLP over Dual-SPI into `asp_top`).
---

# Skill: cocotb-regression

**What it verifies**: RTL behaviour per module and end to end (TLP over Dual-SPI into `asp_top`).

## How to run

```bash
source ~/.tools/oss-cad-suite/environment
python3 tools/run_cocotb.py              # every add_cocotb_test target in sim/CMakeLists.txt
python3 tools/run_cocotb.py --only dshot # a subset
```

Runs in a temporary copy (no `sim_build/` in the repo). Waveforms: `--trace`
(needs lz4 headers).

## Pass criteria

Every target prints `PASS`; the tool exits non-zero if any target fails or prints no `TESTS=` line (it did not build or did not run). A new test must be registered in `sim/CMakeLists.txt` or it is never run.

## Evidence

`results.xml` and the summary line.
