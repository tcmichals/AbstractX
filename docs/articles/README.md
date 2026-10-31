# AbstractX Articles

Introductory and explanatory articles live here. They explain ideas and
examples; subsystem `SPECIFICATION.md` files remain authoritative for
requirements, and target `HOWTO.md` files remain the source for build and
hardware setup instructions.

## What AbstractX Is: Accelerators Run the Buses, Linux Sees Results

Every article here builds on one idea ([`[SPEC-ARCH-14]`](../DESIGN_SPECIFICATION.md)): **Linux never
drives a sensor bus.** `spidev`, `i2c-dev` and per-sample GPIO interrupts are slow and jittery, so a
processor or fabric dedicated to I/O owns SPI, I2C, UART, DShot and PWM, starts each transfer from the
sensor's own trigger with DMA, and runs the fast loop where the samples land. Linux (or the other MCU
core) sees fused state, telemetry and trace through rings in shared memory, in blocks, never per
sample, with no custom kernel driver. On FPGAs the accelerator is 100% PIO (pio2).

![AbstractX accelerator split: sensors and actuators are driven only by an I/O accelerator per platform (Pico 2 W core 0, an ESP32-P4 core, the XuanTie E906, pio2 on Zynq or Tang Nano), which hands results through rings to Core 1, the app core, Linux or the SPI-link host; only the E906 path is built and measured today](assets/abstractx-accelerator-split.svg)

Today only the E906 path is built and measured (DRDY edge -> DMA started ~6 µs, edge -> consumer
coroutine ~31 µs average, Part 6). Where an article uses Linux `spidev`, it is either bring-up
(the intro's Python stage) or the baseline being beaten (Part 6's Linux column), never the product.

## Articles & Benchmark Series

- [Embedded Firmware Observability on a Budget: Tracking Memory & Traceability Using Pure GitHub](./GITHUB_NATIVE_EMBEDDED_OBSERVABILITY.md)
- [Deterministic HW/SW Co-Design: Structuring Knowledge Bases for Agentic AI in FPGA & Embedded Systems](./AGENTIC_AI_AND_RAG_COMPARISON.md)
- [Scaling Beyond Fixed TLPs: Lessons from Open-Source PCIe & AXI-Stream (Alex Forencich Patterns)](./PCIE_AXI_STREAMING_ARCHITECTURAL_LESSONS.md)
- [Straight-Line Async: GPS + Compass with C++20 Coroutines](./gps-compass-straight-line-async/README.md)
- [Experiment First, Compile Later: Python on Both Processors of the Allwinner T527 (Part 5: device tree, remoteproc, RPMsg streams, MicroPython on the E906, Studio)](./micropython-on-e906/README.md)
- [AbstractX vs FreeRTOS on the XuanTie E906](./abstractx-vs-freertos-e906/README.md)
- [Where Should the IMU Live on the T527? Linux on the A55, or the E906? (Part 6, draft: E906 bare-metal column measured, others pending)](./sensor-placement-t527/README.md)
- [Jumpstarting Heterogeneous Silicon with AbstractX: From Python `asyncio` to Freestanding C++20](./INTRO_ABSTRACTX_HETEROGENEOUS_DEVELOPMENT.md)

