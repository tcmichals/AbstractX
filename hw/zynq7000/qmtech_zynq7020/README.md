<!-- Copyright (C) 2026 Tim Michals -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# QMTECH Zynq-7020 Hardware Scaffold

This directory is the integration and board bring-up workspace for the **QMTECH Zynq-7020 (XC7Z020)** in AbstractX.

It follows the exact same zero-patch, `local.mk` override architecture proven in `cubie-a5e`.

---

## 🚀 Multi-PC Workspace Setup

Run the wrapper from the AbstractX repository root:

```bash
./hw/zynq7000/qmtech_zynq7020/tools/setup_workspace.sh
```

### What `setup_workspace.sh` does automatically:
1. Locates or clones:
   * **Buildroot** (`https://gitlab.com/buildroot.org/buildroot.git`)
   * **Linux kernel** (`git@github.com:tcmichals/linux-cubie.git`, branch `cubie-linux-7.1`)
   * **U-Boot** (`git@github.com:tcmichals/u-boot-zynq.git`, branch `main`)
   * **AbstractX shared Zynq Buildroot external tree** (`hw/zynq7000/buildroot_external`)
2. Configures the dedicated out-of-tree workspace `hw/zynq7000/bld.qmtech-20/`.
3. Creates `hw/zynq7000/bld.qmtech-20/local.mk` pointing to your local `linux-cubie` and `u-boot-zynq` repositories.
4. Initializes the build with `abstractx_qmtech_zynq7020_defconfig`.

---

## 🛠️ Build Commands

All builds execute out-of-tree in `hw/zynq7000/bld.qmtech-20/`:

| Task | Command | Description |
|---|---|---|
| **Full Image Build** | `make -C hw/zynq7000/bld.qmtech-20 -j$(nproc)` | Builds toolchain, U-Boot, Linux, and generates `sdcard.img` |
| **Fast Kernel Rebuild** | `make -C hw/zynq7000/bld.qmtech-20 linux-rebuild` | Incremental rebuild using `local.mk` |
| **Fast U-Boot Rebuild** | `make -C hw/zynq7000/bld.qmtech-20 uboot-rebuild` | Incremental rebuild using `local.mk` |
| **Menuconfig** | `make -C hw/zynq7000/bld.qmtech-20 menuconfig` | Buildroot interactive configuration |
| **Linux Menuconfig** | `make -C hw/zynq7000/bld.qmtech-20 linux-menuconfig` | Interactive kernel Kconfig |

Output images are generated in `hw/zynq7000/bld.qmtech-20/images/`:
* `boot.bin` (the configured U-Boot SPL image; verify FSBL and FPGA bitstream packaging separately for the selected boot flow)
* `u-boot.img`
* `uImage`
* `zynq-qmtech-xc720.dtb`
* `sdcard.img`

---

## 🔄 Multi-PC Kernel Sync Helper: `tools/sync_kernel.sh`

```bash
# Check status of local kernel working tree:
./hw/zynq7000/qmtech_zynq7020/tools/sync_kernel.sh status

# Push kernel commits to GitHub before switching PCs:
./hw/zynq7000/qmtech_zynq7020/tools/sync_kernel.sh push

# Pull latest kernel commits on another PC:
./hw/zynq7000/qmtech_zynq7020/tools/sync_kernel.sh pull

# Force an incremental kernel rebuild:
./hw/zynq7000/qmtech_zynq7020/tools/sync_kernel.sh rebuild
```

---

---

## 🖥️ USB Serial Console Connection

The QMTECH Zynq-7020 Starter Kit Carrier board includes an onboard USB-to-UART bridge connected to Zynq PS UART0 (`ttyPS0`):

```
+------------------------------------+                  Micro-USB Cable                  +------------------------------------+
|  QMTECH Carrier Board              |   ============================================>   |  Host Development Workstation      |
|  - Micro-USB Port: "UART"          |             115200 Baud / 8N1                     |  - Port: /dev/ttyUSB0              |
|  - Silicon Labs CP2102 / CH340     |                                                   |  - Terminal: picocom / minicom     |
+------------------------------------+                                                   +------------------------------------+
```

### 1. Connect and Identify the Port:
Plug a standard Micro-USB cable from the **UART** port on the carrier board into your Linux workstation:

```bash
# Check kernel dmesg for the enumerated serial device:
dmesg | grep -E "ttyUSB|ttyACM"
# Typical output: cp210x converter now attached to ttyUSB0
```

### 2. Open the Terminal Console:
```bash
# Using picocom (Recommended):
picocom -b 115200 /dev/ttyUSB0

# Or using screen:
screen /dev/ttyUSB0 115200

# Or using minicom:
minicom -D /dev/ttyUSB0 -b 115200
```

### 3. Log In to Linux RT:
* **Username**: `root`
* **Password**: *(None / press Enter)*

---

## 🔌 Hardware Debugging: Raspberry Pi Pico JTAG / XVC (Vivado)

You can turn an inexpensive **$4 Raspberry Pi Pico (RP2040)** into a high-speed **Xilinx Virtual Cable (XVC)** JTAG programmer and hardware debugger for Vivado. This enables full Vivado Hardware Manager access, Integrated Logic Analyzer (ILA) core debugging, and bitstream programming without proprietary Xilinx platform cables.

Based on the [Adam Taylor MicroZed / Hackaday xvc-pico architecture](https://github.com/kholia/xvc-pico/):

```
+--------------------------+                  JTAG 3.3V LVCMOS Lines                  +-----------------------------------+
|  Raspberry Pi Pico       |   ---------------------------------------------------->  |  QMTECH Zynq-7020 Board (JTAG)    |
|  (Running xvc-pico.uf2)  |   GPIO 2 (Pin 4) -> TCK (Pin 6)                          |  - 14-pin Standard 2.0mm Header   |
|                          |   GPIO 3 (Pin 5) -> TMS (Pin 4)                          |  - Bank 0 / JTAG Voltage: 3.3V    |
|                          |   GPIO 4 (Pin 6) -> TDI (Pin 10)                         |                                   |
|                          |   GPIO 5 (Pin 7) -> TDO (Pin 8)                          |                                   |
|                          |   GND    (Pin 8) -> GND (Pins 3, 5, 7, 9)                |                                   |
+--------------------------+                                                          +-----------------------------------+
             |                                                                                          |
             | USB CDC-ACM (/dev/ttyACM0)                                                              |
             v                                                                                          |
+-------------------------------------------------------------------------------------------------------+
|  Host Workstation:                                                                                    |
|  1. Runs `xvcd-pico -s /dev/ttyACM0` (Opens TCP localhost:2542)                                       |
|  2. Vivado Hardware Manager connects via "Add Xilinx Virtual Cable (XVC)"                             |
|  3. Inspects PL ILA signals & Cortex-A9 DAP cores in real-time                                        |
+-------------------------------------------------------------------------------------------------------+
```

### 1. JTAG Wiring Pinout Table

Connect 5 female-to-female jumper wires between the Pico and the QMTECH board's 14-pin JTAG header:

| Pico Physical Pin | Pico GPIO | Signal Name | QMTECH JTAG Header Pin | Notes |
| :---: | :---: | :---: | :---: | :--- |
| **Pin 4** | **GPIO 2** | **TCK** | **Pin 6** | JTAG Test Clock |
| **Pin 5** | **GPIO 3** | **TMS** | **Pin 4** | JTAG Test Mode Select |
| **Pin 6** | **GPIO 4** | **TDI** | **Pin 10** | JTAG Test Data In |
| **Pin 7** | **GPIO 5** | **TDO** | **Pin 8** | JTAG Test Data Out |
| **Pin 8** | **GND** | **GND** | **Pin 3 / 5 / 7** | Common System Ground (**Mandatory**) |

> [!IMPORTANT]
> Both the Raspberry Pi Pico (RP2040) and the QMTECH Zynq-7020 JTAG interface operate natively at **3.3V LVCMOS**. No level shifters are required. Ensure common GND is connected before powering either board.

### 2. Flash the Pico Firmware:
1. Hold down the white **BOOTSEL** button on the Raspberry Pi Pico and plug it into your PC USB port.
2. The Pico will mount as a USB flash drive named `RPI-RP2`.
3. Download or copy `xvc-pico.uf2` into the `RPI-RP2` drive:
   ```bash
   # From the xvc-pico repository (https://github.com/kholia/xvc-pico/releases):
   cp xvc-pico.uf2 /media/$USER/RPI-RP2/
   ```
4. The Pico will reboot automatically and enumerate as a USB serial device (e.g. `/dev/ttyACM0`).

### 3. Build & Run the Host Daemon (`xvcd-pico`):
```bash
# Clone and build xvcd-pico:
git clone https://github.com/kholia/xvc-pico.git
cd xvc-pico/xvcd-pico
make

# Launch the daemon bridging the Pico USB serial port to TCP localhost:2542:
./xvcd-pico -s /dev/ttyACM0
# Output:
# [INFO] Listening for XVC connection on 0.0.0.0:2542...
```

### 4. Connect Vivado Hardware Manager:
1. Open **Vivado** (2020.1 or later).
2. Click **Open Hardware Manager** &rarr; **Open Target** &rarr; **Auto Connect**.
3. In the Hardware Targets window, right-click `localhost:3121` (or click *Add Target*) &rarr; **Add Xilinx Virtual Cable (XVC)**.
4. Set:
   * **Host Name**: `localhost` (or `127.0.0.1`)
   * **Port**: `2542`
5. Click **OK**.
6. Vivado will immediately discover the Zynq-7000 JTAG scan chain:
   * `arm_dap` (Dual Cortex-A9 Debug Access Port)
   * `xc7z020_1` (Artix-7 FPGA PL Fabric)
7. You can now:
   * Program bitstreams directly to PL.
   * Debug running RTL cores with the **Vivado Integrated Logic Analyzer (ILA)**.
   * Monitor internal AbstractX Wishbone, SPI, and AXI DMA transactions in real-time!

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

## FPGA architecture

The implementation details and requirements are maintained in the
[QMTECH hardware specification](SPECIFICATION.md). The shared Zynq device-tree
ABI is documented in
[`../DEVICE_TREE_CONFIGURATION.md`](../DEVICE_TREE_CONFIGURATION.md); avoid
duplicating those contracts here.

---

## 📁 Repository Directory Conventions

* `hw/zynq7000/bld.qmtech-20/`: Dedicated out-of-tree Buildroot workspace (ignored by `.gitignore`).
* `hw/zynq7000/qmtech_zynq7020/tools/`: Diagnostic and automation scripts.
* `hw/zynq7000/qmtech_zynq7020/top_qmtech_zynq7020.sv`: Top-level SystemVerilog module.
* `hw/zynq7000/qmtech_zynq7020/qmtech_zynq7020.xdc`: Physical pin constraints.
