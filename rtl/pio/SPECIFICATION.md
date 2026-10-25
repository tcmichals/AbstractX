# AbstractX Programmable I/O (PIO) Engine Specification

```
Document:    rtl/pio/SPECIFICATION.md
Status:      Active / SSOT
Governs:     rtl/pio/asp_pio_tile.sv, rtl/pio/asp_pio_sm.sv, rtl/pio/asp_pio_tlp_shim.sv, sim/cocotb/test_asp_pio_cocotb.py
Last Update: 2026-10-25
```

---

## 1. Overview & Architecture

The AbstractX Programmable I/O (**ASP-PIO**) engine is a high-speed, synthesizable SystemVerilog hardware subsystem designed to offload physical pin serialization, sensor protocol handshakes, and actuator timing from the main CPUs (AArch64 host, XuanTie E906, and Cortex-M33).

It implements the proven **RP2040 16-bit PIO Instruction Set Architecture (ISA)** augmented with **AbstractX 64-Byte TLP Stream Integration** and **Supervisor-Worker State Machine Chaining**.

```mermaid
flowchart TD
    subgraph Host["Host Interface"]
        WB["Wishbone Bus (0x40000700)"]
        SYS_TIME["64-bit Nanosecond Timebase"]
    end

    subgraph PIO_Tile["ASP-PIO Tile Core"]
        IRAM["Shared Instruction RAM\n(32 Words x 16-bit)"]
        IRQ_BUS["Internal IRQ Bus (irq 0..7)"]

        subgraph SMs["State Machines (4 per Tile)"]
            SM0["SM 0: SPI Worker\n(MOSI/MISO/SCLK)"]
            SM1["SM 1: IMU Supervisor\n(Auto-DMA on DRDY)"]
            SM2["SM 2: DShot Worker\n(Motor Pulses + eRPM)"]
            SM3["SM 3: UART/RC Worker\n(CRSF / SBUS / LED)"]
        end

        TLP_SHIM["TLP Framing & Timestamp Shim"]
    end

    subgraph Fabric["AbstractX System Fabric"]
        ROUTER["asp_router.sv\n(64B TLP Crossbar)"]
        PINS["Physical DIO Pins\n(SPI, DShot, UART, LED)"]
    end

    WB -->|CSRs & Program Load| IRAM
    WB <-->|Manual CPU FIFOs| SMs
    SYS_TIME -->|Timestamp Latch| TLP_SHIM
    IRAM --> SM0 & SM1 & SM2 & SM3
    SM1 <-->|irq 0..7 Handshake| SM0
    SMs <-->|Physical Wiggling| PINS
    SM1 -->|Auto-Push Samples| TLP_SHIM
    TLP_SHIM -->|512-bit AXI-Stream| ROUTER
```

---

## 2. Formal Requirements

### `[SPEC-PIO-01]` Quad-State Machine Tile Architecture
* **Requirement**: The PIO block MUST instantiate four (4) independent, concurrent State Machines (SM0..SM3) sharing a 32-word x 16-bit dual-port instruction memory. Each state machine operates on an independent fractional 16-bit clock divider (`div_int[15:0]`, `div_frac[7:0]`), supporting execution speeds from the full fabric clock (e.g. 100 MHz) down to sub-kHz rates.
* **Verified by**: `cocotb-regression`, `rtl-lint`

### `[SPEC-PIO-02]` Standard RP2040 16-Bit Instruction Set Compatibility
* **Requirement**: Each state machine MUST implement the exact 16-bit RP2040 instruction encoding, supporting all 9 core operations:
  1. `JMP` (`000`): Conditional branch (`!X`, `X--`, `!Y`, `Y--`, `X!=Y`, `PIN`, `!OSRE`).
  2. `WAIT` (`001`): Cycle-exact stall on GPIO pin, mapped pin, or internal IRQ flag.
  3. `IN` (`010`): Shift 1 to 32 bits from source (pins, X, Y, null, ISR, OSR) into ISR.
  4. `OUT` (`011`): Shift 1 to 32 bits from OSR to destination (pins, X, Y, null, pindirs, PC, ISR, exec).
  5. `PUSH` / `PULL` (`100`): Move words between ISR/OSR and 4-word deep RX/TX FIFOs.
  6. `MOV` (`101`): Move/invert/bit-reverse data between X, Y, pins, ISR, OSR, and status.
  7. `IRQ` (`110`): Set, clear, and wait on internal IRQ flags.
  8. `SET` (`111`): Direct immediate 5-bit write to pins, pindirs, X, or Y.
* All instructions MUST support concurrent 5-bit delay and side-set pin driving.
* **Verified by**: `cocotb-regression`

### `[SPEC-PIO-03]` Internal IRQ Bus & Supervisor/Worker State Machine Chaining
* **Requirement**: The tile MUST provide an internal 8-bit IRQ flag register (`irq[7:0]`). State machines can set flags (`irq set N`), clear flags (`irq clear N`), and block until cleared (`irq wait N` / `wait 1 irq N`).
* **Supervisor Pattern**: High-level sequencer state machines (e.g. IMU Auto-DMA) trigger low-level bus transceivers (e.g. SPI Worker) via internal IRQs without invoking the main CPU or generating interrupt storms.
* **Verified by**: `cocotb-regression`

### `[SPEC-PIO-04]` Integrated 64-Byte TLP Framing & Nanosecond Timestamping
* **Requirement**: The PIO tile MUST integrate an autonomous TLP packetizer shim. When a designated state machine pushes a completed sensor burst (e.g. 14 bytes) into its RX FIFO:
  1. The shim latches the 64-bit monotonic system timestamp (`i_sys_timestamp`) sampled at the exact cycle of the triggering event.
  2. The shim frames the payload into a standard 64-byte `Tlp64` datagram (`Type=0x10`, Target Address, Sequence ID, Timestamp, Sensor Payload).
  3. The shim drives `m_tlp_tdata[511:0]` and `m_tlp_tvalid` into `asp_router.sv` via AXI-Stream with zero CPU intervention.
* **Verified by**: `cocotb-regression`, `sanitizer-gate`

### `[SPEC-PIO-05]` Wishbone B4 Slave Bus Interface & Register Map
* **Requirement**: The PIO tile MUST expose a standard Wishbone B4 pipelined slave interface at base address `0x40000700`:
  * `0x00`: Tile Control & Status (`ENABLE[3:0]`, `RESTART[3:0]`).
  * `0x04`: Internal IRQ Status & Clear.
  * `0x10 .. 0x8C`: Instruction Memory (32 words x 32-bit, lower 16 bits active).
  * `0x100 + 0x20 * N`: SM `N` Clock Divider (`INT:16, FRAC:8`).
  * `0x104 + 0x20 * N`: SM `N` Execution Control (`EXECCTRL`: wrap, side-set, jmp_pin).
  * `0x108 + 0x20 * N`: SM `N` Shift Control (`SHIFTCTRL`: autopush, autopull, in/out dir).
  * `0x10C + 0x20 * N`: SM `N` Pin Control (`PINCTRL`: in_base, out_base, set_base, side_base).
  * `0x110 + 0x20 * N`: SM `N` TX FIFO (write).
  * `0x114 + 0x20 * N`: SM `N` RX FIFO (read).
* Software MUST be able to execute manual SPI or UART byte transfers through SM0 TX/RX FIFOs when autonomous sequencing is idle.
* **Verified by**: `cocotb-regression`, `hw-sw-register-parity`

### `[SPEC-PIO-06]` Multi-Rate Primary-Paced IMU Auto-DMA Sequence
* **Requirement**: The PIO tile MUST autonomously ingest high-rate IMU sensors (ICM-42688-P / BMI270) at up to 8 kHz:
  1. Stall on external physical DRDY pin.
  2. Latch nanosecond timestamp.
  3. Command SPI worker to execute multi-register burst (Status $\rightarrow$ Accel $\rightarrow$ Gyro $\rightarrow$ Clear IRQ).
  4. Stream 64B TLP to host ring buffer.
* Total CPU utilization during continuous 8 kHz streaming MUST remain **0.0%**.
* **Verified by**: `cocotb-regression`, `board-bringup`

### `[SPEC-PIO-07]` barectf CTF 1.8 Hardware Observability & Timeline Integration
* **Requirement**: The PIO tile MUST support autonomous binary event tracing compatible with the dynamic barectf CTF 1.8 schema (`trace/barectf_config.yaml`, Stream 5 Timeline Spans). When trace generation is enabled via Wishbone control register:
  1. The hardware packetizer formats compact binary trace spans for SM execution states (Trigger event, Burst active, Stall/Wait, and FIFO Overrun).
  2. Each event embeds `tile_id`, `sm_id`, `event_type`, `pc[4:0]`, and duration in clock cycles, timestamped by the 64-bit nanosecond monotonic clock.
  3. The events flow into the observability plane to display cycle-accurate hardware execution blocks alongside software coroutine lifecycles in the AbstractX Studio Timeline.
* **Verified by**: `cocotb-regression`, `host-tests`

