---
name: rtl-synth-check
runs_on: fpga-build
verifies: "Synthesis-level structural problems simulation cannot see, e.g. two always blocks driving one register."
description: "Check skill. Synthesis-level structural problems simulation cannot see, e.g. two always blocks driving one register. Run when a requirement names this skill in its Verified by line or a change touches what it checks."
---

# Skill: rtl-synth-check

**What it verifies**: Synthesis-level structural problems simulation cannot see, e.g. two always blocks driving one register.

## How to run

```bash
yosys -p "read_verilog -sv rtl/<file>.sv; hierarchy -top <module>; proc; check -assert" 
```

## Pass criteria

No `multiple conflicting drivers`; `check -assert` exits 0.

## Evidence

Yosys output in the commit message.
