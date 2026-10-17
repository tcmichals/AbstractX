---
name: e906-disassembly-check
verifies: Bare-metal control flow that source review cannot prove: trap classification ([SPEC-E906-CRASH-01]), `mhcr` cache setting ([SPEC-E906-CACHE-02]), idle-wait masking.
---

# Skill: e906-disassembly-check

**What it verifies**: Bare-metal control flow that source review cannot prove: trap classification ([SPEC-E906-CRASH-01]), `mhcr` cache setting ([SPEC-E906-CACHE-02]), idle-wait masking.

## How to run

```bash
riscv-none-elf-objdump -d build_e906/apps/gps_imu_app/platforms/allwinner_e906/e906_coprocessor | less
# default_trap_entry:
#   stack alloc:   addi sp, sp, -416
#   fpu save:      fsd f0, 144(sp) ... fsd f31, 392(sp)
#   branch:        lw t0, 128(sp); bltz t0
#   fpu restore:   fld f0, 144(sp) ... fld f31, 392(sp)
#   stack free:    addi sp, sp, 416
# _start: csrw 0x7c1 with 0x1031
```

## Pass criteria

The instructions match the requirement text. Verify from the binary, never from comments.

## Evidence

Quoted instructions in the commit message.
