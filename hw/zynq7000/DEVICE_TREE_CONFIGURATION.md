# AbstractX Zynq-7000 Device-Tree Configuration

This document defines the shared Linux/U-Boot device-tree contract for:

- QMTECH XC7Z020
- ALINX AC7010C
- ALINX AC7020C

The design follows the Cubie A5E pattern: one board-specific base DTB plus
human-readable boot configuration and boot-time U-Boot overlays.

## Base DTB versus overlay

The base DTB owns board facts:

- Zynq device and DDR size
- PS clocks and pinctrl
- UART, Ethernet, USB, SD, and QSPI
- Board-specific PL address and IRQ wiring
- Reserved-memory placement

The shared overlays own the AbstractX PL ABI:

```text
abstractx-uio.dtbo
abstractx-trace.dtbo
```

A shared overlay is valid only when the selected bitstream provides the same
CSR address, IRQ, DMA ring ABI, TLP size, and trace channel on every board.
An overlay cannot create hardware that is absent from the FPGA bitstream.

## AbstractX UIO/DMA ABI

The common UIO node describes:

```text
CSR base:       0x40000000
CSR size:       0x00010000
IRQ_F2P[0]:     Linux IRQ 29
TLP size:       64 bytes
DMA bucket:     256 bytes (four TLP records)
Reserved DDR:   0x1e000000 - 0x1fffffff (32 MiB)
Trace channel:  0x04
```

The DMA enable/stop operation is:

```text
CSR + 0x00, bit 0 = 1: enable RX/TX DMA
CSR + 0x00, bit 0 = 0: stop RX/TX DMA
```

Wishbone control remains on the AXI-Lite/TLP path. Normal traffic and trace
may use separate logical DMA rings while sharing the initial HP0 engine.

The Zynq HP path is non-coherent. Userspace must use the documented cache
maintenance or noncached/reserved-memory policy before changing ring ownership.

## U-Boot boot flow

Buildroot installs `boot.scr`, `config.txt`, `system.dtb`, and the `.dtbo`
files in the FAT boot partition. U-Boot performs:

1. Load `config.txt`.
2. Import `dtoverlay` and optional `cmdline` values.
3. Load `system.dtb`.
4. Run `fdt resize 0x10000`.
5. Apply overlays in listed order with `fdt apply`.
6. Load `uImage` and boot Linux with the merged FDT.

The base DTB and overlays must be compiled with symbols (`-@`). The binary
U-Boot environment remains generated and CRC-protected; users edit
`config.txt`, not `uboot.env`.

Example:

```text
dtoverlay=abstractx-uio abstractx-trace
cmdline=loglevel=7
```

## Linux verification

After boot:

```text
ls -l /dev/uio*
cat /sys/class/uio/uio0/maps/map0/name
find /sys/firmware/devicetree/base -name '*abstractx*' -o -name '*dma*'
```

The expected result is a UIO device exposing the CSR window and an active
reserved-memory node for the DMA buckets. Linux UIO support is provided by
`CONFIG_UIO`, `CONFIG_UIO_PDRV_GENIRQ`, and the `generic-uio` overlay binding.

## Buildroot outputs

The shared external tree compiles the overlays, generates `boot.scr`, stages
`config.txt`, copies the selected base DTB as `system.dtb`, and packages all
artifacts into `boot.vfat`. The board defconfig selects the base Linux DTB:

```text
xilinx/zynq-qmtech-xc720
xilinx/zynq-ac7010c
xilinx/zynq-ac7020c
```
