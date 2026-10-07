AbstractX Zynq-7000 Buildroot external tree
==========================================

This board directory supports the QMTECH XC7Z020, ALINX AC7010C, and ALINX
AC7020C configurations. Use hw/zynq7000/README.md for workspace setup and
Buildroot commands. Each board uses a separate Buildroot output directory.

The post-image step creates sdcard.img from the generated boot.vfat and
rootfs.ext4 images. boot.bin is supplied by the selected U-Boot SPL build; a
successful Buildroot image build does not prove that its FSBL, PS
initialization, or FPGA bitstream matches the physical board. Follow the
board-specific hardware instructions and verify the required boot chain.

The post-image step selects config.txt from the configured board DTB. QMTECH
enables the shared AbstractX UIO/trace overlays by default. ALINX AC7010C and
AC7020C default to no AbstractX overlays because their baseline bitstreams do
not include the TLP/DMA fabric. Enable those overlays only with a bitstream
implementing the shared ABI; see hw/zynq7000/DEVICE_TREE_CONFIGURATION.md.
