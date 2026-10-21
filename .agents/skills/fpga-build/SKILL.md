---
name: fpga-build
runs_on: fpga-build
verifies: "A board bitstream is produced from the shared RTL with the open-source Gowin flow."
description: "Check skill. A board bitstream is produced from the shared RTL with the open-source Gowin flow. Run when a requirement names this skill in its Verified by line or a change touches what it checks."
---

# Skill: fpga-build

**What it verifies**: A board bitstream is produced from the shared RTL with the open-source Gowin flow.

## How to run

```bash
make -f hw/Makefile <board>     # tang9k, primer20k, (tang_nano20k)
```

## Pass criteria

`build_hw/<board>.fs` exists; nextpnr reports no unplaced cells.

## Evidence

nextpnr log; bitstream hash.
