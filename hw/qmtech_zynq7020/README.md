<!-- Copyright (C) 2026 Tim Michals -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# QMTECH Zynq-7020 Hardware Scaffold

This directory is the integration and board bring-up workspace for the **QMTECH Zynq-7020 (XC7Z020)** in AbstractX.

It follows the exact same zero-patch, `local.mk` override architecture proven in `cubie-a5e`.

---

## 🚀 Multi-PC Workspace Setup

Run the automated workspace setup script from the AbstractX project root or this directory:

```bash
./hw/qmtech_zynq7020/tools/setup_workspace.sh
```

### What `setup_workspace.sh` does automatically:
1. Locates or clones:
   * **Buildroot** (`https://gitlab.com/buildroot.org/buildroot.git`)
   * **Linux kernel** (`git@github.com:tcmichals/linux-cubie.git`, branch `cubie-linux-7.1`)
   * **U-Boot** (`git@github.com:tcmichals/u-boot-zynq.git`, branch `main`)
   * **QMTECH external tree** (`git@github.com:tcmichals/QMTECH.git`)
2. Configures the out-of-tree build directory: `hw/qmtech_zynq7020/bld/`.
3. Creates `hw/qmtech_zynq7020/bld/local.mk` pointing to your local `linux-cubie` and `u-boot-zynq` repositories.
4. Initializes `hw/qmtech_zynq7020/bld/.config` with `zynq_qmtech_xc720_defconfig`.

---

## 🛠️ Build Commands

All builds execute out-of-tree in `hw/qmtech_zynq7020/bld/`:

| Task | Command | Description |
|---|---|---|
| **Full Image Build** | `make -C hw/qmtech_zynq7020/bld -j$(nproc)` | Builds toolchain, U-Boot, Linux, and generates `sdcard.img` |
| **Fast Kernel Rebuild** | `make -C hw/qmtech_zynq7020/bld linux-rebuild` | Incremental 5-second rebuild of kernel using `local.mk` |
| **Fast U-Boot Rebuild** | `make -C hw/qmtech_zynq7020/bld uboot-rebuild` | Incremental rebuild of U-Boot using `local.mk` |
| **Menuconfig** | `make -C hw/qmtech_zynq7020/bld menuconfig` | Buildroot interactive configuration |
| **Linux Menuconfig** | `make -C hw/qmtech_zynq7020/bld linux-menuconfig` | Interactive kernel Kconfig |

Output images are generated in `hw/qmtech_zynq7020/bld/images/`:
* `boot.bin` (SPL / FSBL + Bitstream + U-Boot)
* `u-boot.img`
* `uImage`
* `zynq-qmtech-xc720.dtb`
* `sdcard.img`

---

## 🔄 Multi-PC Kernel Sync Helper: `tools/sync_kernel.sh`

```bash
# Check status of local kernel working tree:
./hw/qmtech_zynq7020/tools/sync_kernel.sh status

# Push kernel commits to GitHub before switching PCs:
./hw/qmtech_zynq7020/tools/sync_kernel.sh push

# Pull latest kernel commits on another PC:
./hw/qmtech_zynq7020/tools/sync_kernel.sh pull

# Force an incremental kernel rebuild:
./hw/qmtech_zynq7020/tools/sync_kernel.sh rebuild
```

---

## 🔌 Hardware Debugging: Pico JTAG / XVC

From the Hackaday + Adam Taylor flow, the referenced project is:
* `https://github.com/kholia/xvc-pico/`
* Tutorial: `https://www.adiuvoengineering.com/post/microzed-chronicles-jtag-using-a-raspberry-pi-pico`

### What `xvc-pico` gives you:
* RP2040/Pico firmware implementing an XVC-compatible JTAG bridge.
* Host daemon (`xvcd-pico`) for the XVC server endpoint.
* Vivado Hardware Manager connects via **Add Xilinx Virtual Cable (XVC)**.

### Practical workflow:
1. Flash Pico with `xvc-pico` firmware (`.uf2` available in that repo).
2. Wire Pico JTAG to Zynq JTAG (TCK/TMS/TDI/TDO/GND, with correct voltage domain).
3. Run `xvcd-pico` on host.
4. In Vivado Hardware Manager, add XVC target (host IP + port).
5. Build outputs for `xvc-pico` belong in `hw/qmtech_zynq7020/pico_bld/` (ignored in `.gitignore`).

---

## 📁 Repository Directory Conventions

* `hw/qmtech_zynq7020/bld/`: Dedicated out-of-tree Buildroot build output (ignored by `.gitignore`).
* `hw/qmtech_zynq7020/pico_bld/`: Local XVC daemon/firmware builds (ignored by `.gitignore`).
* `hw/qmtech_zynq7020/tools/`: Automation scripts (`setup_workspace.sh`, `sync_kernel.sh`).
