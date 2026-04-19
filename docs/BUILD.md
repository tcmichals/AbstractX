# AbstractX Build & Simulation Guide

This document describes how to compile the AbstractX hardware modules, how to run the simulation testbenches, and outlines the directory structure of the repository.

## 1. Prerequisites

To execute the test suite, your environment requires the following open-source tools:
- **CMake** (v3.20+)
- **Verilator** (v5+ recommended)
- **Python 3.10+**
- **Cocotb** (`pip install cocotb`)
- **Make**

## 2. Running Simulations

AbstractX uses a CMake-wrapped regression suite that executes Cocotb + Verilator. 

To run all simulations:
```bash
mkdir build
cd build
cmake ..
make test-all
```

**Discovering Targets:**
If you want to run isolated tests instead of the entire suite, or if you forget the available build options, you can see a human-readable list of all simulation targets by running:
```bash
make info
```

Simulation trace files resulting from `--trace-structs` will be generated inside the `sim/cocotb/` working directory for viewing with GTKWave and similar waveform viewers.

## 3. Directory Layout and Source Breakdown

| Path | Purpose |
|------|---------|
| `/docs/` | System specifications, design invariants, and protocol definitions. |
| `/rtl/` | Core AbstractX routing and interconnect logic. |
| `/rtl/spi/` | Physical SPI interfaces (MAC layers and protocol boundary shims). |
| `/sim/cocotb/` | Python-based testbenches and Cocotb Makefiles. |
| `/` (Root) | CMake scaffolding and host utilities (e.g., `asp_tun_driver.py`). |

### Key RTL Modules
- `asp_spi_frontend.sv`: Translates raw SPI (`MOSI`, `MISO`, `CS`, `SCLK`) into an internal un-framed byte stream.
- `asp_spi_reg_bank.sv`: Translates the byte stream into AXI-Stream (`tdata`, `tvalid`, `tready`), stripping the AbstractX Switch Protocol (ASP) headers.
- `asp_router.sv`: Switch framework that routes AXI-Stream packets to various hardware channels.
- `asp_wishbone_master.sv`: Consumes AXI-Stream control packets to execute Wishbone memory-mapped IO operations against standard peripherals.
- `asp_sys_regs.sv`: The core System Register Wishbone target mechanism. Features the global `VERSION` definition and queryable loopback scratch registers.
- `asp_top.sv`: The top-level wrapper instantiating the entire pipeline. Automatically resolves SPI Chip-Selects into cleanly framed AXIS `tlast` events via a skid buffer.

## 4. Testbench Manifest

Per the repository's strict Design Rules, every RTL module must be targeted by an isolated test.

| Testbench File | Target Module | Status |
|----------------|---------------|--------|
| `test_abstractx_axis_cocotb.py` | `asp_spi_reg_bank.sv` | **Implemented** |
| `test_asp_spi_frontend_cocotb.py` | `asp_spi_frontend.sv` | **Implemented** |
| `test_asp_router_cocotb.py` | `asp_router.sv` | *Pending* |
| `test_asp_wishbone_master_cocotb.py`| `asp_wishbone_master.sv` | *Pending* |
| `test_asp_sys_regs_cocotb.py` | `asp_sys_regs.sv` | *Pending* |

*Note: As pending testbenches are authored, you must uncomment their target module directives inside `sim/cocotb/Makefile` to bind them to the compilation queue.*
