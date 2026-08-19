# AbstractX Build & Simulation Guide

This document describes how to compile the AbstractX hardware modules, how to run the simulation testbenches, and outlines the directory structure of the repository.

## 1. Prerequisites

To execute the test suite, your environment requires the following open-source tools:
- **CMake** (v3.20+)
- **Verilator** (v5+ recommended)
- **Python 3.10+**
- **Cocotb** (`pip install cocotb`)
- **Make**

For the active QMTECH Zynq-7020 direction, also expect:

- an external Buildroot/BSP baseline from `https://github.com/tcmichals/QMTECH`,
- an out-of-tree Buildroot output directory such as `hw/qmtech_zynq7020/bld/`, and
- optional Pico/XVC local build output in `hw/qmtech_zynq7020/pico_bld/`.

## 2. Building & Running Tests with CMake

AbstractX provides a unified, modern **CMake + CTest** build system for all C++20 coroutine engines, simulation harnesses, and example applications:

### Quick Build & Test:
```bash
# 1. Configure CMake build tree
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 2. Build all simulation engines and examples in parallel
cmake --build build -j$(nproc)

# 3. Run the complete test suite via CTest (100% passing)
ctest --test-dir build --output-on-failure
```

### Running Individual Targets:
```bash
# Multi-Target Hardware Proof Benchmark:
./build/examples/simple_proof_benchmark

# Generic Non-Sensor Hardware I/O & Messaging Demo:
./build/examples/generic_io_messaging_demo

# Aerospace Redundant Dual-IMU Failover:
./build/examples/redundant_imu_failover

# 4-Axis Robotics Synchronized Motion Controller:
./build/examples/robotics_multi_axis_motion

# SITL Flight Multi-Sensor Coroutine Simulation:
./build/sim/sitl_coro_sim
```

### Running Hardware RTL Cocotb Tests:
```bash
make -C sim/cocotb clean
make -C sim/cocotb TOPLEVEL=asp_tlp_64b_top MODULE=test_asp_tlp_64b_cocotb
```

---

## 3. Directory Layout and Source Breakdown

| Path | Purpose |
|------|---------|
| `/include/` | Header-only freestanding C++20 Coroutine & PCIe TLP engine (`asp_coro.hpp`, `spsc_tlp_ring.hpp`, `asp_tlp64.hpp`). |
| `/examples/` | Standalone multi-domain applications (Flight Controller, Robotics, Redundancy, Generic I/O). |
| `/sim/` | C++20 SITL harnesses, Bare-Metal MCU SPSC tests, and Halo worker pool models. |
| `/rtl/` | SystemVerilog RTL modules (Router, Dual-SPI, IMU Auto-DMA, Wishbone Master, System Registers). |
| `/docs/` | System specifications, design invariants, case studies, and verification evidence. |

---

## 4. Testbench Manifest

| Testbench File | Target Subsystem / Module | Type | Status |
|---|---|---|---|
| `test_asp_tlp_64b_cocotb.py` | `asp_tlp_64b_top.sv` | RTL Cocotb | ✅ **Passed (100%)** |
| `test_asp_wishbone_master_cocotb.py` | `asp_wishbone_master.sv` | RTL Cocotb | ✅ **Passed (100%)** |
| `test_asp_router_cocotb.py` | `asp_router.sv` | RTL Cocotb | ✅ **Passed (100%)** |
| `test_asp_sys_regs_cocotb.py` | `asp_sys_regs.sv` | RTL Cocotb | ✅ **Passed (100%)** |
| `test_asp_axis_fifo_cocotb.py` | `asp_axis_fifo.sv` | RTL Cocotb | ✅ **Passed (100%)** |
| `test_asp_spi_frontend_cocotb.py` | `asp_spi_frontend.sv` | RTL Cocotb | ✅ **Passed (100%)** |
| `simple_proof_benchmark.cpp` | Multi-Target Hardware Proof (Linux/Pico2/ESP32/FPGA) | C++20 CTest | ✅ **Passed (100%)** |
| `generic_io_messaging_demo.cpp` | Generic Actuator, Storage & Messaging | C++20 CTest | ✅ **Passed (100%)** |
| `redundant_imu_failover.cpp` | Aerospace Dual-Sensor Redundancy | C++20 CTest | ✅ **Passed (100%)** |
| `robotics_multi_axis_motion.cpp` | 4-Axis Robotics Motion Control | C++20 CTest | ✅ **Passed (100%)** |
| `sitl_coro_sim.cpp` | Multi-Rate Flight SITL Coroutines | C++20 CTest | ✅ **Passed (100%)** |
| `test_baremetal_isr_spsc.cpp` | Freestanding MCU ISR SPSC Handoff | C++20 CTest | ✅ **Passed (100%)** |
| `test_spsc_channel_array.cpp` | Lock-Free SPSC Array & Vector Router | C++20 CTest | ✅ **Passed (100%)** |
| `test_halo_worker_pool.cpp` | Multi-Threaded I/O Worker Pool | C++20 CTest | ✅ **Passed (100%)** |
| `test_tlp_msg.cpp` | Multi-Target Message Sizing Verification | C++20 CTest | ✅ **Passed (100%)** |
