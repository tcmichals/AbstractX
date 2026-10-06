# AbstractX ALINX AC7020C target

Standalone AbstractX board target for the ALINX AC7020C (`XC7Z020-2CLG400I`). It is intentionally independent from the separate `alnx/project/AC7020C` development project. See `SPECIFICATION.md` for requirements and verified hardware facts.

```text
AbstractX/hw/zynq7000/
├── buildroot_external/       shared Buildroot external tree
├── qmtech_zynq7020/          QMTECH board target
└── alinx_ac7020c/            this board target
    ├── vivado/               reproducible XPR/XSA generation
    ├── tools/                setup and PS-init helpers
    ├── bld/                  Buildroot output (ignored)
    └── output/               Vivado release artifacts (ignored)
```

## Vivado build

From the AbstractX repository root:

```bash
make -C hw/zynq7000/alinx_ac7020c xpr
make -C hw/zynq7000/alinx_ac7020c xsa
```

Expected outputs:

```text
hw/zynq7000/alinx_ac7020c/build/ac7020c_base/ac7020c_base.xpr
hw/zynq7000/alinx_ac7020c/output/ac7020c_base.bit
hw/zynq7000/alinx_ac7020c/output/ac7020c_base.xsa
```

## U-Boot DDR/PS initialization

The XSA contains generated PS initialization. Install the exact GPL source into the sibling U-Boot tree with:

```bash
./hw/zynq7000/alinx_ac7020c/tools/install_ps7_init.sh
```

Destination:

```text
u-boot-zynq/board/xilinx/zynq/zynq-ac7020c/ps7_init_gpl.c
```

For an FSBL-first boot, Vitis/PetaLinux builds FSBL from `ps7_init.c` in the XSA and FSBL initializes DDR before U-Boot. For a U-Boot-SPL-first boot, enable SPL and compile the installed `ps7_init_gpl.c`.

## Buildroot

Run `tools/setup_workspace.sh` to locate or clone Buildroot, `linux-cubie`, and `u-boot-zynq`, create `bld/local.mk` source overrides, and initialize the ALINX defconfig. Then build with:

```bash
make -C hw/zynq7000/alinx_ac7020c/bld -j$(nproc)
```

To use existing local checkouts instead of the defaults, pass explicit paths to
the shared setup script:

```text
./hw/zynq7000/tools/setup_workspace.sh alinx \
    --buildroot /path/to/buildroot \
    --linux /path/to/linux-cubie \
    --uboot /path/to/u-boot-zynq
```

The kernel uses `xilinx/zynq-ac7020c.dtb`; U-Boot uses `ac7020c_defconfig`.

## Device-tree overlays and UIO

The Buildroot image follows the Cubie A5E boot configuration model. The board
DTB describes board-specific hardware; `/boot/config.txt` selects common
AbstractX overlays before Linux starts:

```text
dtoverlay=abstractx-uio abstractx-trace
```

U-Boot loads `system.dtb`, imports `config.txt`, resizes the FDT, applies the
selected `.dtbo` files, and boots Linux with the merged tree. The common UIO
overlay describes the AbstractX CSR window at `0x40000000`, IRQ_F2P[0] (Linux
IRQ 29), and the reserved 32 MiB DMA region at `0x1e000000`.

The DMA enable/stop control is a UIO register operation:

```text
CSR + 0x00, bit 0 = 1: enable RX/TX DMA
CSR + 0x00, bit 0 = 0: stop RX/TX DMA
```

The overlay also records the 256-byte DMA bucket and 64-byte TLP sizes for
userspace. Verify the merged configuration after boot with:

```text
ls -l /dev/uio*
cat /sys/class/uio/uio0/maps/map0/name
find /sys/firmware/devicetree/base -name '*abstractx*' -o -name '*dma*'
```

## FPGA bitstream loading

The preferred production path is boot-time loading: Vivado creates the PL
bitstream, and AMD `bootgen`/Vitis or PetaLinux packages it with the FSBL and
U-Boot into `BOOT.BIN`:

```text
BootROM -> FSBL -> PL bitstream -> U-Boot -> Linux
```

The Linux 7.1 configuration also contains the Zynq FPGA Manager framework:
`CONFIG_FPGA_MGR_ZYNQ_FPGA`, `CONFIG_FPGA_BRIDGE`, and `CONFIG_FPGA_REGION`.
For development, install a compatible `.bin` bitstream under `/lib/firmware`
and request it through:

```text
echo ac7020c_base.bit.bin > /sys/class/fpga_manager/fpga0/firmware
cat /sys/class/fpga_manager/fpga0/state
```

AMD/PetaLinux images may provide `fpgautil`, which is a userspace frontend for
FPGA Manager operations. Do not reload the PL while AXI peripherals or DMA
clients are active; stop them first or provide an FPGA Region/bridge isolation
scheme.

## USB3320C

The ULPI electrical interface is 1.8 V in the Vivado PS bank configuration. Linux describes the external PHY with `usb-nop-xceiv`, active-low reset on MIO46, and `dr_mode = "host"`. Required kernel options include `CONFIG_NOP_USB_XCEIV`, `CONFIG_USB_CHIPIDEA`, and `CONFIG_USB_CHIPIDEA_HOST`.

## LEDs

Both LEDs are active low:

- PS LED: MIO0, Linux Zynq GPIO line 0.
- PL LED: R19, AXI GPIO line 0 at `0x41200000`.

Use `gpioinfo` to discover runtime GPIO chip names. The `gpio-leds` nodes are exposed under `/sys/class/leds/`.
