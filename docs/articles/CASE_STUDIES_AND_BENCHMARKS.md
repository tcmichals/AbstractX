# AbstractX Benchmark Series & Case Studies: Multi-Silicon Speed & Offload

This document outlines the three benchmark case studies demonstrating AbstractX's speed, memory efficiency, and polymorphic silicon scaling across the **Raspberry Pi Pico 2 W**, **Allwinner Radxa Cubie A5E (E906)**, and the **QMTECH Zynq-7020 FPGA**.

---

## Article 1: Sub-Microsecond Concurrency on $5 Silicon
### *How AbstractX Squeezes 8 kHz Flight Loops onto the Raspberry Pi Pico 2 W (RP2350)*

* **Target Hardware**: Raspberry Pi Pico 2 W (Dual ARM Cortex-M33 @ 150 MHz, hardware FPU).
* **Core Problem**: Traditional RTOSes (FreeRTOS, Zephyr) allocate 2–4 KB dedicated SRAM stacks per sensor thread and burn hundreds of clock cycles per context switch. Default vendor SDKs rely on blocking polling loops (`i2c_write_blocking`, `spi_write_blocking`) that freeze the CPU.
* **AbstractX Architecture**:
  * **Core 0 (I/O Processor / PCIe Switch)**: Runs non-blocking DMA and interrupt-driven state machines for SPI1, I2C0, and GPIO. Decodes virtual BAR addresses (`bar::LedBase`, `bar::EscBase`) and forward-dispatches without blocking.
  * **Core 1 (Flight Loop)**: Runs the C++20 stackless coroutine task graph on a **single shared 2 KB stack** with **zero dynamic heap allocation**.
  * **The Interconnect**: Shared SRAM ring (`SpscTlpRing<64>`) with hardware SIO FIFO doorbells (`sio_hw->fifo_wr = 1`).
* **Key Benchmarks**:
  * **Context Switch Overhead**: **15 CPU cycles** (~100 ns @ 150 MHz) vs. **300+ cycles** (2–3 µs) in FreeRTOS/Zephyr.
  * **RAM Footprint**: **< 100 bytes** per suspended coroutine frame in `.bss` vs. **2,048–4,096 bytes** per OS thread stack (95% memory savings).
  * **CPU Utilization for Useful Math**: **94% of CPU cycles** on Core 1 dedicated to AHRS matrix calculations and EKF state estimation.

---

## Article 2: Defeating Vendor SDK Bloat
### *Offloading Linux Real-Time I/O to a RISC-V Coprocessor (Allwinner Radxa Cubie A5E)*

* **Target Hardware**: Allwinner Radxa Cubie A5E (Quad AArch64 Cortex-A55 @ 1.4 GHz Linux + XuanTie E906 RISC-V Coprocessor).
* **Core Problem**: Linux non-real-time kernel jitter causes unacceptable scheduling spikes for 8 kHz flight loops. Meanwhile, vendor-provided RTOS SDKs for the E906 RISC-V coprocessor suffer from massive binary bloat and complex OpenAMP/RPMsg boilerplate.
* **AbstractX Architecture**:
  * **Linux Userspace (Application Domain)**: Operates as a PCIe Root Complex. Emits 64-byte TLPs into shared SRAM rings without kernel syscalls or driver context switches.
  * **XuanTie E906 (I/O Processor / PCIe Switch)**: Bare-metal freestanding C++20 image (< 16 KB binary footprint) servicing on-chip SPI0 and UART2 via PLIC hardware interrupts and `sun6i-msgbox` doorbells.
  * **Zero RPC Boilerplate**: Linux communicates via clean bus memory transactions (`make_mem_write`, `make_mem_read`) to the virtual BAR map.
* **Key Benchmarks**:
  * **Binary Footprint**: **< 16 KB** freestanding AbstractX binary vs. **> 300 KB** Allwinner vendor RTOS SDK image.
  * **I/O Latency**: Sub-microsecond deterministic response on E906 vs. 50–200 µs Linux scheduler jitter.
  * **Zero Linux Kernel Drivers Required**: Sensors are driven directly by E906; Linux userspace sees raw CTF 1.8 telemetry frames.

---

## Article 3: Polymorphic Silicon: From C++ to SystemVerilog
### *Offloading the I/O Processor to an FPGA Switch Fabric (QMTECH Zynq-7020)*

* **Target Hardware**: QMTECH Zynq-7020 (Dual ARM Cortex-A9 @ 667 MHz PS + Xilinx Artix-7 FPGA PL).
* **Core Problem**: High-rate robotic systems eventually hit CPU limits when servicing high-frequency interrupts (e.g. multi-axis ESC DShot bursts, 8 kHz IMU DRDY, lidar pulses). Rewriting software drivers into FPGA RTL traditionally requires completely rethinking the software architecture.
* **AbstractX Solution (Polymorphic I/O)**:
  * **Zero Software Changes**: The Linux flight loop code is **100% identical** to the MCU version. It continues pushing 64-byte TLPs to `/dev/uio0` DMA coherent rings in DDR memory.
  * **The FPGA Switch Fabric (`rtl/asp_router.sv`)**: The C++ I/O processor is replaced in silicon by an autonomous SystemVerilog crossbar switch.
  * **Autonomous Auto-DMA (`rtl/asp_imu_auto_dma.sv`)**: Hardware latches nanosecond timestamps on physical DRDY pin interrupts, clocks the SPI bus via hardware DMA, and packetizes 64-byte TLPs directly into the AXI-Stream bus.
* **Key Benchmarks**:
  * **CPU Utilization on ARM Host**: **0.0% CPU wait states**; the FPGA performs 100% of the bus clocking, CRC32 generation, and packet framing in hardware.
  * **Interrupt-to-Bus Latency**: **< 10 nanoseconds** hardware latching vs. microseconds of software ISR latency.
  * **Formal Parity**: Dual verification across CppUTest SITL and Cocotb Verilator co-simulations with 100% grand traceability via SpecTrace.

---

## Article 4: Embedded Firmware Observability on a Budget
### *Tracking Firmware Memory Budgets & Test Traceability with Pure GitHub*

* **Full Article**: [`docs/articles/GITHUB_NATIVE_EMBEDDED_OBSERVABILITY.md`](GITHUB_NATIVE_EMBEDDED_OBSERVABILITY.md)
* **Core Problem**: Many embedded teams and open-source projects need automated RAM/Flash budget tracking and requirement verification in CI, but lack the budget or operational bandwidth to manage external cloud services, API tokens, and separate dashboards.
* **AbstractX Solution**:
  * **Standard GNU Binutils**: Uses `size -A` to calculate physical SRAM/Flash buckets and `nm -u` to audit for forbidden heap symbols (`malloc`, `free`, `operator new`) at the ELF binary level.
  * **Native `$GITHUB_STEP_SUMMARY`**: Formats rich summary tables, pass/fail status meters, and Mermaid charts directly inside GitHub Actions runs without third-party plugins.
  * **Native PR Comments via `actions/github-script`**: Updates a single, in-place PR comment with SRAM allocation pie charts and test execution speed deltas without comment spam.
  * **Git as the Single Source of Truth**: Commits living evidence directly to [`docs/verification/RUNNING_PROFILE.md`](../verification/RUNNING_PROFILE.md) on merge to `main`.
* **Key Advantages**:
  * **$0 / Month Cloud Bill**: 100% free, zero tokens, zero external API keys.
  * **Permanent Version-Controlled Record**: Metrics branch, tag, and diff with your code in Git forever.
