# AbstractX Design Rules

This document outlines the hard engineering invariants for the AbstractX project. Any pull request or commit that violates these rules must be rejected to ensure the commercial viability and reliability of the IP core.

## 1. Testing and Verification
- **Test-bench Mandate:** Every SystemVerilog (`.sv`) module MUST have a corresponding isolated testbench.
- **Python-first Validation:** We use `cocotb` as our primary testing framework. 
- **Verilator Default:** All `cocotb` testbenches must support compilation and execution via Verilator (`SIM ?= verilator`).
- **No Mock Tie-Offs in Top-Level:** The top-level architectures (e.g., `asp_top.sv`) must not contain hardcoded mock tie-offs for critical datapath signals (such as `tlast`). Top-level wrappers must represent functional, deployable architectures.
- **Self-Documenting Builds:** All `CMakeLists.txt` configurations MUST provide an explicit, human-readable helper mapping (like `make info` or custom module comments) so that generated Makefile targets are entirely self-discoverable by developers.

## 2. Naming Conventions
- **Module Prefix:** All native AbstractX modules should be prefixed with `asp_` (AbstractX Switch Protocol) to avoid namespace collisions in host systems.
- **File Matching:** A module named `asp_foo` MUST reside in a file named `asp_foo.sv`.
- **Testbench Matching:** Testbenches must reside in the `sim/cocotb` directory and must be explicitly linked to their hardware module counterpart (e.g. `test_asp_foo_cocotb.py`).

## 3. Interfaces
- **AXIS Native:** Internal data paths should prefer AXI-Stream (`tdata`, `tvalid`, `tready`, `tlast`) structures for switching and routing.
- **Hardware Isolation:** Physical pin implementations (like SPI protocol decoding) must be isolated into "frontend" or "MAC" modules (e.g., `asp_spi_frontend.sv`). The core routing layers must only ingest byte streams/AXIS.

## 4. AXIS Test Methodology (The AbstractX Standard)
All AXI-Stream modules claiming AbstractX compatibility MUST pass a strict validation methodology in their Cocotb testbenches. Ad-hoc "happy path" testing is strictly rejected. Ensure the following protocol invariants are tested:
- **Random Backpressure Tolerance:** The testbench MUST randomly deassert `tready` (stall) during packet traversal to formally prove that `tdata`, `tvalid`, and `tlast` remain perfectly stable without data dropping or duplication.
- **Framing Integrity:** The testbench MUST computationally validate that `tlast` perfectly matches the boundary of the transaction without slipping cycles or firing early.
- **AXIS Rule Compliance:** Testbenches must verify that modules do not create combinatorial loops (e.g., `tvalid` waiting on `tready`, or `tready` directly driving incoming `tvalid`).

## 4. IP Boundaries
- **Dual-License Rules:** You must not copy/paste aggressively licensed open source code into `asp_` components without verifying compliance with our Dual-License (Open Source + Commercial) model.
