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
    subgraph CUBIE["Radxa Cubie A5E / A7A 40-Pin Header"]
        P2["Pin 2: 5V Power (5V_VCC)"]
        P3["Pin 3: I2C1-SDA (PH0 / TWI1)"]
        P5["Pin 5: I2C1-SCL (PH1 / TWI1)"]
        P6["Pin 6: Ground (GND)"]
        P8["Pin 8: UART0-TX (PB9 - Linux Console)"]
        P10["Pin 10: UART0-RX (PB10 - Linux Console)"]
        P11["Pin 11: UART2-TX (PB0 - Navigation SoC TX)"]
        P13["Pin 13: UART2-RX (PB1 - Navigation SoC RX)"]
    end

    subgraph GPS_PUCK["u-blox NEO-M8N GPS + Compass Puck"]
        VCC["VCC (5V Power)"]
        GND["GND (Ground)"]
        SDA["I2C SDA"]
        SCL["I2C SCL"]
        TX["GPS TX (Output)"]
        RX["GPS RX (Input)"]
    end

    P2 ===>|5V Power| VCC
    P6 ===>|Ground| GND
    P3 <===>|I2C Data| SDA
    P5 ===>|I2C Clock| SCL
    P11 ===>|Serial Commands (TX -> RX)| RX
```

### Physical 40-Pin Header Socket Diagram & Wire Colors

```text
               [ TOP OF 40-PIN HEADER ]
                      
  (ODD ROW)                                (EVEN ROW)
  ======================================================
  Pin 1: 3.3V Power         [ 1 ] [ 2 ]    Pin 2: 5V Power      <-- 🔴 RED WIRE (5V Power)
  Pin 3: I2C1-SDA (Data)    [ 3 ] [ 4 ]    Pin 4: 5V Power
       ↳ 🔵 BLUE WIRE       [ 5 ] [ 6 ]    Pin 6: Ground (GND)  <-- ⚫ BLACK WIRE (Ground)
  Pin 5: I2C1-SCL (Clock)   [ 7 ] [ 8 ]    Pin 8: Console TX    (White - Host minicom)
       ↳ 🟠 ORANGE WIRE     [ 9 ] [ 10]    Pin 10: Console RX   (Gray  - Host minicom)
  Pin 11: UART2-TX (SoC TX) [ 11] [ 12]    Pin 12: GPIO
       ↳ 🟡 YELLOW WIRE     [ 13] [ 14]    Pin 14: Ground
  Pin 13: UART2-RX (SoC RX)   :     :
       ↳ 🟢 GREEN WIRE
  ======================================================
```

### Complete 40-Pin Header Connection Table

| Header Pin | SOC Pin / Net | Target Peripheral | Puck Wire | Function |
|---|---|---|---|---|
| **Pin 2** (or Pin 4) | `5V_VCC` | System Power | **Red** | **5V Power Input** for GPS & Compass puck |
| **Pin 6** (or Pin 14) | `GND` | Ground Reference | **Black** | **System Ground** |
| **Pin 3** | `PH0 / TWI1-SDA` | **I2C1 Data** | **Blue** | **I2C Data** (Compass & u-blox DDC) |
| **Pin 5** | `PH1 / TWI1-SCL` | **I2C1 Clock** | **Orange** | **I2C Clock** (Compass & u-blox DDC) |
| *Pin 8* | *PB9 / UART0-TX* | *Linux Console* | *White* | *Reserved for Linux Debug Terminal (115200)* |
| *Pin 10* | *PB10 / UART0-RX*| *Linux Console* | *Gray* | *Reserved for Linux Debug Terminal (115200)* |
| **Pin 11** | `PB0 / UART2-TX` | **UART2 Transmit** | **Yellow** | **GPS Serial RX** (SoC TX $\rightarrow$ GPS RX) |
| **Pin 13** | `PB1 / UART2-RX` | **UART2 Receive** | **Green** | **GPS Serial TX** (GPS TX $\rightarrow$ SoC RX) |
| **Pin 14** | `GND` | Ground | **Black** | Secondary Ground Reference |

> [!IMPORTANT]
> **Serial Crossover Rule**: Connect **Pin 11 (UART2-TX)** to GPS **RX**, and **Pin 13 (UART2-RX)** to GPS **TX**.
> Never connect TX to TX!

---

## 2. Device Tree Architecture & Routing Invariants

The AbstractX flight overlay (`apps/gps_imu_app/platforms/allwinner_e906/cubie_a5e_e906_overlay.dtso`)
enforces two strict hardware partitioning rules:

### Invariant 1: Preserve Linux Host Console on UART0
In Allwinner device trees, dynamically enabling `uart2` without locking aliases can cause the Linux kernel
to dynamically shift `ttyS0` to UART2, breaking your serial debug terminal.
The overlay explicitly declares:
```dts
&{/aliases} {
    serial0 = "/soc/serial@2500000"; /* UART0 permanently locked as /dev/ttyS0 */
    serial2 = "/soc/serial@2500800"; /* UART2 on Pins 11 & 13 */
};

&uart0 {
    status = "okay"; /* Linux Host Debug Terminal */
};
```

### Invariant 2: Bind E906 Flight Peripherals to `generic-uio`
When assigning UART2 (GPS) and I2C1 (Compass/DDC) to the bare-metal XuanTie E906 co-processor,
we bind them to `compatible = "generic-uio"`. This commands the Linux kernel to:
1. Configure pinmux (PB0/PB1 to UART2, PH0/PH1 to TWI1) at power-on.
2. Un-gate APB clock trees in the Clock Control Unit (CCU).
3. **Refrain from attaching Linux kernel tty or i2c drivers**, eliminating bus contention, register resets, and interrupt fights with the E906.

```dts
/* UART2: High-Speed Navigation Serial Bus (Pins 11 & 13) */
&uart2 {
    compatible = "generic-uio";
    pinctrl-names = "default";
    status = "okay";
};

/* I2C1 (TWI1): General Expansion Bus (Pins 3 & 5) */
&i2c1 {
    compatible = "generic-uio";
    pinctrl-names = "default";
    status = "okay";
};
```

---

## 3. Multi-Stage Bring-Up Workflow

### Stage 1: Seed & Verify with Python (`tools/seed_cubie_a5e_gps.py`)

AbstractX provides a dedicated Python seeder and diagnostic utility to test both buses immediately:

```bash
# 1. Self-test in Mock mode (simulates NMEA, UBX, and I2C scan without hardware):
python3 tools/seed_cubie_a5e_gps.py --mock

# 2. Probe I2C1 (Pins 3 & 5) for u-blox DDC (0x42) and Compass (0x0D / 0x1E):
python3 tools/seed_cubie_a5e_gps.py --i2c 1

# 3. Ingest GPS NMEA serial stream at factory default 9600 baud:
python3 tools/seed_cubie_a5e_gps.py --serial /dev/ttyS2 --baud 9600

# 4. Seed the u-blox M8N to 5 Hz navigation rate and stream 64B TLPs to Studio:
python3 tools/seed_cubie_a5e_gps.py --serial /dev/ttyS2 --baud 9600 --i2c 1 --configure-5hz --stream-tlp
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
