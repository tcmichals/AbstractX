---
name: hw-sw-register-parity
runs_on: host
verifies: "Strict 1:1 register contract and address parity between C++ target HAL headers, SystemVerilog RTL modules, Python diagnostic tools, and architecture specifications ([SPEC-ZYNQ-01], [SPEC-ZYNQ-02], [SPEC-ZYNQ-03], [SPEC-ZYNQ-06])."
description: "Check skill. Enforces exact 1:1 match of register offsets, bit masks, and hardware IDs across C++ drivers, RTL CSR decoders, Python utilities, and Markdown specifications. Run when touching target HAL headers, RTL CSR blocks, Python target diagnostics, or target architecture specs."
---

# Skill: hw-sw-register-parity

**What it verifies**: 
- Zynq-7000 AXI-Lite Bridge: C++ `ZynqUioBridge` (`targets/zynq7000/include/abstractx/target/zynq_uio.hpp`) matches RTL `asp_axi_lite_bridge.sv`, Python `zynq_diagnostics.py`, and `SPECIFICATION.md` CSR map (`REG_CONTROL=0x00`, `REG_STATUS=0x04`, `REG_IRQ_STATUS=0x08`, `REG_IRQ_ENABLE=0x0C`, `REG_TLP_IN_PORT=0x10`, `REG_TLP_OUT_PORT=0x14`, `REG_EGR_COUNT=0x18`, `REG_ING_FREE=0x1C`, `REG_HARDWARE_ID=0x40`, `HARDWARE_MAGIC=0x41535036`).
- System registers: Python diagnostics (`zynq_diagnostics.py`) match the case labels of the AXI4-Lite slave `asp_sys_regs.sv` (offsets parsed from the RTL, not hard-coded).
- XuanTie E906 Memory Map: Invariants in `memory_map.h` match physical silicon truths (DSP local RAM `0x00020000`, OP-TEE A2 `0x00044000`, SRAM Space 0 `0x3FFC0000`, Space 1 `0x40000000`, shared tail `0x3FFF0000`, RPMsg pool `0x40020000`).

## How to run

```bash
python3 tools/check_register_parity.py
```

## Pass criteria

`[SUCCESS] 100% Hardware-Software Register Parity Verified across all layers.` with exit code 0.

## Evidence

Output of `tools/check_register_parity.py`.
