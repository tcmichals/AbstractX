# AbstractX Zynq-7000 platform

Shared platform integration for Zynq-7000 boards. This directory is a peer of `hw/tang9k` and `hw/primer20k`.

```text
hw/zynq7000/
├── README.md
├── tools/setup_workspace.sh
├── buildroot_external/
│   ├── configs/abstractx_qmtech_zynq7020_defconfig
│   └── configs/abstractx_alinx_ac7020c_defconfig
├── qmtech_zynq7020/
└── alinx_ac7020c/
```

## Workspace setup

The setup tool locates or clones shared Buildroot, `linux-cubie`, and `u-boot-zynq` repositories, writes board-local `bld/local.mk` source overrides, and initializes one or both output trees.

```bash
./hw/zynq7000/tools/setup_workspace.sh all
./hw/zynq7000/tools/setup_workspace.sh qmtech
./hw/zynq7000/tools/setup_workspace.sh alinx
```

Board wrappers are also available:

```bash
./hw/zynq7000/qmtech_zynq7020/tools/setup_workspace.sh
./hw/zynq7000/alinx_ac7020c/tools/setup_workspace.sh
```

## Build images

```bash
make -C hw/zynq7000/qmtech_zynq7020/bld -j$(nproc)
make -C hw/zynq7000/alinx_ac7020c/bld -j$(nproc)
```

Both configurations use the same external tree but select independent Linux DTBs and U-Boot board targets.

## ALINX Vivado/XSA

The ALINX target is standalone. Generate its hardware platform with:

```bash
make -C hw/zynq7000/alinx_ac7020c xsa
make -C hw/zynq7000/alinx_ac7020c ps-init
```

The first command generates XPR, bitstream and XSA. The second extracts the XSA's exact `ps7_init_gpl.c` into the sibling `u-boot-zynq` board directory.

Generated Vivado and Buildroot outputs are ignored; Tcl, XDC, specifications, READMEs and Buildroot defconfigs are source-controlled.
