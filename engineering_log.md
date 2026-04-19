# AbstractX Engineering Log

This log chronicles the development, technical decisions, and architecture milestones for the AbstractX project.

## [2026-04-19] - Project Setup & Licensing Model

### What was done
*   Conducted an initial review of the AbstractX repository structure, goals, and existing documentation.
*   Updated `README.md` to formally declare a **Dual-License Model** (Open Source GPLv3 + Commercial License).
*   Created this `engineering_log.md`.

### Why it was done
*   **Commercial Viability:** AbstractX is designed to solve a major pain point in the embedded space (FPGA-to-Linux seamless streaming + control). The dual-license model allows researchers and open-source projects to use it for free (via the viral GPLv3), building community trust and uncovering edge cases. It simultaneously protects the intellectual property, requiring companies building closed-source products to purchase a commercial license.
*   **Traceability:** This engineering log ensures that major design choices, architectural shifts, and daily progress are formally preserved alongside the codebase rather than getting lost in commit histories.

## [2026-04-19] - AXIS Pipeline Migration & Strict Testing Rules

### What was done
*   Imported the core building blocks from the legacy offloader (`spi_frontend`, `router`, `wishbone_master`) and rebranded them to `asp_` prefix.
*   Wrote the top-level `asp_top.sv` integration, generating AXIS `tlast` via a skid buffer pinned to SPI `CS` deassertions.
*   Created `asp_sys_regs.sv` for Wishbone querying.
*   Wrote `asp_tun_driver.py` Linux interface to bridge the host `tun0` to the SPI framework.
*   Setup CMake + Cocotb + Verilator build system (`CMakeLists.txt`, `sim/CMakeLists.txt`, `sim/cocotb/Makefile`).
*   **Enacted a Strict Design Rule**: Every `.sv` file must have a test bench.

### Why it was done
*   Migrating the hardware architecture to native AXIS interfaces over SPI allows for massive parallel routing without sacrificing stability.
*   The strict testing rule ensures the IP core is bulletproof, securing the commercial viability of the dual-license model. 

## Ongoing Work / Next Steps
*   Satisfy the new design rule: Write the missing testbenches for the newly introduced `.sv` wrapper modules (`asp_spi_frontend.sv`, `asp_router.sv`, `asp_wishbone_master.sv`, `asp_sys_regs.sv`).
