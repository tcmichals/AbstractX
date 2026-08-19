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

## [2026-04-19] - Full Verification & HW Synthesis Milestone

### What was done
*   **Complete Testing Coverage**: Implemented isolated cocotb testbenches for `asp_router`, `asp_sys_regs`, `asp_axis_fifo`, and `asp_wishbone_master`. 
*   **E2E Integration**: Verified the full SPI-to-Wishbone pipeline using the `AbstractX_VIP` (Verification IP) in `test_asp_top_cocotb.py`.
*   **Hardware Synthesis**: Established the Gowin synthesis infrastructure in the `hw/` directory.
*   **Target Success**: Successfully generated bitstreams (`.fs`) for **Tang Nano 9K** and **Tang Primer 20K** using Yosys and nextpnr-himbaechel.
*   **Code Quality**: Achieved 100% pass rate across 21 test cases in the regression suite.

### Why it was done
*   **Production Readiness**: Moving from simulation-only to verified hardware bitstreams proves the RTL is synthesis-ready and fits within the target Gowin devices.
*   **Reliability**: The exhaustive testing of the Wishbone Master (including backpressure handling) ensures no data loss occurs when the SPI host outpaces the internal Wishbone bus.

## Next Steps
*   **Wishbone Burst Support**: Implement CTI/BTE support in the master to optimize block transfers.
*   **Hardware-in-the-loop (HIL)**: Run the Python driver against a physical Tang board.

## [2026-05-02] - QMTECH Zynq-7020 Bring-up Pivot, Buildroot Conventions, and TUN/DMA Path

### What was done
*   Established **QMTECH Zynq-7020** as the next major integration focus, while treating `primer20k` and `tang9k` as already-working reference platforms for existing RT offloader work.
*   Added and documented a dedicated hardware scaffold under `hw/qmtech_zynq7020/` for board-specific notes and bring-up conventions.
*   Recorded the known-good **Pico JTAG/XVC** flow for low-cost Zynq bring-up, specifically referencing:
	*   `https://github.com/kholia/xvc-pico/`
	*   Adam Taylor / Adiuvo article: `https://www.adiuvoengineering.com/post/microzed-chronicles-jtag-using-a-raspberry-pi-pico`
*   Captured the practical decision to keep **Buildroot outputs** in `hw/qmtech_zynq7020/bld/` and **Pico/XVC local build outputs** in `hw/qmtech_zynq7020/pico_bld/`, with both treated as generated artifacts that should not be committed.
*   Added a new **unified Python userspace bridge** at `python/asp_tun_bridge.py` supporting:
	*   SPI backend
	*   DMA backend
	*   auto backend selection
	*   optional CRC policy (`auto|on|off`)
*   Documented the Python TUN/DMA usage model in `python/TUN_FRAMEWORK.md`.
*   Added a **Rust userspace scaffold** at `rust/tun_dma_bridge/` with separated modules for:
	*   ASP frame handling
	*   DMA character-device transport
	*   TUN device setup / packet IO
*   Documented the Rust cross-compilation assumptions for Zynq-7020, including the likely target triple:
	*   `armv7-unknown-linux-gnueabihf`
*   Captured the Zynq-specific practical note that the SoC is **Cortex-A9 / ARMv7-A**, not legacy ARM9.
*   Recorded the device-tree/USB bring-up guidance that external ULPI PHY boards may require:
	*   `compatible = "usb-nop-xceiv";`
	*   proper PHY reset GPIO handling
	*   matching kernel config such as `CONFIG_NOP_USB_XCEIV`

### Why it was done
*   **Focus on the riskiest unknowns first:** The Gowin boards are already delivering value for RT offloader work. The Zynq-7020 path introduces the more complex system problems—PS/PL integration, DMA plumbing, Buildroot/BSP management, Linux device-tree issues, USB, and low-jitter host interfaces—so it has the highest leverage as the next development target.
*   **Preserve a fast iteration loop:** Python remains the quickest path for bring-up, inspection, and protocol iteration. A single userspace bridge that can talk SPI now and DMA next avoids fragmenting the workflow across multiple scripts.
*   **Create a low-jitter migration path:** Rust userspace is a good next step for TUN + DMA because it improves safety and maintainability over long-running data-plane code while remaining much easier to iterate than immediate kernel work.
*   **Avoid overcommitting too early to in-kernel Rust:** Rust-for-Linux on ARMv7 is promising but not the best first milestone for this board. Userspace Rust gives most of the development benefits early, while leaving room for a kernel move later if p99 latency requires it.
*   **Keep protocol compatibility without unnecessary overhead:** CRC was made policy-based rather than removed outright. SPI/serial-style links should keep CRC enabled; DMA paths inside the box can reasonably disable CRC while preserving the frame field for compatibility.
*   **Keep generated outputs out of version control:** The Buildroot `bld` tree and Pico/XVC local build output are large, host-specific, and not appropriate for git. Explicit directory conventions reduce repo noise and accidental commits.
*   **Leverage low-cost tooling:** `xvc-pico` provides a practical path for Zynq bring-up without requiring a dedicated Xilinx cable, lowering friction for experimentation and board recovery.
*   **Use the existing QMTECH BSP as the board source of truth:** The separate `QMTECH` repository already contains the Buildroot external-tree baseline, so this repo should integrate with it rather than duplicate and drift.

### Practical decisions captured
*   Prioritize **QMTECH Zynq-7020** over new Gowin work for the next phase.
*   Keep **Buildroot baseline bring-up** as the first milestone; do not block early progress on a bleeding-edge kernel jump.
*   Use:
	*   `bld` for Buildroot output
	*   `pico_bld` for Pico/XVC local build output
*   Use Python for near-term TUN/DMA bring-up.
*   Use Rust userspace as the long-term primary software path for TUN + DMA hardening.
*   Treat in-kernel Rust as conditional / later, not the first dependency for success.

### Short design rationale summary
*   **CRC off for DMA** because DMA is an in-box trusted path; keep CRC on for SPI.
*   **Python now** for speed of bring-up and debugging.
*   **Rust next** for a safer, more durable userspace TUN + DMA bridge.
*   **Kernel later** only if measured latency/jitter requires it.

### Updated next steps
*   Boot the QMTECH Zynq-7020 using the existing Buildroot external-tree baseline.
*   Verify USB host bring-up with the `usb-nop-xceiv` DT pattern and reset GPIO if required.
*   Confirm DMA device nodes appear and are usable from Linux.
*   Exercise `python/asp_tun_bridge.py` over DMA with CRC disabled.
*   Cross-compile and package `rust/tun_dma_bridge` against the Buildroot ABI once the basic board bring-up is stable.

## [2026-08-11] - Architectural Pivot: PCIe-like 64-Byte Fixed TLP Protocol & IMU Auto-DMA IP Core

### What was done
*   **Architecture Pivot (`asp-tlp-64b`)**: Formally transitioned AbstractX from legacy variable-length framing (`asp-compat-v1`) to a **PCIe-like 64-byte Transaction Layer Packet (TLP)** specification (`asp-tlp-64b`).
*   **TLP Operations Defined**: Established normative definitions for `MemRd` (0x01), `MemWr` (0x02), `CplD` (0x03), `Cpl` (0x04), and `DMA_Stream` (0x10) operations targeting Wishbone registers and streaming channels.
*   **Dual-SPI Physical Layer**: Standardized on Dual-SPI SDR mode (256 SCLK cycles per 64-byte TLP) as the primary external link layer.
*   **UART ESC DMA Engine Optimization**: Formally specified the dual-trigger flush policy (40-byte full buffer trigger OR 2-character idle timeout trigger).
*   **IMU SPI Master & Auto-DMA Core Specification**: Created [`docs/IMU_AUTO_DMA_IP_SPEC.md`](file:///home/tcmichals/ssdData/projects/home/AbstractX/docs/IMU_AUTO_DMA_IP_SPEC.md) detailing the hardware SPI master, `IMU_INT` hardware interrupt trigger, 64-bit hardware timestamp latching, and 64B TLP packetization.
*   **Documentation Suite Updated**: Updated all canonical spec docs (`ASP_SPEC_DIRECTION.md`, `ASP_PROTOCOL.md`, `ASP_SPI_TRANSPORT.md`, `ASP_SPI_REGISTER_MAP.md`, `ABSTRACTX_SWITCH_FABRIC_ARCHITECTURE.md`, `README.md`).

### Why it was done
*   **FPGA Core Logic Savings**: Dynamic byte-stream parsers and dynamic framing state machines consume excessive LUT/FF resources. Fixed 64-byte (512-bit) vectors allow ultra-compact shift registers and word-aligned FIFOs, maximizing $f_{MAX}$ and lowering gate count on Gowin and Zynq target FPGAs.
*   **Pipelined Split Transactions**: PCIe-style `Tag` correlation allows the host CPU to issue non-blocking `MemRd` requests without stalling the Dual-SPI interface.
*   **Sub-Microsecond Telemetry Jitter**: Capturing a 64-bit hardware timestamp at the exact clock cycle of the `IMU_INT` trigger eliminates software interrupt latency and jitter.
*   **UART ESC Efficiency**: The 2-character idle timeout guarantees low latency for short serial frames while the 40-byte threshold maintains maximum link efficiency during active streaming.

## [2026-08-11] - InvenSense ICM-42688-P iNav Driver Alignment & Pure Python Cocotb VIP

### What was done
*   **iNav Driver Cross-Reference**: Analyzed the official iNav flight controller driver ([`accgyro_icm42605.c`](file:///home/tcmichals/ssdData/projects/home/flightcode/inav/src/main/drivers/accgyro/accgyro_icm42605.c)) from `/home/tcmichals/ssdData/projects/home/flightcode/inav`.
*   **Python Cocotb VIP (`sim/cocotb/icm42688p_cocotb_vip.py`)**: Implemented a pure Python hardware emulator for the InvenSense / TDK ICM-42688-P IMU matching iNav register configurations (`WHO_AM_I = 0x47`, `PWR_MGMT0 = 0x0F`, `GYRO_CONFIG0 = 0x06`, `ACCEL_CONFIG0 = 0x06`, `INT_CONFIG = 0x03`, `INT_SOURCE0 = 0x08`, 14-byte continuous read starting at `TEMP_DATA1 0x1D`).
*   **Cocotb Verification Testbench (`sim/cocotb/test_asp_tlp_64b_cocotb.py`)**: Implemented a test sequence executing the iNav driver initialization, DRDY interrupt generation, 14-byte hardware SPI burst read, and `o_int_req` doorbell IRQ assertion. Passed 100%.
*   **Documentation Suite Updated**: Added Section 5 ("iNav Flight Controller Driver Integration & Alignment Review") to [`docs/IMU_AUTO_DMA_IP_SPEC.md`](file:///home/tcmichals/ssdData/projects/home/AbstractX/docs/IMU_AUTO_DMA_IP_SPEC.md) and updated [`walkthrough.md`](file:///home/tcmichals/.gemini/antigravity-ide/brain/99f37398-c760-4741-a219-52c473d36ea7/walkthrough.md).

### Why it was done
*   **Synthesizable RTL Isolation**: Using pure Python Cocotb coroutines for external device pin driving eliminates the need for testbench Verilog files, keeping the RTL codebase 100% synthesizable.
*   **Flight Code Compatibility**: Aligning the FPGA hardware Auto-DMA registers and Python VIP directly with iNav ensures zero friction when porting flight controller software to the Cubie A5E target.

## [2026-08-19] - C++20 Coroutine Flight Controller Architecture & SITL Simulator Proof

### What was done
*   **C++20 Coroutine Header Core (`include/asp_coro.hpp`)**: Implemented stackless `Task<T>`, `TlpAwaiter` split-transaction dispatchers, `CoroutineIoEngine`, and combinators (`when_all` / `&&` and `when_any` / `||`) designed specifically for embedded flight loops and 64B TLPs.
*   **SITL Simulator Benchmark (`sim/sitl_coro_sim.cpp`)**: Created a Software-in-the-Loop simulator modeling background hardware coprocessor/bus latencies (8 kHz IMU stream, 50 Hz I2C Barometer @ 1.5 ms latency, 100 Hz I2C Magnetometer @ 800 µs latency).
*   **Multi-Rate Interleaving Verified**: Proved that a single-threaded flight loop executing C++20 coroutines seamlessly runs ~12 IMU rate loop iterations while a 1.5 ms I2C Barometer read is pending in hardware, with zero CPU blocking, zero OS context switches, and zero mutex locks.
*   **Combinator Verification (`sim/test_coro_combinators.cpp`)**: Verified concurrent `when_all` joins and `when_any` timeout/race mechanisms.
*   **Architecture Guide**: Published [`docs/COROUTINE_FLIGHT_CONTROLLER_ARCHITECTURE.md`](file:///home/tcmichals/ssdData/projects/home/AbstractX/docs/COROUTINE_FLIGHT_CONTROLLER_ARCHITECTURE.md).

### Why it was done
*   **Decouples Flight Code from I/O Latencies**: Solves the historical flight controller dilemma between bare-metal state machine spaghetti (iNav/Betaflight) and high-overhead multi-threaded mutex contention (ArduPilot Linux).
*   **Addresses Linux I/O Blocking**: Overcomes the lack of `epoll` support for `/dev/spidev` and `/dev/i2c-dev` by running background bus worker threads while the top-level flight loop runs on a single deterministic coroutine thread.

## [2026-08-19] - General Framework Shift & The 3 Software Execution Environments

### What was done
*   **3 Execution Environments Specified**: Formally published [`docs/ABSTRACTX_SOFTWARE_ENVIRONMENTS.md`](file:///home/tcmichals/ssdData/projects/home/AbstractX/docs/ABSTRACTX_SOFTWARE_ENVIRONMENTS.md) detailing:
    1. **Environment 1 (Linux Host / SITL)**: Single C++20 coroutine thread + dedicated POSIX I/O worker threads per blocking bus (`/dev/spidev`, `/dev/i2c-dev`).
    2. **Environment 2 (Multi-Core MCU / AMP)**: Core 1 application coroutines + Core 0 interrupt/DMA/PIO coprocessor (RP2350 Pico 2W, ESP32-P4, STM32).
    3. **Environment 3 (FPGA Hardware Offloader)**: Host CPU coroutines + synthesizable RTL state machines & 512-bit vector router (Gowin Tang 9K/20K, Zynq-7020).
*   **Multi-Target TLP Header (`include/asp_tlp_msg.hpp`)**: Standardized universal 20B `TlpHeader`, fixed 64B `Tlp64` (FPGA), compact 24B `TlpShort` (MCU Reg R/W), variable `TlpVar<N>`, and 48B `TlpDescriptor` (Linux zero-copy).
*   **Top-Level Rebranding (`README.md`)**: Elevated AbstractX to a **Universal Asynchronous Hardware Offloader & Heterogeneous Interconnect Framework**, covering Robotics (ROS2), Industrial DAQ, Battery Management (BMS), and Aviation.

### Why it was done
*   **100% Application Portability**: Developers can write C++20 coroutine control logic once and run it unchanged across PC simulation, low-cost microcontrollers, and high-performance FPGA platforms.
*   **Silicon-Optimal Messaging**: Uses fixed 64B on FPGA to minimize gate count, while using compact 24B/variable/zero-copy on MCUs/Linux to conserve memory and preserve CPU cache locality.

## [2026-08-19] - Freestanding Bare-Metal Microcontroller SPSC Architecture & ISR Safety

### What was done
*   **Freestanding C++20 Headers**: Audited and stripped all OS headers (`<mutex>`, `<thread>`, `<condition_variable>`, `<iostream>`, `<queue>`, `<vector>`) from `include/`, ensuring 100% portability to bare-metal ARM GCC and RISC-V GCC toolchains.
*   **Single-Core ISR Safety Verified (`sim/test_baremetal_isr_spsc.cpp`)**: Proved that SPSC queues are 100% lock-free, wait-free, and re-entrant between a hardware ISR (Producer) and the main flight loop coroutine (Consumer) with zero critical sections (`__disable_irq`).
*   **Dual-Core AMP Shared SRAM**: Verified lock-free atomic release/acquire memory barriers (`DMB` / `FENCE`) across Core 0 (I/O coprocessor) and Core 1 (Coroutine engine) on RP2350 Pico 2W and ESP32-P4.
*   **Dedicated Lock-Free SPSC Channel Array (`sim/test_spsc_channel_array.cpp`)**: Implemented `SpscChannelArray<T, NumChannels>` with zero mutexes and zero runtime heap allocations.
*   **Documentation Suite Updated**: Added Section 10 to [`docs/ABSTRACTX_SOFTWARE_ENVIRONMENTS.md`](file:///home/tcmichals/ssdData/projects/home/AbstractX/docs/ABSTRACTX_SOFTWARE_ENVIRONMENTS.md) and updated [`README.md`](file:///home/tcmichals/ssdData/projects/home/AbstractX/README.md).

### Why it was done
*   **Zero OS Footprint**: Microcontrollers in avionics, robotics, and industrial DAQ do not run an OS. Core data structures must execute safely in raw silicon with zero runtime heap dependencies.
*   **Interrupt & Inter-Core Determinism**: Lock-free SPSC guarantees that neither hardware interrupts on single-core MCUs nor parallel coprocessor execution on dual-core MCUs can corrupt the coroutine scheduler state.


