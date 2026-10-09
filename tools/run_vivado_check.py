#!/usr/bin/env python3
"""
tools/run_vivado_check.py
-------------------------
Runs AMD Vivado 2025.2 static analysis, elaboration, and out-of-context synthesis
on AbstractX SystemVerilog modules to verify zero-latch inference, IEEE 1800 compliance,
and LUT utilization on the target silicon (Xilinx Zynq-7020 XC7Z020-CLG400-1).
"""

import sys
import os
import subprocess
from pathlib import Path

VIVADO_SETTINGS = Path("/home/tcmichals/tools/Xilinx/2025.2/Vivado/settings64.sh")
DEFAULT_PART = "xc7z020clg400-1"

def run_vivado_check(module_name="asp_router", sv_file="rtl/asp_router.sv", part=DEFAULT_PART):
    root_dir = Path(__file__).resolve().parent.parent
    sv_path = root_dir / sv_file
    
    if not VIVADO_SETTINGS.exists():
        print(f"[ERROR] Vivado settings script not found at {VIVADO_SETTINGS}")
        return 1

    if not sv_path.exists():
        print(f"[ERROR] SystemVerilog file not found: {sv_path}")
        return 1

    tcl_script = f"""
set_param general.maxThreads 8
read_verilog -sv "{sv_path}"
synth_design -top {module_name} -part {part} -mode out_of_context
report_utilization -no_primitives
exit
"""
    tcl_file = root_dir / f"build_vivado_{module_name}.tcl"
    tcl_file.write_text(tcl_script)

    print(f"==============================================================================")
    print(f" AMD Vivado 2025.2 Synthesis & Lint Gate: {module_name}")
    print(f" Target Silicon: {part} (QMTECH Zynq-7020)")
    print(f" Source:         {sv_file}")
    print(f"==============================================================================")

    cmd = f"source {VIVADO_SETTINGS} && vivado -mode batch -nolog -nojournal -source {tcl_file}"
    try:
        proc = subprocess.run(["bash", "-c", cmd], cwd=root_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        # Filter and print relevant summary lines
        lines = proc.stdout.splitlines()
        in_util = False
        util_lines = []
        latch_warning = False
        
        for line in lines:
            if "CRITICAL WARNING:" in line or "ERROR:" in line:
                print(f"  {line}")
            if "Register as Latch" in line:
                parts = [p.strip() for p in line.split("|") if p.strip()]
                if len(parts) >= 2 and parts[1] != "0":
                    latch_warning = True
                    print(f"  [WARNING] Unintended Latch inferred: {line}")
            if "1. Slice Logic" in line or "Utilization Design Information" in line:
                in_util = True
            if in_util:
                util_lines.append(line)
            if in_util and "2. Memory" in line:
                in_util = False

        if util_lines:
            print("\n" + "\n".join(util_lines[:25]))

        if proc.returncode == 0 and not latch_warning:
            print(f"\n[SUCCESS] {module_name} synthesized cleanly on {part} (0 latches, valid SV).")
            return 0
        else:
            print(f"\n[FAILURE] Synthesis or latch check failed (exit code {proc.returncode}).")
            return proc.returncode if proc.returncode != 0 else 1
    finally:
        if tcl_file.exists():
            tcl_file.unlink()

if __name__ == "__main__":
    module = sys.argv[1] if len(sys.argv) > 1 else "asp_router"
    sv = sys.argv[2] if len(sys.argv) > 2 else f"rtl/{module}.sv"
    sys.exit(run_vivado_check(module, sv))
