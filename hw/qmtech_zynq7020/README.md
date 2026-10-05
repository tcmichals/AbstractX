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

## ⚠️ Critical Hardware Bring-Up Reminder: USB/ULPI PHY on Zynq-7000

When bringing up USB Host mode on Zynq-7000 boards with external ULPI PHYs (e.g. Microchip USB3320 or TI TUSB1210):

1. **Device Tree `usb-nop-xceiv` Pattern**:
   * The ULPI transceiver must be represented as a `compatible = "usb-nop-xceiv";` PHY node.
   * `drv-vbus;` is required to command the transceiver to drive 5V onto the physical connector.
   * `reset-gpios = <&gpio0 46 GPIO_ACTIVE_LOW>;` is required if the PHY reset line is routed to an MIO pin (e.g. MIO46 on common carrier boards).

2. **Controller DT Node (`&usb0`)**:
   * `dr_mode = "host";` forces host role and avoids floating OTG ID pin false detections.
   * `disable-over-current;` is required on boards without dedicated active overcurrent circuitry to prevent the kernel from cutting port power.
   * `usb-role-switch;` provides a userspace sysfs fallback control node under `/sys/class/usb_role/`.

3. **Matching Kernel Configuration**:
   * `CONFIG_NOP_USB_XCEIV=y`
   * `CONFIG_USB_CHIPIDEA=y`
   * `CONFIG_USB_CHIPIDEA_HOST=y`
   * `CONFIG_USB_EHCI_HCD=y`

---

## ⚡ FPGA Loading Guide (Linux FPGA Manager)

The QMTECH Zynq-7020 platform supports dynamic PL bitstream programming directly from Linux userspace via the Xilinx PCAP / Linux FPGA Manager (`/sys/class/fpga_manager/fpga0`):

```bash
# Copy your Vivado bitstream to the target (or use built-in top_qmtech_zynq7020.bit):
scp top_qmtech_zynq7020.bit root@<ZYNQ_IP>:/root/

# Load bitstream into the Artix-7 PL fabric:
load_fpga top_qmtech_zynq7020.bit
```

### What `load_fpga` does:
1. Strips Xilinx `.bit` header metadata and formats raw `.bin` bitstream.
2. Directs the Linux FPGA Manager (`devcfg@f8007000`) to program the fabric over PCAP.
3. Automatically queries `/dev/uio0` to verify the AbstractX hardware magic ID (`0x41535036` `"ASP6"`) and confirms the PL 100 MHz clock is ticking.

---

## 🔍 Hardware Diagnostics & Verification (`zynq_test`)

The target image includes `/usr/bin/zynq_test` (from `tools/zynq_diagnostics.py`), providing an interactive CLI to test all hardware blocks:

```bash
# 1. Run Complete Automated Diagnostic Suite:
zynq_test --test all
# Output checks:
#   [Test 1] Hardware Ping & Scratch Register Loopback (0x4000_0004)
#   [Test 2] User LED Toggle (Carrier D3 @ P22, Core D2 @ M14)
#   [Test 3] 100 MHz PL Hardware Nanosecond Benchmark (0x4000_0010)
#   [Test 4] IMU SPI WHO_AM_I Probe (ICM-42688-P @ 0x75 -> 0x47)
#   [Test 5] Live 8 kHz TLP Telemetry Stream Monitor

# 2. Individual Subsystem Tests:
zynq_test --test ping     # Scratch register loopback
zynq_test --test led      # Alternates LEDs 3 times
zynq_test --test clock    # Benchmarks PL nanosecond timer against CPU
zynq_test --test imu      # Reads WHO_AM_I register (0x75)
zynq_test --test stream   # Monitored 8 kHz TLP ingestion from DDR3 ring
```

### Direct IMU SPI Register Operations:
```bash
# Read any IMU register over SPI (hex offset):
zynq_test --read-reg 0x75         # Reads WHO_AM_I (returns 0x47 for ICM-42688-P)

# Write any IMU register over SPI (offset, value):
zynq_test --write-reg 0x4E 0x0F   # PWR_MGMT0: Wake up Gyro & Accel in Low-Noise mode
zynq_test --write-reg 0x4F 0x03   # GYRO_CONFIG0: Set 8 kHz ODR, +/-2000 dps
zynq_test --write-reg 0x50 0x03   # ACCEL_CONFIG0: Set 8 kHz ODR, +/-16 g
```

### Controlling Autonomous 8 kHz DRDY Auto-DMA:
```bash
# Start continuous hardware DRDY Auto-DMA:
zynq_test --start-auto-dma
# PL hardware now autonomously latches 64-bit timestamps and bursts 14B IMU frames into DDR3!

# Stop hardware Auto-DMA:
zynq_test --stop-auto-dma
# Halts autonomous DMA triggers and returns PL core to idle.

# One-step sensor initialization and auto-DMA launch:
zynq_test --init-imu
```

---

## 🌐 Ethernet Connectivity to AbstractX Studio & Flight Display

The QMTECH Zynq-7020 Carrier board features a Gigabit Ethernet RJ45 port (RTL8211E PHY) connected to the PS Gigabit Ethernet MAC (`GEM0`).

```
+------------------------------------+          UDP Port 9870           +------------------------------------+
|  QMTECH Zynq-7020 Board (Target)   |   --------------------------->   |  Host Workstation / PC (Engineer)  |
|  - Artix-7 PL Auto-DMA (8 kHz IMU) |   64-Byte TLP Telemetry Frames   |  - AbstractX Studio (Workbench)    |
|  - Linux RT (Dual Cortex-A9)       |                                  |  - Flight Display (Cockpit PFD)    |
|  - gps_imu_app / Telemetry Server  |                                  |  - 120 FPS Real-time Visualization |
+------------------------------------+                                  +------------------------------------+
```

### 1. Verify Target Network Connection:
```bash
# Check IP address on target:
ip addr show eth0

# If using DHCP, network initializes automatically.
# To assign a static IP:
ip addr add 192.168.1.100/24 dev eth0
ip link set eth0 up
```

### 2. Start Flight Application on the Zynq Board:
```bash
# Run gps_imu_app streaming telemetry to your PC's IP over UDP port 9870:
gps_imu_app --udp <HOST_PC_IP>:9870
```

### 3. Launch the GUI on your Host Workstation:
On your Linux or desktop workstation:

```bash
# Launch the unified AbstractX Studio engineering workbench:
python3 tools/visualizer/abstractx_studio.py --port 9870

# Or launch the dedicated 120 FPS Primary Flight Display (PFD) canvas:
python3 tools/flight_display.py --port 9870
```

The GUI will immediately detect the incoming 64-byte `Tlp64` telemetry stream, rendering:
* **Primary Flight Display (PFD)**: Artificial horizon, roll/pitch attitude wireframe, altitude, airspeed.
* **Silicon Observability Workbench**: Live 8 kHz IMU scope, CPU utilization, TLP bus packet inspection, Wishbone transaction counters, and SPSC ring memory occupancy.

---

## 🏛️ AbstractX Standard FPGA Architecture Invariants

The QMTECH Zynq-7020 implementation follows the **mandatory AbstractX FPGA fabric invariants** shared across all supported FPGA silicon (Xilinx Zynq, Gowin GW5AST, Intel Cyclone):

1. **512-Bit (64-Byte) Synchronous TLP Switch Fabric (`asp_router.sv`)**:
   - Universal, synthesizable crossbar switch routing 64-byte TLPs between host DMA, telemetry producers, and Wishbone peripherals.
2. **Standard On-Chip Wishbone Interconnect (`asp_wishbone_master.sv`)**:
   - `0x4000_0000`: System version, magic (`0x41535036`), nanosecond master timer, scratch loopback, LEDs.
   - `0x4000_0100`: IMU Auto-DMA & Direct SPI engine (`asp_imu_auto_dma.sv`).
   - `0x4000_0200`: DShot/PWM 4-channel motor core (`asp_dshot_core.sv`).
   - `0x4000_0600`: WS2812B NeoPixel RGB status core (`asp_neopixel_core.sv`).
3. **Zero-Copy Host SPSC DMA Engine (`asp_axi_dma.sv`)**:
   - Dual-ring circular SPSC buffers in coherent DDR memory with doorbells and UIO interrupt signaling (`IRQ_F2P[0]`).
4. **Autonomous DRDY Timestamping & Sampling**:
   - Sub-350 ns DRDY-to-DDR burst transfer without CPU intervention or interrupt jitter.

---

## 📁 Repository Directory Conventions

* `hw/qmtech_zynq7020/bld/`: Dedicated out-of-tree Buildroot build output (ignored by `.gitignore`).
* `hw/qmtech_zynq7020/pico_bld/`: Local XVC daemon/firmware builds (ignored by `.gitignore`).
* `hw/qmtech_zynq7020/tools/`: Diagnostic and automation scripts (`load_bitstream.py`, `zynq_diagnostics.py`, `setup_workspace.sh`, `sync_kernel.sh`).
* `hw/qmtech_zynq7020/top_qmtech_zynq7020.sv`: Top-level SystemVerilog module.
* `hw/qmtech_zynq7020/qmtech_zynq7020.xdc`: Physical pin constraints.

