---
name: e906-disassembly-check
runs_on: host
verifies: "Bare-metal control flow that source review cannot prove: trap classification ([SPEC-E906-CRASH-01]), `mhcr` cache setting ([SPEC-E906-CACHE-02]), idle-wait masking."
description: "Check skill. Bare-metal control flow that source review cannot prove: trap classification ([SPEC-E906-CRASH-01]), `mhcr` cache setting ([SPEC-E906-CACHE-02]), idle-wait masking. Run when a requirement names this skill in its Verified by line or a change touches what it checks."
---

# Skill: e906-disassembly-check

**What it verifies**: Bare-metal control flow that source review cannot prove: trap classification ([SPEC-E906-CRASH-01]), `mhcr` cache setting ([SPEC-E906-CACHE-02]), idle-wait masking.

## How to run

```bash
riscv-none-elf-objdump -d build_e906/apps/gps_imu_app/platforms/allwinner_e906/e906_coprocessor | less
# default_trap_entry:
#   stack alloc:   addi sp, sp, -416
#   fpu save:      fsw f0, 144(sp) ... fsw f31, 392(sp)   (single precision: the E906 has no D)
#   branch:        lw t0, 128(sp); bltz t0
#   idle skip:     la t1, e906_idle_wfi; bne t0, t1; addi t0, t0, 4   ([SPEC-E906-IRQ-01])
#   fpu restore:   flw f0, 144(sp) ... flw f31, 392(sp)
#   stack free:    addi sp, sp, 416
# _start: csrw 0x7c1 with 0x1031; mtvec written as (default_trap_entry | 3), base 64-byte aligned

# No double-precision instruction anywhere (they trap on this core, [SPEC-E906-ARCH-01]); must print 0:
riscv-none-elf-objdump -d <elf> | grep -cE '\s(fld|fsd|c\.fldsp|c\.fsdsp|f[a-z]+\.d|fcvt\.[a-z]+\.d|fcvt\.d\.[a-z]+)\s'
```

## Pass criteria

The instructions match the requirement text. Verify from the binary, never from comments.

## Evidence

Quoted instructions in the commit message.
