# AbstractX Engineering Log & Architecture History

This document maintains a chronological record of major architectural milestones, protocol revisions, FPGA hardware builds, git commits, and archive branches.

---

## Active Branches & Archive Pointers

- **`main`**: Canonical production branch. Contains 4-channel DShot/PWM, WS2812B NeoPixel RGB core, IMU SPI Auto-DMA, PCIe TLP BAR space (`0x40000000..0x40000600`), 64-bit Master Timestamping, Linux LED_CTRL, and 4 hardware debug pins.
- **`archive/v1-legacy-8ch`** (Commit [`e45cf30`](https://github.com/tcmichals/AbstractX/commit/e45cf30)): Legacy archive branch preserving early 8-channel motor scaffolding and initial bitstream builds.

---

## Chronological Engineering Milestones

### Milestone 5: 64-Bit Timestamp, PCIe Identity, 4-Ch DShot & Python VIP
- **Date**: 2026-08-11
- **Commits**: [`7424814`](https://github.com/tcmichals/AbstractX/commit/7424814), [`86a08f2`](https://github.com/tcmichals/AbstractX/commit/86a08f2), [`ba7b600`](https://github.com/tcmichals/AbstractX/commit/ba7b600), [`421dc01`](https://github.com/tcmichals/AbstractX/commit/421dc01)
- **Key Changes**:
  - Configured 4 motor channels (`o_motor_pins[3:0]`) for Quadcopters (DShot + 1-wire ESC Serial Passthrough `serial_4way.c`), saving logic utilization.
  - Implemented standard PCIe Device ID/Rev (`0xABF10164`), Vendor ID (`0x19981ACC`), and atomic 64-bit Master Hardware Timestamp Read Registers (`0x40000010` / `0x40000014`) for sub-microsecond clock sync & EKF3 sensor fusion.
  - Added Linux-controllable Onboard LEDs 2..6 (`0x4000000C`) and 1 Hz FPGA Hardware Heartbeat (LED 1).
  - Added 4 dedicated Hardware Logic Analyzer Debug Pins (`o_debug_pins[3:0]`).
  - Created executable Python verification suite `tools/test_asp_pcie.py` (`--mode rainbow`, `--mode pwm_sweep`, `--mode led_chase`, `--mode cli`).
- **Bitstream Verification**:
  - Tang Nano 9K (`GW1NR-9`): **76.92 MHz** (PASS)
  - Tang Primer 20K (`GW2A-18`): **162.07 MHz** (PASS)

### Milestone 4: DShot & NeoPixel IP Cores
- **Date**: 2026-08-11
- **Commit**: [`69e1a70`](https://github.com/tcmichals/AbstractX/commit/69e1a70)
- **Key Changes**: Integrated DShot150/300/600 hardware core (`asp_dshot_core.sv`) and WS2812B single-wire NRZ driver (`asp_neopixel_core.sv`) onto Wishbone switch fabric.

### Milestone 3: Portable Flight Stack & Decoupled PCIe Architecture
- **Date**: 2026-08-11
- **Commit**: [`06ac0aa`](https://github.com/tcmichals/AbstractX/commit/06ac0aa)
- **Key Changes**: Formulated PCIe-like TLP Register BAR API (`pcie_reg_api.h`) and `AP_HAL_AbstractX` architecture specs for ArduPilot / Betaflight / iNav.

### Milestone 2: Zynq & Early Bitstreams
- **Date**: 2026-08-10
- **Commits**: [`e45cf30`](https://github.com/tcmichals/AbstractX/commit/e45cf30), [`3008806`](https://github.com/tcmichals/AbstractX/commit/3008806)
- **Key Changes**: Initial Gowin synthesis Makefile and Zynq TUN/DMA bridge documentation. Preserved on branch `archive/v1-legacy-8ch`.

---

## Hardware Pinout Reference Summary

- **Tang Nano 9K Pinout Spec**: [`docs/TANG9K_PINOUT.md`](TANG9K_PINOUT.md)
- **Tang Primer 20K Pinout Spec**: [`docs/PRIMER20K_PINOUT.md`](PRIMER20K_PINOUT.md)
- **Master Documentation Index**: [`docs/README.md`](README.md)
