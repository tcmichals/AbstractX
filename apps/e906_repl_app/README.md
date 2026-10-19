# AbstractX XuanTie E906 MicroPython REPL Application

This directory contains the interactive hardware scripting, diagnostic, and bring-up environment for the **Radxa Cubie A5E (Allwinner A527 / T527)** XuanTie E906 RISC-V coprocessor.

It embeds the upstream MicroPython bytecode runtime inside a freestanding C++20 coroutine task with zero MCU heap allocations, enabling interactive hardware probing (`abstractx.peek`, `abstractx.poke`) and rapid driver staging alongside hard real-time flight loops.

---

## 1. Physical 40-Pin Header & Peripheral Wiring (NEO-M8N GPS + Compass)

When wiring a standard drone GPS & Electronic Compass puck (such as the u-blox NEO-M8N with QMC5883L/HMC5883L compass) to the Radxa Cubie A5E 40-pin expansion header, use the following pin assignments:

```mermaid
flowchart TD
    subgraph CUBIE["Radxa Cubie A5E 40-Pin Header"]
        P1["Pin 1: 3.3V Power (VCC_3V3)"]
        P3["Pin 3: I2C1-SDA (PH0 / TWI1)"]
        P5["Pin 5: I2C1-SCL (PH1 / TWI1)"]
        P6["Pin 6: Ground (GND)"]
        P8["Pin 8: UART0-TX (PB9 - Linux Console)"]
        P10["Pin 10: UART0-RX (PB10 - Linux Console)"]
        P11["Pin 11: UART2-TX (PB0 - Navigation SoC TX)"]
        P13["Pin 13: UART2-RX (PB1 - Navigation SoC RX)"]
    end

    subgraph GPS_PUCK["u-blox NEO-M8N GPS + Compass Puck"]
        VCC["VCC (3.3V / 5V)"]
        GND["GND"]
        SDA["I2C SDA"]
        SCL["I2C SCL"]
        TX["GPS TX (Serial Stream Output)"]
        RX["GPS RX (Configuration Input)"]
    end

    P1 ===>|3.3V Power| VCC
    P6 ===>|Ground| GND
    P3 <===>|I2C Data| SDA
    P5 ===>|I2C Clock| SCL
    P11 ===>|SoC Commands (TX -> RX)| RX
    P13 <===|NMEA / UBX Stream (TX -> RX)| TX
```

### Complete Pin & Color Reference Table

| Header Pin | SoC Net / Function | Target Peripheral | Wire Color | Puck Function & Connection Rule |
|---|---|---|---|---|
| **Pin 1** (or 2/4) | `VCC_3V3` (or `5V`) | Power Supply | **Red** | Power input for GPS and Compass |
| **Pin 6** (or 14) | `GND` | Ground Reference | **Black** | Common system ground |
| **Pin 3** | `PH0 / TWI1-SDA` | **I2C1 Data** | **Blue** | I2C SDA (Compass `0x0D`/`0x1E` & u-blox DDC `0x42`) |
| **Pin 5** | `PH1 / TWI1-SCL` | **I2C1 Clock** | **Orange** | I2C SCL (Compass & u-blox DDC) |
| *Pin 8* | *PB9 / UART0-TX* | *Debug Terminal* | *White* | *Reserved: Linux Host Serial Debug Console (115200)* |
| *Pin 10* | *PB10 / UART0-RX* | *Debug Terminal* | *Gray* | *Reserved: Linux Host Serial Debug Console (115200)* |
| **Pin 11** | `PB0 / UART2-TX` | **UART2 Transmit** | **Yellow** | **GPS RX** (SoC Transmit $\rightarrow$ GPS Puck Receive) |
| **Pin 13** | `PB1 / UART2-RX` | **UART2 Receive** | **Green** | **GPS TX** (GPS Puck Transmit $\rightarrow$ SoC Receive) |

> [!IMPORTANT]
> **Serial Crossover Rule**:
> * **Pin 11 (UART2-TX)** MUST connect to the GPS puck **RX (Yellow)**.
> * **Pin 13 (UART2-RX)** MUST connect to the GPS puck **TX (Green)**.
> * Connecting TX to TX will result in silence and prevent NMEA streaming.

---

## 2. Architecture & Memory Quarantine

The MicroPython REPL runs under strict deterministic execution rules (`[SPEC-E906-REPL-01]` through `[SPEC-E906-REPL-06]`):

1. **Zero MCU SRAM Heap**: The real-time C++ coroutine dispatcher runs 100% zero-heap in MCU SRAM Space 0 (`0x3FFC0000`).
2. **DSP Memory Quarantine**: MicroPython's 128 KB Garbage Collector heap is quarantined inside the dormant on-chip Cadence HiFi4 Audio DSP memory aperture (`0x00020000`).
3. **Cooperative Interleaving**: MicroPython bytecode loops call `abstractx::repl_vm_hook()` at every branch/loop boundary, allowing primary-paced sensor rings to process on time.
4. **Resilient REPL Exceptions**: KeyboardInterrupt (Ctrl-C) and soft reset (`PYEXEC_FORCED_EXIT`, Ctrl-D) are safely handled via setjmp/longjmp NLR blocks without kernel panic or coprocessor crash.

---

## 3. Quick Start & Verification

### A. Build the E906 Firmware Binary
```bash
# Cross-compile for XuanTie E906 coprocessor
cmake -S . -B build_e906 -DABSTRACTX_TARGET=allwinner_e906
cmake --build build_e906 --target e906_repl_app
```

### B. Launch Interactive REPL from Linux Host
On the booted Radxa Cubie A5E:
```bash
# Load firmware into remoteproc
echo stop > /sys/class/remoteproc/remoteproc0/state
cp e906_repl_app.elf /lib/firmware/
echo e906_repl_app.elf > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# Connect to the interactive MicroPython REPL over RPMsg
microcom -p /dev/rpmsg0
```

### C. Inspect Hardware from the MicroPython Prompt
```python
>>> import abstractx

# Check version
>>> abstractx.version()
'AbstractX 1.0.0 (E906 MicroPython REPL)'

# Read CPU cycle counter (rdcycle)
>>> abstractx.cycles()
145829104

# Peek UART2 status register (Base 0x02500800)
>>> hex(abstractx.peek(0x0250087C))
'0x00000006'

# Soft reset REPL VM
>>> (Press Ctrl-D)
```
