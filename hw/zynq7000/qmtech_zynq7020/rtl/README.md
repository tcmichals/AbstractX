# QMTECH XC7Z020 RTL

This directory owns hand-written, synthesizable board-level RTL for the QMTECH target.

- `top_qmtech_zynq7020.sv` integrates the AbstractX PL fabric with board I/O.
- Shared reusable RTL remains under the repository-level `rtl/` directory.
- Generated Vivado wrappers and build products must not be committed here.
