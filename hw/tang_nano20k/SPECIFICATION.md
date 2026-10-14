# Cubie A5E + Sipeed Tang Nano 20K — FPGA Test Target Specification

**Role.** The Tang Nano 20K is the small FPGA used to test the AbstractX FPGA path
with the Radxa Cubie A5E on the bench. The **QMTech Zynq-7020 is the full
AbstractX platform** ([`hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md`](../zynq7000/qmtech_zynq7020/SPECIFICATION.md));
this target runs the same `asp_top` fabric over a serial link instead of AXI.

Section 1 is the A5E ↔ FPGA link and does not depend on the FPGA board (it also
applies to the Trenz CYC1000 in
[`docs/tier3_targets/hardware/CUBIE_A5E_CYC1000_INTEGRATION.md`](../../docs/tier3_targets/hardware/CUBIE_A5E_CYC1000_INTEGRATION.md)
and the cubie-a5e Tang Primer 25K carrier). Section 2 is the Tang Nano 20K board.

Each requirement lists how it is verified (a skill in `.agents/skills/`) and its
status. **Open** items need a fact that is not in the repository yet; they are
not guessed.

---

## 1. A5E ↔ FPGA Link (board-independent)

### `[SPEC-A5E-LINK-01]` Physical Link
* **Requirement**: 5 signals plus ground between the Cubie A5E 40-pin header and the
  FPGA, 3.3 V LVCMOS on both sides, FPGA powered from its own USB:

  | Signal | A5E header pin (per CYC1000 guide) | Direction |
  | :--- | :--- | :--- |
  | `SPI_SCLK` | 23 | A5E → FPGA |
  | `SPI_CS_N` | 24 | A5E → FPGA |
  | `SPI_IO0` | 19 | bidirectional (Dual-SPI) |
  | `SPI_IO1` | 21 | bidirectional (Dual-SPI) |
  | `INT_REQ` | 22 | FPGA → A5E (doorbell, GPIO interrupt) |
  | `GND` | 6, 9 or 14 | — |
* **Open**: the A5E pin numbers come from the CYC1000 guide, not from a Radxa
  pinout or the A5E device tree; confirm which SPI controller (SPI0 or SPI1) and
  which GPIO bank drive these pins before wiring.
* **Verified by**: `board-bringup` (link test below). **Status**: specified, not built.

### `[SPEC-A5E-LINK-02]` Protocol
* **Requirement**: the link carries 64-byte TLPs (`[SPEC-TLP-01]`) through
  `rtl/spi/asp_spi_frontend.sv` in Dual-SPI mode. It is an external serial
  profile, so the CRC32 footer is computed and checked (`[SPEC-TLP-01]`).
* **Requirement**: the RTL above the board top (`asp_top` and everything below it)
  is identical on every board; a board top only maps pins and clocks.
* **Verified by**: `cocotb-regression` (`test_asp_tlp_64b_cocotb` on `asp_top`).
  **Status**: implemented in RTL; simulation passes.

### `[SPEC-A5E-LINK-03]` Linux Host Side
* **Requirement**: A5E Linux reaches the FPGA through `spidev` on the header SPI
  controller and receives `INT_REQ` as a GPIO edge interrupt (libgpiod); no
  blocking poll of the FPGA in the AbstractX loop (`[SPEC-ARCH-01]`).
* **Requirement**: bring-up starts at SCLK ≤ 10 MHz. The highest SCLK that
  passes the link test with jumper wires is measured and recorded as evidence
  before it is used (target up to 50 MHz).
* **Verified by**: `board-bringup`. **Status**: specified, not built.

---

## 2. Tang Nano 20K Board

### `[SPEC-TN20K-01]` Device and Toolchain
* **Device**: Gowin GW2AR-LV18QN88C8/I7 (GW2A-18C family; GW2A-18 die plus
  64 Mbit SDRAM), 27 MHz on-board oscillator.
* **Toolchain** (open source, `~/.tools/oss-cad-suite`): `yosys synth_gowin`,
  `nextpnr-himbaechel --device GW2AR-LV18QN88C8/I7 --vopt family=GW2A-18C`
  (chip database `chipdb-GW2A-18C.bin` is installed), `gowin_pack`,
  `openFPGALoader -b tangnano20k`.
* **Requirement**: built by a `tang_nano20k` target in `hw/Makefile`, like the
  existing `tang9k` and `primer20k` targets.
* **Verified by**: `fpga-build` (bitstream produced). **Status**: not built.

### `[SPEC-TN20K-02]` Board Top and Constraints
* **Requirement**: `hw/tang_nano20k/top_tang_nano20k.sv` instantiates `asp_top`
  unchanged and maps the link (`[SPEC-A5E-LINK-01]`), the IMU, the DShot outputs
  and the on-board WS2812 LED; `hw/tang_nano20k/tang_nano20k.cst` assigns pins.
  Every pin in the `.cst` cites the Sipeed schematic pin it comes from.
* **Requirement**: `rst_n` passes through a reset synchronizer (async assert,
  sync de-assert) before reaching `asp_top` (TODO.md RTL item).
* **Open**: FPGA header pin numbers for the link, IMU and DShot lines.
* **Verified by**: `fpga-build`, `rtl-lint`. **Status**: not written.

### `[SPEC-TN20K-03]` Feature Set and Resource Budget
* **Included**: `asp_spi_frontend`, `asp_router`, `asp_sys_regs`,
  `asp_wishbone_master`, `asp_imu_auto_dma`, `asp_dshot_core` ×4,
  `asp_neopixel_core`, `asp_pwm_decoder` (all of `asp_top`).
* **Excluded** (Zynq only): `asp_axi_lite_bridge`, `asp_axi_dma`, `asp_ila_trace`.
* **Requirement**: the design fits with at least 20% LUT and FF headroom and
  meets timing at its system clock (27 MHz, or a PLL clock stated in the top),
  per the nextpnr report.
* **Verified by**: `fpga-fit`. Proxy evidence: the existing Tang Primer 20K
  build (`make -f hw/Makefile primer20k`) uses the same GW2A-18 die.
* **Measured 2026-10-14 (GW2A-18, Primer 20K top, nextpnr-himbaechel)**:
  LUT4 4 505 / 20 736 (21%), DFF 4 669 / 15 552 (30%), ALU 1 068 (6%),
  IOB 29 (7%), routed Fmax 126.7 MHz. Fits with headroom and clears the
  Nano 20K's 27 MHz clock. (Before the incremental-CRC change,
  `[SPEC-TLP-CRC-01]`: LUT4 50%, Fmax 13.7 MHz, which would fail at 27 MHz.)
  **Status**: proxy met; Nano 20K's own build not yet run.

### `[SPEC-TN20K-04]` Bench Bring-up Test
* **Requirement**: from A5E Linux, (1) read the `asp_sys_regs` ID register over the
  link and get `0xABF10164`, (2) write and read back the scratch register,
  (3) see `INT_REQ` fire on IMU DRDY and read an IMU auto-DMA TLP, (4) run 10 000
  TLP round trips with zero CRC errors at the chosen SCLK.
* **Evidence**: the test log is stored under `docs/verification/evidence/` with
  the bitstream hash, SCLK and A5E kernel version.
* **Verified by**: `board-bringup`. **Status**: not run.
