# ALINX AC7020C RTL

This directory owns future hand-written, synthesizable board-level RTL for the AC7020C target.

The current baseline is assembled by `vivado/system_bd.tcl`; Vivado generates `system_wrapper` in the ignored build directory. Hand-written validation tops belong here, while generated wrappers and build products must not be committed.

Shared reusable RTL remains under the repository-level `rtl/` directory.
