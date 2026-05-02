<!-- Copyright (C) 2026 Tim Michals -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# AbstractX TUN Framework (Python + Rust path)

## Canonical Python bridge

Use `python/asp_tun_bridge.py` as the unified userspace bridge.

It supports:

- SPI backend (`--backend spi`)
- DMA backend (`--backend dma`)
- auto selection (`--backend auto`)

### Quick start (SPI)

`python3 python/asp_tun_bridge.py --backend spi --crc-mode on`

### Quick start (DMA)

`python3 python/asp_tun_bridge.py --backend dma --crc-mode off`

## CRC policy

- SPI/serial links: `--crc-mode on` (or `auto`)
- DMA in-box path: `--crc-mode off` (or `auto`)

CRC field remains present in frames for compatibility.

## Short rationale

- **CRC off for DMA**: DMA is an in-box, trusted transport, so CRC checking is usually unnecessary overhead there.
- **CRC on for SPI**: SPI is a serialized external link and benefits much more from error detection.
- **Python first**: fastest path for bring-up, debugging, and protocol iteration.
- **Rust next**: better long-running userspace path for TUN + DMA once behavior is proven.
- **Kernel later**: only if measurements show userspace cannot meet latency/jitter goals.

## Key options

- `--backend auto|spi|dma`
- `--dma-h2c /dev/xdma0_h2c_0`
- `--dma-c2h /dev/xdma0_c2h_0`
- `--tun-name tun0`
- `--tun-cidr 10.0.0.1/24`
- `--loop-sleep-ms 5`

## Rust migration path (TUN + DMA)

A Rust userspace scaffold now exists at:

- `rust/tun_dma_bridge/`

This is the long-term low-jitter path while keeping Python for fast bring-up.

