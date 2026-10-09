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

The default ALINX AC7010C and AC7020C Buildroot images load U-Boot SPL without
an FPGA bitstream, so their base DTBs are strictly PS-only. PL addresses, IRQs,
and reserved-memory regions belong in overlays coupled to a matching loaded
bitstream; a base DTB must never advertise unconfigured PL hardware.

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

The UIO and trace overlays are valid only when the selected bitstream provides
the matching CSR address, IRQ, memory profile, TLP size, and trace ABI. An
overlay cannot create hardware absent from the FPGA bitstream. Every board's
generated `/boot/config.txt` selects only the PS bus overlay. Linux-side test
automation loads a bitstream first and then applies its matching PL overlay.

## AbstractX UIO/packet ABI

The default BRAM profile's UIO node describes:

```text
CSR base:       0x40000000
CSR size:       0x00010000
IRQ_F2P[0]:     Linux IRQ 29
TLP size:       64 bytes
Ingress FIFO:   128 slots x 64 bytes (8 KiB BRAM)
Egress FIFO:    128 slots x 64 bytes (8 KiB BRAM)
Integrity:      trusted internal transport, footer reserved as zero
Trace channel:  0x04
```

The generic UIO device exposes only map 0 for the CSR/packet-port window. The
default overlay reserves no DDR and exposes no DMA-ring map.

The DMA enable/stop operation is:

```text
CSR + 0x00, bit 0 = 1: enable RX/TX DMA
CSR + 0x00, bit 0 = 0: stop RX/TX DMA
```

Wishbone control and packet movement remain on the AXI-Lite/TLP path.

Larger bitstreams may define a separate DDR overlay using one explicit mode:

- **HP0 non-coherent:** a noncached `no-map` reserved region or a kernel DMA
	allocation with `dma_sync_*_for_cpu/device()` at ownership transitions.
- **ACP coherent:** a kernel-managed cacheable DMA buffer and PL transactions
	carrying the correct coherent/shareable ACP attributes.

DDR overlays MUST use a distinct compatibility/profile identifier. A cached
`/dev/mem` mapping on HP0 is forbidden, and coherency never replaces atomic
head/tail ownership barriers.

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
find /sys/firmware/devicetree/base -name '*abstractx*'
```

The expected BRAM-profile result is a UIO device exposing only the CSR/packet
window. Linux UIO support is provided by `CONFIG_UIO`,
`CONFIG_UIO_PDRV_GENIRQ`, and the `generic-uio` overlay binding.

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
