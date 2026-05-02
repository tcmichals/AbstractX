<!-- Copyright (C) 2026 Tim Michals -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# QMTECH Zynq-7020 Hardware Scaffold

This directory is the integration start-point for QMTECH Zynq-7020 work in this repo.

## Current board direction

This board is the current system-integration focus for AbstractX.

Primary software path:

- Python userspace TUN + DMA for bring-up
- Rust userspace TUN + DMA for hardening

Primary platform path:

- Buildroot baseline first
- bleeding-edge kernel/toolchain later, after basic functionality is proven

## Known-good Pico JTAG/XVC project link

From the Hackaday + Adam Taylor flow, the referenced project is:

- `https://github.com/kholia/xvc-pico/`

Related tutorial reference:

- `https://www.adiuvoengineering.com/post/microzed-chronicles-jtag-using-a-raspberry-pi-pico`

## What `xvc-pico` gives you

- RP2040/Pico firmware implementing XVC-compatible JTAG bridge
- Host daemon (`xvcd-pico`) for XVC server endpoint
- Vivado Hardware Manager can connect via **Add Xilinx Virtual Cable (XVC)**

## Practical workflow

1. Flash Pico with `xvc-pico` firmware (`.uf2` available in that repo).
2. Wire Pico JTAG to Zynq JTAG (TCK/TMS/TDI/TDO/GND, with correct voltage domain).
3. Run `xvcd-pico` on host.
4. In Vivado Hardware Manager, add XVC target (host IP + port).

## Notes

- Pico is 3.3V IO; use level shifting if target JTAG voltage is lower (e.g. 1.8V/2.5V).
- For board/BSP external-tree baseline, keep using your existing:
	- `https://github.com/tcmichals/QMTECH`
	- `zynq_qmtech_xc720/`

## USB/ULPI bring-up reminder

If the board uses an external ULPI USB PHY, Linux/device-tree bring-up may need:

- `compatible = "usb-nop-xceiv";`
- a valid `reset-gpios` entry for the PHY or transceiver reset path
- kernel support for `CONFIG_NOP_USB_XCEIV`

That note is easy to forget and annoying to rediscover later—classic bring-up gremlin behavior.

## Buildroot output directory convention

Use a dedicated out-of-tree build directory named:

- `bld`

This keeps generated artifacts separated from source and matches your current
QMTECH workflow convention.

## Pico / XVC build output convention

If you keep local `xvc-pico` firmware or host-daemon build outputs under this
board directory, use:

- `pico_bld`

That directory is treated as generated output and should not be committed.
