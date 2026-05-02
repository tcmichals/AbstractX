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
