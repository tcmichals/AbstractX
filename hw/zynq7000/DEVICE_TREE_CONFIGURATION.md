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

The shared overlays describe PS bus access and, when supported, the AbstractX
PL ABI:

```text
abstractx-buses.dtbo
abstractx-uio.dtbo
abstractx-trace.dtbo
```

`abstractx-buses.dtbo` enables PS I²C0 on MIO14/MIO15 and PS SPI1 on
MIO10–MIO13, exposing `/dev/i2c-0` and `/dev/spidev1.0`. It is selected for
all three board configurations. Its ICM-42688-P child uses the truthful
`invensense,icm42688` compatible and binds to spidev in the shared kernel; the
in-kernel IIO SPI driver is disabled for that endpoint.

The UIO and trace overlays are valid only when the selected bitstream provides the same
CSR address, IRQ, DMA ring ABI, TLP size, and trace channel on every board.
An overlay cannot create hardware that is absent from the FPGA bitstream.
Each board has its own generated `/boot/config.txt`. All select the bus overlay.
QMTECH also selects the UIO/trace overlays by default. ALINX AC7010C and AC7020C
omit those two because their baseline bitstreams do not contain the TLP/DMA
fabric. Add them to an ALINX config only when deploying a matching fabric
bitstream.

## AbstractX UIO/DMA ABI

The common UIO node describes:

```text
CSR base:       0x40000000
CSR size:       0x00010000
IRQ_F2P[0]:     Linux IRQ 29
TLP size:       64 bytes
DMA ring slot:  64 bytes (one TLP record)
Ring capacity:  256 slots (16 KiB)
Reserved DDR:   0x1e000000 - 0x1fffffff (32 MiB)
Trace channel:  0x04
```

The generic UIO device exposes map 0 for the CSR window and map 1 for the
reserved DMA window. UIO maps the physical ring noncached; userspace MUST use
that map rather than a cached `/dev/mem` mapping.

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
dtoverlay=abstractx-buses
```

QMTECH additionally uses:

```text
dtoverlay=abstractx-uio abstractx-trace
cmdline=loglevel=7
```

## Linux verification

After boot:

```text
ls -l /dev/spidev1.0 /dev/i2c-0
python3 -c 'import gpiod, spidev; print("IMU userspace modules available")'
ls -l /dev/uio*
cat /sys/class/uio/uio0/maps/map0/name
cat /sys/class/uio/uio0/maps/map1/name
find /sys/firmware/devicetree/base -name '*abstractx*' -o -name '*dma*'
```

The expected result is a UIO device exposing the CSR window and an active
reserved-memory map for 64-byte DMA ring slots. Linux UIO support is provided by
`CONFIG_UIO`, `CONFIG_UIO_PDRV_GENIRQ`, and the `generic-uio` overlay binding.

## IMU backend validation proof of concept

The shared hardware test suite is specified in
[`testApps/imu_backend_validation_poc/SPECIFICATION.md`](testApps/imu_backend_validation_poc/SPECIFICATION.md).
It compares Linux GPIO-event → spidev acquisition with PL DRDY → SPI → DMA
acquisition using one ICM-42688-P test flow. The PS DRDY GPIO line and safe
PS/PL SPI routing must be verified on the actual board before results are
considered comparable.

## Buildroot outputs

The shared external tree compiles the overlays, generates `boot.scr`, stages
`config.txt`, copies the selected base DTB as `system.dtb`, and packages all
artifacts into `boot.vfat`. The board defconfig selects the base Linux DTB:

```text
xilinx/zynq-qmtech-xc720
xilinx/zynq-ac7010c
xilinx/zynq-ac7020c
```
