---
name: rtl-synth-check
verifies: Synthesis-level structural problems simulation cannot see, e.g. two always blocks driving one register.
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
