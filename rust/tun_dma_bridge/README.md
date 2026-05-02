<!-- Copyright (C) 2026 Tim Michals -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# tun_dma_bridge (Rust userspace scaffold)

This crate is the Rust userspace path for AbstractX TUN + DMA bridging on Zynq.

## Current status

- CLI and module structure implemented
- TUN open/read/write path implemented
- DMA char-device open/read/write path implemented
- ASP framing/parsing + optional CRC check path implemented

## Build

From repo root:

`cargo build --manifest-path rust/tun_dma_bridge/Cargo.toml`

## Cross-compiling for Zynq-7020

Zynq-7020 is **Cortex-A9 / ARMv7-A**, not classic ARM9.

For a Linux userspace binary, the likely Rust target is:

- `armv7-unknown-linux-gnueabihf`

This must match the ABI used by your Buildroot toolchain/rootfs.

### Important ABI alignment checks

Make sure Rust output matches Buildroot choices for:

- ARM architecture level (`armv7-a`)
- hard-float vs soft-float ABI
- libc family (`glibc` vs `musl`)
- linker/sysroot from the same Buildroot toolchain

If these do not match, the binary may compile but fail to run correctly on target.

### Recommended workflow

1. Use Buildroot to establish the target toolchain and rootfs ABI first.
2. Build the Rust binary against the same ABI.
3. Once stable, package the Rust bridge through the Buildroot external tree for reproducibility.

## Buildroot integration plan

Long-term, prefer packaging this crate in your Buildroot external tree rather than relying on ad-hoc host-side cross builds.

### Packaging checklist

- [ ] Confirm Buildroot target libc (`glibc`/`musl`)
- [ ] Confirm float ABI matches target (`gnueabihf` is typical for Zynq Linux)
- [ ] Set Rust target triple to match toolchain ABI
- [ ] Provide linker/sysroot configuration from Buildroot toolchain
- [ ] Install resulting binary into rootfs package output
- [ ] Add runtime config/service wrapper if desired
- [ ] Record exact target triple in BSP docs

## Run (example)

`sudo cargo run --manifest-path rust/tun_dma_bridge/Cargo.toml -- --tun-name tun0 --tun-cidr 10.0.0.1/24 --dma-h2c /dev/xdma0_h2c_0 --dma-c2h /dev/xdma0_c2h_0 --crc-mode off`

## Notes

- This is userspace; long-term kernel path can reuse framing and policy logic.
- For DMA bring-up, CRC off is often practical; use CRC on for serial-style links.
