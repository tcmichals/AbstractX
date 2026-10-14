---
name: fpga-build
verifies: A board bitstream is produced from the shared RTL with the open-source Gowin flow.
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
