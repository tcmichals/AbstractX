# Radxa Cubie A5E u-blox NEO-M8N GPS & I2C Compass Bring-Up Guide

This guide defines the hardware wiring, Device Tree architecture, and multi-stage
bring-up workflow for integrating a standard **u-blox NEO-M8N GPS & Electronic Compass Puck**
with the **Radxa Cubie A5E (Allwinner A527 / T527)** under AbstractX.

---

## 1. Hardware Architecture & Physical 40-Pin Header Layout

A standard drone GPS puck combines:
1. **u-blox NEO-M8N GNSS Receiver**: Communicates over streaming UART (default **9600 baud** NMEA / UBX) and I2C DDC (Address `0x42`).
2. **On-Board Electronic Compass**: 3-axis magnetometer (typically **QMC5883L** at `0x0D` or **HMC5883L** at `0x1E`) sharing the same I2C bus.

```mermaid
flowchart TD
    subgraph CUBIE["Radxa Cubie A5E 40-Pin Header"]
        P2["Pin 2: 5V Power (5V_VCC)"]
        P3["Pin 3: I2C1-SDA (PB5 / TWI1)"]
        P5["Pin 5: I2C1-SCL (PB4 / TWI1)"]
        P6["Pin 6: Ground (GND)"]
        P8["Pin 8: UART0-TX (PB9 - Linux Debug Console)"]
        P10["Pin 10: UART0-RX (PB10 - Linux Debug Console)"]
        P16["Pin 16: UART3-TX (PI11 - GPS Serial RX)"]
        P31["Pin 31: UART3-RX (PI12 - GPS Serial TX)"]
    end

    subgraph GPS_PUCK["u-blox NEO-M8N GPS + Compass Puck (Built-in Pull-Ups)"]
        VCC["VCC (5V Power)"]
        GND["GND (Ground)"]
        SDA["I2C SDA (Integrated 2.2kΩ-4.7kΩ Pull-Up)"]
        SCL["I2C SCL (Integrated 2.2kΩ-4.7kΩ Pull-Up)"]
        TX["GPS TX (Streaming Output)"]
        RX["GPS RX (Command Input)"]
    end

    P2 ===>|5V Power| VCC
    P6 ===>|Ground| GND
    P3 <===>|I2C Data| SDA
    P5 ===>|I2C Clock| SCL
    P16 ===>|Serial Commands (SoC TX -> GPS RX)| RX
    P31 <===|NMEA / UBX (GPS TX -> SoC RX)| TX
```

### Physical 40-Pin Header Socket Diagram & Wire Colors

```text
               [ TOP OF 40-PIN HEADER ]
                      
  (ODD ROW)                                (EVEN ROW)
  ======================================================
  Pin 1: 3.3V Power         [ 1 ] [ 2 ]    Pin 2: 5V Power      <-- 🔴 RED WIRE (5V Power)
  Pin 3: I2C1-SDA (PB5)     [ 3 ] [ 4 ]    Pin 4: 5V Power
       ↳ 🔵 BLUE WIRE       [ 5 ] [ 6 ]    Pin 6: Ground (GND)  <-- ⚫ BLACK WIRE (Ground)
  Pin 5: I2C1-SCL (PB4)     [ 7 ] [ 8 ]    Pin 8: Console TX    (White - Host minicom /dev/ttyS0)
       ↳ 🟠 ORANGE WIRE     [ 9 ] [ 10]    Pin 10: Console RX   (Gray  - Host minicom /dev/ttyS0)
  Pin 11: GPIO / PWM (PI9)  [ 11] [ 12]    Pin 12: SPI1-CS0 (PI2)
  Pin 13: GPIO / PWM (PI10) [ 13] [ 14]    Pin 14: Ground
  Pin 15: S-UART0-TX (PL12) [ 15] [ 16]    Pin 16: UART3-TX (PI11) <-- 🟡 YELLOW WIRE (GPS RX)
  Pin 17: 3.3V Power        [ 17] [ 18]    Pin 18: GPIO (PI14)
  Pin 19: SPI1-MOSI (PB13)  [ 19] [ 20]    Pin 20: Ground
  Pin 21: SPI1-MISO (PB14)  [ 21] [ 22]    Pin 22: FPGA Frame Ready ISR (PL13)
  Pin 23: SPI1-CLK (PB12)   [ 23] [ 24]    Pin 24: SPI1-CS0 (PB11)
  Pin 25: Ground            [ 25] [ 26]    Pin 26: UART4-TX (PI0)
  Pin 27: I2C2-SDA (PI16)   [ 27] [ 28]    Pin 28: I2C2-SCK (PI15)
  Pin 29: IMU DRDY (PI8)    [ 29] [ 30]    Pin 30: Ground
  Pin 31: UART3-RX (PI12)   [ 31] [ 32]    Pin 32: UART4-RX (PI1)
       ↳ 🟢 GREEN WIRE
  ======================================================
```

### Complete 40-Pin Header Connection Table

| Header Pin | SOC Pin / Net | Target Peripheral | Puck Wire | Function |
|---|---|---|---|---|
| **Pin 2** (or Pin 4) | `5V_VCC` | System Power | **Red** | **5V Power Input** for GPS & Compass puck |
| **Pin 6** (or Pin 14) | `GND` | Ground Reference | **Black** | **System Ground** |
| **Pin 3** | `PB5 / TWI1-SDA` | **I2C1 Data** | **Blue** | **I2C Data** (Compass & u-blox DDC) |
| **Pin 5** | `PB4 / TWI1-SCL` | **I2C1 Clock** | **Orange** | **I2C Clock** (Compass & u-blox DDC) |
| *Pin 8* | *PB9 / UART0-TX* | *Linux Console* | *White* | *Linux Debug Terminal (115200: /dev/ttyS0)* |
| *Pin 10* | *PB10 / UART0-RX*| *Linux Console* | *Gray* | *Linux Debug Terminal (115200: /dev/ttyS0)* |
| **Pin 16** | `PI11 / UART3-TX`| **UART3 Transmit**| **Yellow** | **GPS Serial RX** (SoC TX $\rightarrow$ GPS RX) |
| **Pin 31** | `PI12 / UART3-RX`| **UART3 Receive** | **Green** | **GPS Serial TX** (GPS TX $\rightarrow$ SoC RX) |
| **Pin 24, 23, 19, 21** | `PB11-PB14 / SPI1`| **Dual-SPI / Single-SPI** | — | Hardware FPGA X-Fabric / High-Speed Sensor bus |
| **Pin 29** | `PI8 / PI-EINT8` | **IMU DRDY** | — | Dedicated Hardware Edge-Trigger Interrupt Line |

> [!IMPORTANT]
> **I2C Pull-Up Resistors Hardware Verification**:
> * Radxa Cubie A5E Schematic v1.2 Sheet 19 proves that Pins 3 & 5 (`PB5`/`PB4`) have **no on-board pull-up resistors**.
> * **Standard GPS pucks** (e.g. u-blox NEO-M8N with integrated QMC/HMC compass) already incorporate internal 2.2kΩ–4.7kΩ pull-up resistors on their SDA/SCL lines to 3.3V. When plugged in, the bus is automatically pulled up.
> * If using raw breakout boards without pull-ups, external 2.2kΩ–4.7kΩ pull-ups to 3.3V (Pin 1 or Pin 17) are mandatory.

> [!IMPORTANT]
> **Serial Crossover Rule**: Connect **Pin 16 (UART3-TX)** to GPS **RX (Yellow)**, and **Pin 31 (UART3-RX)** to GPS **TX (Green)**.
> Never connect TX to TX!
> Note: Radxa Cubie A5E schematic sheet 5 proves `PB0`/`PB1` (UART2) are No-Connect on the PCB; Pins 11 & 13 connect to `PI9`/`PI10` (no UART hardware). UART3 on Pins 16 & 31 is the hardware navigation port.

---

## 2. Device Tree Architecture: Dual Overlay Profiles

AbstractX provides two purpose-built Device Tree Overlays in `apps/e906_repl_app/`:

### Profile A: Linux Host Profile (`cubie-a5e-flight-stack-linux.dtso`)
For Linux userspace testing, scripting, and peripheral bring-up:
* `i2c1` (Pins 3 & 5) is bound to Linux I2C controller driver $\rightarrow$ `/dev/i2c-1`.
* `uart3` (Pins 16 & 31) is bound to Linux 8250 serial driver $\rightarrow$ `/dev/ttyAS3` (`serial3` alias).
* `spi1` (Pins 24, 23, 19, 21) is bound to Linux SPIDEV $\rightarrow$ `/dev/spidev1.0`.
* UART0 debug terminal is locked to `/dev/ttyS0` (Pins 8 & 10) via `/aliases`.

### Profile B: XuanTie E906 Real-Time Profile (`cubie-a5e-flight-stack-e906.dtso`)
For hard real-time execution on the XuanTie E906 RISC-V co-processor:
* Peripherals (`spi1`, `uart3`, `i2c1`) are bound to `compatible = "generic-uio"`.
* Linux configures pinmux and ungates CCU clocks/resets, but attaches **no kernel drivers**, preventing bus contention and register resets.
* XuanTie E906 firmware accesses registers directly at `0x02500c00` (UART3), `0x02502400` (TWI1), and `0x04026000` (SPI1).

---

## 3. Multi-Stage Bring-Up Workflow

### Stage 1: Seed & Verify with Python (`tools/seed_cubie_a5e_gps.py`)

AbstractX provides a dedicated Python seeder and diagnostic utility to test both buses immediately under Linux:

```bash
# 1. Self-test in Mock mode (simulates NMEA, UBX, and I2C scan without hardware):
python3 tools/seed_cubie_a5e_gps.py --mock

# 2. Probe I2C1 (Pins 3 & 5) for u-blox DDC (0x42) and Compass (0x0D / 0x1E):
python3 tools/seed_cubie_a5e_gps.py --i2c 1

# 3. Ingest GPS NMEA serial stream on UART3 (Pins 16 & 31) at factory default 9600 baud:
python3 tools/seed_cubie_a5e_gps.py --serial /dev/ttyAS3 --baud 9600

# 4. Seed the u-blox M8N to 5 Hz navigation rate and stream 64B TLPs to Studio:
python3 tools/seed_cubie_a5e_gps.py --serial /dev/ttyAS3 --baud 9600 --i2c 1 --configure-5hz --stream-tlp
```

Expected diagnostic output on successful connection:
```text
[+] Scanning I2C bus 1 (Pins 3 & 5)...
  [ACK] Device detected at 0x0D (QMC5883L 3-Axis Electronic Compass)
  [ACK] Device detected at 0x42 (u-blox NEO-M8N DDC Port - 84 bytes in buffer)
[+] Connected to /dev/ttyS2 at 9600 baud.
[+] Seeding u-blox M8N configuration: Setting 5 Hz rate & enabling NAV-PVT...
[1/10] FIX (9 Sats) | Lat: 37.774920°, Lon: -122.419410°, Alt: 18.4 m
[2/10] FIX (9 Sats) | Lat: 37.774921°, Lon: -122.419412°, Alt: 18.5 m
```

---

### Stage 2: Interactive MicroPython REPL on XuanTie E906

Once loaded onto the E906 co-processor via RemoteProc (`apps/e906_repl_app`), connect to the interactive
REPL via RPMsg (`/dev/rpmsg0` or AbstractX Studio Tab 11):

```python
>>> import abstractx

# 1. Verify UART2 Peripheral Clock & Status (UART2_LSR at 0x02500814)
>>> lsr = abstractx.peek(0x02500814)
>>> print(f"Data Ready: {bool(lsr & 0x01)}")

# 2. Drain incoming NMEA characters from FIFO (UART2_RBR at 0x02500800)
>>> for _ in range(30):
...     if abstractx.peek(0x02500814) & 0x01:
...         print(chr(abstractx.peek(0x02500800) & 0xFF), end="")
$GNGGA,123519,3746.4952,N...
```

---

### Stage 3: Real-Time C++20 Flight Loop & Studio Telemetry

In the compiled production flight binary (`gps_imu_app`), the GPS pipeline runs autonomously with zero dynamic heap:

1. **Lock-Free Ingestion**: `fc::hal::Uart2` receives streaming bytes via circular ring buffer.
2. **Primary-Paced Channel**: `drivers::gps::UbloxGps` awaits incoming fixes without polling spins:
   ```cpp
   // apps/gps_imu_app/include/gps_imu_tasks.hpp
   Task<void> gps_producer_task(UbloxGps& gps) {
       for (;;) {
           GpsFix fix = co_await gps.next_fix_async();
           if (fix.valid) {
               Tlp64 tlp = UbloxGps::to_tlp(fix);
               g_telemetry_ring.push(tlp);
           }
       }
   }
   ```
3. **Observability**:
   - **Studio Tab 3 (TLP Bus Debugger)**: Shows real-time 64-byte packets with tag `0x02`, lat/lon, altitude, ground speed, and satellite count.
   - **USER Floating Canvas**: Displays 3D GPS satellite lock and ground tracking wireframes.

---

## 4. Hardware Troubleshooting & Errata

| Symptom | Probable Cause | Corrective Action |
|---|---|---|
| **Garbage characters in serial stream** | Baud rate mismatch | The factory default for u-blox NEO-M8N is **9600 baud**. If previously configured by ArduPilot/Betaflight, it may be at **38400** or **115200 baud**. Try testing with `--baud 9600`, `--baud 38400`, and `--baud 115200`. |
| **No characters received at all** | TX/RX lines swapped | Swap Pin 11 and Pin 13 connections. Remember: SoC TX connects to GPS RX; SoC RX connects to GPS TX. |
| **`i2cdetect` shows nothing on Bus 1** | Missing 3.3V pull-ups | Most GPS pucks include 4.7 kΩ pull-up resistors on the puck PCB. If using a raw breakout, verify SDA and SCL are pulled up to 3.3V. |
| **Valid flag remains `0` / No Fix** | Indoor antenna attenuation | u-blox GPS receivers require a direct line of sight to the sky. Move the puck near a window or outdoors. The LED on the puck will begin blinking once a 3D fix is acquired. |
| **Linux serial console dies when loading overlay** | Alias node collision | Verify `/aliases` includes `serial0 = "/soc/serial@2500000"`. |
