---
name: fpga-fit
verifies: The design fits the FPGA with headroom and meets timing ([SPEC-TN20K-03]).
---

# Skill: fpga-fit

**What it verifies**: The design fits the FPGA with headroom and meets timing ([SPEC-TN20K-03]).

## How to run

```bash
make -f hw/Makefile <board>     # read the nextpnr utilisation and Fmax report
```

## Pass criteria

LUT and FF use ≤ 80%; Fmax above the system clock for every clock domain.

## Evidence

Utilisation and Fmax lines from the nextpnr log, recorded in the engineering log.
