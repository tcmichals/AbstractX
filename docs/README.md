# AbstractX Architecture & Documentation Hub

This is the technical documentation index for AbstractX, an embedded systems
research project exploring cooperative C++20 applications, asynchronous
peripheral access, and FPGA-attached I/O. The repository contains host,
microcontroller, Linux, and FPGA work at different levels of completeness;
presence of a target guide does not by itself mean that target is production
ready or hardware-validated.

The documentation is organized as a three-tier navigation model. The relevant
`SPECIFICATION.md` is authoritative for each subsystem; this index helps locate
it and should not duplicate its requirements.

## Articles

**[`articles/`](articles/)** contains introductory, explanatory articles. They
complement—not replace—the normative specifications and target build guides.

- **[`articles/gps-compass-straight-line-async/`](articles/gps-compass-straight-line-async/README.md)**:
  An introductory Pico 2 W sensor-node design using GPS over UART, a QMC5883L
  over I²C, an LED, and cooperative C++20 coroutines. Its theme is straight-line
  async: readable sequential coroutine code over event-driven I/O. It discusses
  the move from Protothreads and puts AbstractX alongside other embedded
  frameworks such as Pigweed without framing them as competitors.

```
┌────────────────────────────────────────────────────────────────────────┐
│      TIER 1: ABSTRACT VISION & ARCHITECTURAL INVARIANTS                │
│      • docs/tier1_vision/SYSTEM_INVARIANTS.md                          │
│      • Zero dynamic heap, single shared stack, Primary-Paced channels  │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │ Governs
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│      TIER 2: SYSTEM CONTRACTS, PROTOCOL & OBSERVABILITY                │
│      • docs/tier2_contracts/TLP_BUS_SPECIFICATION.md                   │
│      • docs/tier2_contracts/HAL_INTERFACES.md                          │
│      • docs/tier2_contracts/OBSERVABILITY_SCHEMA.md                    │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │ Implemented by
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│      TIER 3: CONCRETE TARGET BSPS, HARDWARE & APPLICATIONS             │
│      • docs/tier3_targets/bsp/ (Pico 2 W, ESP32-P4, Allwinner E907)    │
│      • docs/tier3_targets/hardware/ (Waveshare P4 Errata, Tang Pinouts)│
│      • apps/gps_imu_app/ (Reference Flight Application)                │
└────────────────────────────────────────────────────────────────────────┘
```

---

## Tier 1: Abstract Vision & Architectural Invariants ([`tier1_vision/`](tier1_vision/))

Defines the permanent, non-negotiable laws and execution models of the AbstractX framework:

1. **[`tier1_vision/SYSTEM_INVARIANTS.md`](tier1_vision/SYSTEM_INVARIANTS.md)**  
   *Why*: **Master Tier 1 Document**. Authoritative definition of what AbstractX is/is not, the 3-layer architecture, zero-heap freestanding invariants, queue safety rules, and the 3 execution environments.
2. **[`tier1_vision/COROUTINE_FLIGHT_CONTROLLER_ARCHITECTURE.md`](tier1_vision/COROUTINE_FLIGHT_CONTROLLER_ARCHITECTURE.md)**  
   *Why*: The C++20 stackless coroutine task graph (`abstractx/coro.hpp`), split-transaction dispatchers, and concurrency combinators (`when_all` / `when_any`).
3. **[`tier1_vision/PORTABLE_FLIGHT_STACK_ARCHITECTURE.md`](tier1_vision/PORTABLE_FLIGHT_STACK_ARCHITECTURE.md)**  
   *Why*: Silicon offloader architecture detailing hardware offload mechanisms across silicon targets.
4. **[`tier1_vision/ABSTRACTX_SWITCH_FABRIC_ARCHITECTURE.md`](tier1_vision/ABSTRACTX_SWITCH_FABRIC_ARCHITECTURE.md)**  
   *Why*: Parallel vector router fabric and Wishbone gateway architecture.
5. **[`tier1_vision/PROMPT_MARKDOWN_DRIVEN_ARCHITECTURE.md`](tier1_vision/PROMPT_MARKDOWN_DRIVEN_ARCHITECTURE.md)**  
   *Why*: Principles and rules for specification-driven development.
6. **[`tier1_vision/historical/`](tier1_vision/historical/)**  
   *Why*: Preserved historical whitepapers, design goals, and comparative scheduler analyses (`PROTOTHREADS_TO_COROUTINE_WHITEPAPER.md`, `SCHEDULER_VS_COROUTINE_ANALYSIS.md`, `ABSTRACTX_DESIGN_GOALS.md`, `DESIGN_RULES.md`).

---

## Tier 2: System Contracts, Protocol & Observability ([`tier2_contracts/`](tier2_contracts/))

Defines universal integration boundaries, binary framing, HAL contracts, and the telemetry ecosystem:

1. **[`tier2_contracts/TLP_BUS_SPECIFICATION.md`](tier2_contracts/TLP_BUS_SPECIFICATION.md)**  
   *Why*: **Universal Data Plane Specification**. Fixed 64-byte TLP headers, packet operations (`MemRd`, `MemWr`, `CplD`, `DMA_Stream`), Wishbone gateway address map, and Dual-SPI 50 MHz physical transport.
2. **[`tier2_contracts/HAL_INTERFACES.md`](tier2_contracts/HAL_INTERFACES.md)**  
   *Why*: **Symmetrical HAL Contracts**. Non-blocking awaitable HAL interfaces (`ISpi`, `II2c`, `IUart`, `ITimer`, `IMailbox`) and the zero-blocking driver mandate.
3. **[`tier2_contracts/OBSERVABILITY_SCHEMA.md`](tier2_contracts/OBSERVABILITY_SCHEMA.md)**  
   *Why*: **Universal Observability Contracts**. Dynamic CTF 1.8 schema engine, `PlatformTopologyTable`, and the Two-Level AbstractX Studio architecture.
4. **[`tier2_contracts/observability/README.md`](tier2_contracts/observability/README.md)**  
   *Why*: Comprehensive Observability & Tooling Guide, MemBrowse continuous tracking, and Python `.venv` setup.
5. **[`tier2_contracts/FCPROTOCOL_SPECIFICATION.md`](tier2_contracts/FCPROTOCOL_SPECIFICATION.md)**  
   *Why*: MSP and Mavlink compatible telemetry frame bridging.

---

## Tier 3: Concrete Target BSPs, Peripherals & Applications ([`tier3_targets/`](tier3_targets/))

Documents specific silicon targets, hardware pinouts, auto-DMA cores, and errata workarounds:

### Target Board Support Packages ([`tier3_targets/bsp/`](tier3_targets/bsp/))
1. **[`tier3_targets/bsp/PICO2W_DUAL_CORE_ARCHITECTURE.md`](tier3_targets/bsp/PICO2W_DUAL_CORE_ARCHITECTURE.md)**: RP2350 Dual Cortex-M33 AMP topology and CYW43439 Wi-Fi.
2. **[`tier3_targets/bsp/E907_COPROCESSOR_ARCHITECTURE.md`](tier3_targets/bsp/E907_COPROCESSOR_ARCHITECTURE.md)**: Allwinner XuanTie E907 RISC-V coprocessor + shared SRAM.
3. **[`tier3_targets/bsp/LINUX_DEVICE_TREE_GUIDE.md`](tier3_targets/bsp/LINUX_DEVICE_TREE_GUIDE.md)**: Linux Device Tree overlays for Dual-SPI 2x mode.

### Hardware Pinouts, IP Cores & Errata ([`tier3_targets/hardware/`](tier3_targets/hardware/))
1. **[`tier3_targets/hardware/ESP32P4_WAVESHARE_WIFI6_AND_ERRATA_SPEC.md`](tier3_targets/hardware/ESP32P4_WAVESHARE_WIFI6_AND_ERRATA_SPEC.md)**: Waveshare ESP32-P4-WiFi6 (SKU 32021) hardware architecture, silicon errata analysis ([DMA-767], [MSPI-750], [APM-560]), and non-blocking Video-to-SDCard DMA pipeline.
2. **[`tier3_targets/hardware/IMU_AUTO_DMA_IP_SPEC.md`](tier3_targets/hardware/IMU_AUTO_DMA_IP_SPEC.md)**: Hardware IMU SPI Master & Auto-DMA IP core with 64-bit nanosecond timestamp latching.
3. **[`tier3_targets/hardware/TANG9K_PINOUT.md`](tier3_targets/hardware/TANG9K_PINOUT.md)**: Tang Nano 9K FPGA header map and motor assignments.
4. **[`tier3_targets/hardware/PRIMER20K_PINOUT.md`](tier3_targets/hardware/PRIMER20K_PINOUT.md)**: Tang Primer 20K FPGA header map.
5. **[`tier3_targets/hardware/HARDWARE_DEBUGGING_PICO2W_ESP32P4.md`](tier3_targets/hardware/HARDWARE_DEBUGGING_PICO2W_ESP32P4.md)**: Bare-metal debugging and logic analyzer workflows.

---

## Verification & Quality Gates ([`verification/`](verification/))

1. **[`verification/SASHIKO_ADVERSARIAL_REVIEW_AND_CPPUTEST_GUIDE.md`](verification/SASHIKO_ADVERSARIAL_REVIEW_AND_CPPUTEST_GUIDE.md)**: 5-Stage Sashiko adversarial review and CppUTest contracts.
2. **[`verification/AXIS_TESTING_FRAMEWORK.md`](verification/AXIS_TESTING_FRAMEWORK.md)**: AXI-Stream VIP simulation and Cocotb testbenches.
3. **[`verification/E2E_VERIFICATION.md`](verification/E2E_VERIFICATION.md)**: End-to-end verification methodology.
4. **[`verification/ASP_VALIDATION_MATRIX.md`](verification/ASP_VALIDATION_MATRIX.md)**: Timing closure and protocol compliance matrix.
5. **[`verification/ASP_REQUIREMENTS.md`](verification/ASP_REQUIREMENTS.md)**: Quality gates and definition of done.
6. **[`verification/ASP_RELEASE_PROCESS.md`](verification/ASP_RELEASE_PROCESS.md)**: Release tagging checklist.
7. **[`verification/BUILD.md`](verification/BUILD.md)**: Detailed multi-target build manual.
8. **[`verification/ENGINEERING_LOG.md`](verification/ENGINEERING_LOG.md)**: Chronological commit history and bitstream revisions.
9. **[`verification/evidence/`](verification/evidence/)**: Empirical test logs, JSON summaries, and waveforms.

---

## Authoritative System Specification (SSOT) & Standard Template

* **[`DESIGN_SPECIFICATION.md`](DESIGN_SPECIFICATION.md)**: Root Single Source of Truth containing all audited `[SPEC-*]` requirements.
* **[`SPEC_TEMPLATE.md`](SPEC_TEMPLATE.md)**: Standard template for authoring new specifications for drivers, targets, and applications.
