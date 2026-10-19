# Radxa Cubie A5E Sensor & Peripheral Test Guide

> **Detailed Live Test Results**: see [`TEST_RESULTS.md`](TEST_RESULTS.md)  
> **Target Subsystem**: XuanTie E906 RISC-V Coprocessor (`apps/e906_repl_app`)  
> **Governing Specifications**: `[SPEC-E906-REPL-06]`, `[SPEC-GPS-01]`, `[SPEC-E906-REPL-12]`

---

## 1. 40-Pin Header Wiring Ground Truth (Schematic v1.2)

| Header Pin | SoC Signal | Wire Color | Peripheral Connection | Function |
| :--- | :--- | :--- | :--- | :--- |
| **Pin 2** | `5V_VCC` | **Red** | GPS & Compass VCC | 5V DC System Power |
| **Pin 6** | `GND` | **Black** | Common System Ground | Electrical Ground Reference |
| **Pin 16** | `UART3_TX` (`PI11`) | **Yellow** | u-blox NEO-M8N **GPS RX** | Transmit Commands from Cubie to GPS |
| **Pin 31** | `UART3_RX` (`PI12`) | **Green** | u-blox NEO-M8N **GPS TX** | Ingest NMEA/UBX Telemetry into Cubie |
| **Pin 3** | `TWI1_SDA` (`PB5`) | **Blue** | QMC5883L Compass SDA | I2C Serial Data line |
| **Pin 5** | `TWI1_SCL` (`PB4`) | **Orange** | QMC5883L Compass SCL | I2C Serial Clock line (400 kHz) |

> [!IMPORTANT]
> **Serial Crossover Directionality**:
> * GPS **Green Wire (TX)** connects to Cubie **Pin 31 (RX / `PI12`)**!
> * GPS **Yellow Wire (RX)** connects to Cubie **Pin 16 (TX / `PI11`)**!
>
> *Note on Pins 11 & 13*: Cubie A5E Schematic v1.2 Sheet 5 proves that `PB0`/`PB1` are No-Connect. Header Pins 11 & 13 map to `PI9`/`PI10` (no UART hardware block). UART3 on Pins 16 & 31 is the hardware navigation port.

---

## 2. Test Execution & Reproduction Commands

### A. Automated Host Verification (SpecTrace Evidence Ledger)
Run from the repository root:
```bash
# Connects to 192.168.3.3, executes test suite on E906, generates TEST_RESULTS.md,
# and appends structured record to the SpecTrace evidence ledger:
python3 tools/test_e906_sensors_board.py
```

### B. Live Ingestion on XuanTie E906 Coprocessor
Execute on the board:
```bash
ssh root@192.168.3.3
python3 /root/run_e906_script.py /root/flight_sensors.py
```

### C. Direct Linux Host Shell Commands
```bash
# Stream NMEA sentences directly from /dev/ttyS3 @ 115200 baud:
stty -F /dev/ttyS3 115200 raw -echo && cat /dev/ttyS3

# Scan I2C bus 1 for compass (detected at 0x1E):
i2cdetect -y 1
```

---

## 3. Hardware Register Diagnostics (`devmem`)

Verify that the Allwinner PIO controller has actively multiplexed the peripheral pins:

```bash
# Check Pinmux PB_CFG0 (PB4/PB5 TWI1): should be 0x77ffff22 (mux 2 = TWI1)
devmem 0x02000030

# Check Pinmux PI_CFG1 (PI11/PI12 UART3): should be 0xfffff22f (mux 2 = UART3)
devmem 0x02000184

# Check Pull-up PI_PULL0: should be 0x00100000 (pull-up enabled on PI12 RX)
devmem 0x020001a4
```
