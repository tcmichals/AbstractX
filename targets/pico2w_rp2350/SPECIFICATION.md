# Raspberry Pi Pico 2 W (RP2350) Target Specification

This document is the **authoritative hardware and BSP specification** for the **Raspberry Pi Pico 2 W (RP2350 SoC)** target (`targets/pico2w_rp2350/`). It provides the exact pin mappings, Pico SDK APIs, DMA channel recipes, and interrupt wiring required for AI agents and developers to implement the target HAL.

---

## 1. Target Overview & Silicon Architecture

* **SoC**: Raspberry Pi RP2350 (Dual ARM Cortex-M33 @ 150 MHz with DSP & FPU)
* **Wireless**: Infineon CYW43439 (Wi-Fi 4 802.11n + Bluetooth 5.2) connected via internal SPI/PIO
* **SRAM**: 520 KB on-chip SRAM in 10 banked stripes (ideal for lock-free multi-core SPSC rings)
* **Flash**: 4 MB QSPI Flash

```mermaid
graph TD
    subgraph Core0["Core 0: Dedicated ioProcessor Domain (PicoIoProcessor)"]
        Core0_BSP["Pico SDK BSP Init"]
        Core0_IOPROC["<b>ioProcessor Message Processing Loop</b><br/>(PicoIoProcessor : IIoProcessor)"]
        Core0_DMA["SPI1 Full-Duplex DMA Master"]
        Core0_I2C["I2C0 Hardware Master"]
        Core0_UART["UART0 DMA / Non-blocking FIFO"]
        Core0_WiFi["CYW43439 Wi-Fi Service (cyw43_arch_poll)"]
        Core0_Doorbell["SIO Doorbell IRQ Handler"]
        Core0_IOPROC --> Core0_DMA
        Core0_IOPROC --> Core0_I2C
        Core0_IOPROC --> Core0_UART
        Core0_IOPROC --> Core0_WiFi
    end

    subgraph Core1["Core 1: Coroutine Flight Domain"]
        Core1_Sched["C++20 Coroutine Dispatcher"]
        Core1_Tasks["gps_imu_app Coroutines<br/>(imu_task, gps_task, EKF)"]
    end

    subgraph SRAM["Shared SRAM (520 KB Banked)"]
        TxRing["g_tx_ring (SpscTlpRing&lt;64&gt;)<br/>Core 1 -> Core 0 (Requests / IOCTL)"]
        RxRing["g_rx_ring (SpscTlpRing&lt;64&gt;)<br/>Core 0 -> Core 1 (Completions & Auto DMA_Stream)"]
        TelemRing["g_telemetry_ring (SpscTlpRing&lt;64&gt;)<br/>Core 1 -> Core 0 (Telemetry TLPs)"]
    end

    subgraph SIO["RP2350 SIO Hardware"]
        DoorbellFIFO["Hardware SIO FIFO (sio_hw->fifo_wr)"]
    end

    Core1_Tasks -->|Pushes Requests| TxRing
    TxRing -->|Pops Requests| Core0_IOPROC
    Core0_DMA -->|Posts Completions & Streams| RxRing
    Core0_I2C -->|Posts Completions| RxRing
    Core0_UART -->|Posts Completions| RxRing
    RxRing -->|Pops TLPs| Core1_Sched
    Core1_Tasks -->|Pushes Telemetry TLPs| TelemRing
    TelemRing -->|Pops & Broadcasts| Core0_WiFi
    Core0_Doorbell <--> DoorbellFIFO
    DoorbellFIFO <--> Core1_Sched

    classDef c0 fill:#0f172a,stroke:#3b82f6,stroke-width:2px,color:#ffffff;
    classDef c1 fill:#1e1b4b,stroke:#818cf8,stroke-width:2px,color:#ffffff;
    classDef sram fill:#14532d,stroke:#22c55e,stroke-width:2px,color:#ffffff;
    classDef sio fill:#7c2d12,stroke:#f97316,stroke-width:2px,color:#ffffff;

    class Core0_BSP,Core0_IOPROC,Core0_DMA,Core0_I2C,Core0_UART,Core0_WiFi,Core0_Doorbell c0;
    class Core1_Sched,Core1_Tasks c1;
    class TxRing,RxRing,TelemRing sram;
    class DoorbellFIFO sio;
```

### 1.1 The Unified `ioProcessor` IP on Core 0 (Replacing `isr_dispatcher`)

Just like the Linux and Allwinner E906 targets, the Raspberry Pi Pico 2 W target adheres to the **exact same `ioProcessor` architecture** (`abstractx::hal::IIoProcessor`).

1. **Rejection of `isr_dispatcher`**: Top-half hardware ISRs on Core 0 (SPI DMA, UART, GPIO DRDY, Timer) **NEVER resume coroutine handles directly**. They do not touch C++20 coroutine state.
2. **Dedicated Message Processing Loop**: Core 0 runs `PicoIoProcessor::run()` or `step()`. It pulls 64-byte request TLPs from `g_tx_ring`, calls into the non-blocking HAL drivers (`hal_spi`, `hal_uart`, `hal_i2c`), and services the CYW43 Wi-Fi stack.
3. **Drivers Post Messages Back**: When drivers complete their asynchronous transfers, they format 64-byte completion TLPs (`CplD`, `Cpl`, or `DMA_Stream`), push them directly into `g_rx_ring`, and write to `sio_hw->fifo_wr = 1` to signal Core 1.
4. **Autonomous Sensor HW Fusion**: In Auto-DMA mode, Pin 3 / GP20 DRDY interrupts directly trigger SPI1 DMA bursts without CPU intervention. Once complete, the DMA ISR pushes the 64-byte `DMA_Stream` TLP and rings the SIO doorbell.
5. **Bus Lockout Invariant**: When Auto-DMA is active, manual coroutine requests to SPI1 are rejected with `ASP_STATUS_BUS_LOCKED` (0x05).
6. **Zero Busy-Waiting**: When idle, Core 0 executes `__wfe()` to save power until the next hardware interrupt arrives.

---

## 2. Hardware Pinout & Peripheral Assignments

The Pico 2 W HAL assigns hardware peripherals to the following GPIO pins:

| Peripheral | Signal | Pico 2 W Pin (GPIO) | Architectural Function |
| :--- | :--- | :--- | :--- |
| **SPI1 (IMU Bus)** | `SPI1_SCK` | **GP10** (Pin 14) | SPI Serial Clock to ICM-42688-P (10–20 MHz) |
| | `SPI1_TX` (MOSI) | **GP11** (Pin 15) | Master Out Slave In |
| | `SPI1_RX` (MISO) | **GP12** (Pin 16) | Master In Slave Out |
| | `SPI1_CS_N` | **GP13** (Pin 17) | Active-Low Hardware Chip Select |
| **IMU Interrupt** | `IMU_DRDY` | **GP20** (Pin 26) / **Pin 3** | Data-Ready Rising Edge Interrupt from IMU |
| **I2C0 (Sensor Bus)**| `I2C0_SDA` | **GP4** (Pin 6) | I2C Serial Data (Barometer / Magnetometer) |
| | `I2C0_SCL` | **GP5** (Pin 7) | I2C Serial Clock (400 kHz Fast Mode) |
| **UART0 (GPS Bus)**| `UART0_TX` | **GP0** (Pin 1) | Transmits configuration commands to GPS |
| | `UART0_RX` | **GP1** (Pin 2) | Receives UBX binary frames (115200–921600 baud) |
| **CYW43 Wi-Fi** | Internal | **Internal WL_REG_ON** | Managed exclusively by `pico_cyw43_arch` on Core 0 |

---

## 3. Pico SDK API Recipe for HAL Implementations

When implementing HAL drivers under `targets/pico2w_rp2350/src/`, AI agents must use standard **Pico C/C++ SDK** APIs:

### 3.1 SPI Master & Full-Duplex DMA Burst (`hal_spi.cpp`)
* **Headers**: `#include "hardware/spi.h"` and `#include "hardware/dma.h"`
* **Initialization**:
  ```c
  spi_init(spi1, config.frequency_hz);
  spi_set_format(spi1, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST); // Mode 3 for ICM-42688-P
  gpio_set_function(10, GPIO_FUNC_SPI); // SCK
  gpio_set_function(11, GPIO_FUNC_SPI); // MOSI
  gpio_set_function(12, GPIO_FUNC_SPI); // MISO
  gpio_init(13);                        // CS
  gpio_set_dir(13, GPIO_OUT);
  gpio_put(13, 1);
  ```
* **Full-Duplex DMA Channel Setup (INAV/Linux Style)**:
  ```c
  int rx_chan = dma_claim_unused_channel(true);
  int tx_chan = dma_claim_unused_channel(true);

  // Configure RX DMA
  dma_channel_config c_rx = dma_channel_get_default_config(rx_chan);
  channel_config_set_transfer_data_size(&c_rx, DMA_SIZE_8);
  channel_config_set_dreq(&c_rx, spi_get_dreq(spi1, false)); // false = RX
  channel_config_set_read_increment(&c_rx, false);
  channel_config_set_write_increment(&c_rx, true);
  dma_channel_configure(rx_chan, &c_rx, rx_buffer, &spi_get_hw(spi1)->dr, len, false);

  // Configure TX DMA
  dma_channel_config c_tx = dma_channel_get_default_config(tx_chan);
  channel_config_set_transfer_data_size(&c_tx, DMA_SIZE_8);
  channel_config_set_dreq(&c_tx, spi_get_dreq(spi1, true)); // true = TX
  channel_config_set_read_increment(&c_tx, true);
  channel_config_set_write_increment(&c_tx, false);
  dma_channel_configure(tx_chan, &c_tx, &spi_get_hw(spi1)->dr, tx_buffer, len, false);

  // Trigger simultaneous transfer
  dma_start_channel_mask((1u << rx_chan) | (1u << tx_chan));
  ```

### 3.2 I2C Repeated-Start Master (`hal_i2c.cpp`)
* **Header**: `#include "hardware/i2c.h"`
* **Initialization**:
  ```c
  i2c_init(i2c0, 400 * 1000); // 400 kHz Fast Mode
  gpio_set_function(4, GPIO_FUNC_I2C);
  gpio_set_function(5, GPIO_FUNC_I2C);
  gpio_pull_up(4);
  gpio_pull_up(5);
  ```
* **Repeated-Start Transfer (`write_read_sync`)**:
  ```c
  // Write register offset without issuing STOP, then read data
  i2c_write_blocking(i2c0, slave_addr, tx_data, tx_len, true);  // true = Repeated Start (no STOP)
  i2c_read_blocking(i2c0, slave_addr, rx_data, rx_len, false);  // false = Emit STOP
  ```

### 3.3 GPIO & Pin Interrupt Controller (`hal_gpio.cpp`)
* **Headers**: `#include "hardware/gpio.h"`, `#include "hardware/irq.h"`, `#include "hardware/structs/sio.h"`
* **Pin Configuration (Output / Input)**:
  ```c
  gpio_init(pin);
  if (mode == PinMode::Output) {
      gpio_set_dir(pin, GPIO_OUT);
  } else {
      gpio_set_dir(pin, GPIO_IN);
      if (pull == PinPull::PullUp) gpio_pull_up(pin);
      else if (pull == PinPull::PullDown) gpio_pull_down(pin);
  }
  ```
* **Single-Cycle Atomic Output Operations (Set, Xor, Clear) via SIO Hardware**:
  RP2350 provides dedicated single-cycle hardware atomic registers in the Single-cycle IO (SIO) block:
  ```c
  // Atomic SET (drive HIGH)
  sio_hw->gpio_set = (1u << pin);

  // Atomic CLEAR (drive LOW)
  sio_hw->gpio_clr = (1u << pin);

  // Atomic XOR (toggle state)
  sio_hw->gpio_togl = (1u << pin);
  ```
* **Attaching Edge ISRs (Positive / Rising, Negative / Falling, Both)**:
  ```c
  uint32_t event_mask = 0;
  if (trigger == EdgeTrigger::Rising)  event_mask = GPIO_IRQ_EDGE_RISE;
  if (trigger == EdgeTrigger::Falling) event_mask = GPIO_IRQ_EDGE_FALL;
  if (trigger == EdgeTrigger::Both)    event_mask = GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL;

  gpio_set_irq_enabled_with_callback(pin, event_mask, true, &target_gpio_irq_handler);
  ```
* **Ingress TLP Event Generation in Top-Half ISR**:
  When an edge occurs, the top-half ISR:
  1. Captures monotonic nanoseconds: `uint64_t ts_ns = time_us_64() * 1000ULL;`
  2. Constructs a 64-byte `ASP_CHANNEL_GPIO_BRIDGE` event TLP (`Tlp64::make_gpio_event(pin, level, edge, ts_ns)`).
  3. Pushes into `ingress_rx_ring`.
  4. Rings SIO FIFO doorbell: `sio_hw->fifo_wr = 0x01;` to wake Core 1.
  5. Zero polling / zero coroutine calls in top-half ISR!

### 3.4 UART Streaming (`hal_uart.cpp`)
* **Header**: `#include "hardware/uart.h"`
* **Initialization**:
  ```c
  uart_init(uart0, baud_rate);
  gpio_set_function(0, GPIO_FUNC_UART); // TX
  gpio_set_function(1, GPIO_FUNC_UART); // RX
  uart_set_hw_flow(uart0, false, false);
  uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
  ```

### 3.5 Monotonic Hardware Timer (`hal_timer.cpp`)
* **Header**: `#include "hardware/timer.h"`
* **Timestamping**:
  ```c
  uint64_t get_time_ns() {
      return time_us_64() * 1000ULL; // Microseconds to Nanoseconds
  }
  ```

### 3.6 Inter-Core Multicore Launch & SIO Doorbell
* **Headers**: `#include "pico/multicore.h"` and `#include "hardware/sync.h"`
* **Launching Core 1**:
  ```c
  multicore_launch_core1(core1_coroutine_entry);
  ```
* **Signaling Inter-Core Doorbell**:
  ```c
  sio_hw->fifo_wr = 0x01; // Non-blocking hardware SIO FIFO write
  ```

### [SPEC-PICO-TRACE-01] Zero-Copy Trace & Telemetry over UDP
The Pico side of `[SPEC-TRACE-07]` (`docs/DESIGN_SPECIFICATION.md`): the trace
packet buffer **is** the lwIP pbuf that gets transmitted, so no memcpy happens
between the trace engine and the Wi-Fi driver.
* **Goal**: zero memory copies between the trace engine and the lwIP UDP network stack on Pico 2 W.
* **acquire**: `pbuf_alloc(PBUF_TRANSPORT, max_bytes, PBUF_POOL)` from the static
  `memp` pool. `PBUF_POOL_BUFSIZE` (lwIP default ≈ `TCP_MSS` + headers ≈ 1.5 KB
  with `TCP_MSS = 1460` in `include/lwipopts.h`) MUST hold one full packet plus
  UDP/IP/link headers, so the payload is a single contiguous pbuf (`p->next == NULL`;
  otherwise treat as acquire failure). Returns `p->payload` as the span.
* **commit**: `pbuf_realloc(p, used)`, `udp_sendto(pcb, p, &dest, 9870)`, `pbuf_free(p)`.
  lwIP holds its own reference until the frame leaves the CYW43 driver.
* **Not allowed**: `PBUF_RAM` (lwIP heap, `MEM_SIZE`) and `PBUF_REF` over a
  static trace buffer (the buffer could be reused while a frame is still queued
  for ARP or the driver). The current `platform_send_telemetry()` uses
  `PBUF_RAM` + `memcpy` and must be replaced.
* **Pool sizing**: `PBUF_POOL_SIZE` (24 today) is shared with RX; reserve at
  least 2 for trace so a full pool drops trace, not network RX. Trace never
  blocks: on acquire failure events are counted as discarded.
* **Context**: `acquire`/`commit` run only on the core that owns lwIP and
  `cyw43_arch_poll()` (`pico_cyw43_arch_lwip_poll` is not thread-safe). Today
  everything runs on Core 0 (Core 1 is not launched yet, see TODO.md).
* **Static Pool Elision**: When `platform_trace_transport()` provides this native
  zero-copy transport, `ABSTRACTX_STATIC_TRACE_PACKETS = 0` elides the fallback
  static trace pool on Pico 2 W (`[SPEC-TRACE-07]`).
* **Refines**: `[SPEC-TRACE-07]`
* **Method**: `Test`
* **Verified by**: `host-tests` (`sim/test_pico_zero_copy_trace.cpp`: pointer identity and single-pbuf invariants), `memory-footprint` (0 `_Unwind_*` symbols in Pico ELF)
* **Implementation Target**: `targets/pico2w_rp2350/include/hal/pico_trace_transport.hpp`, `targets/pico2w_rp2350/src/hal_platform.cpp`, `cmake/toolchains/arm-none-eabi.cmake`

### [SPEC-PICO-HAL-01] SPI / I2C / Timer Completion, SPI Mode and Interrupt-Driven I2C
The Pico side of `[SPEC-HAL-01]` (`docs/DESIGN_SPECIFICATION.md`).
* **Goal**: every async request on Pico 2 W completes exactly once, with its own result, and the
  bus is configured the way the caller asked.
* **Requirement (one start per completion)**: the SPI DMA completion, the I2C interrupt and the
  timer alarm callback complete the finished request with `push_completion_from_isr(active_req_,
  result)` and then call `set_hardware_idle_from_isr()` **only**. That call already pops and starts
  the next queued request; popping and starting another one afterwards overwrites `active_req_`
  and loses a completion.
* **Requirement (SPI mode)**: `PicoSpi::init` programs CPOL/CPHA from `SpiConfig::mode`
  (Mode0 = CPOL0/CPHA0 ... Mode3 = CPOL1/CPHA1) with `spi_set_format`. The RP2350 PL022 only
  shifts MSB first (Pico SDK `spi_set_format` asserts `SPI_MSB_FIRST`), so `lsb_first = true` is
  refused: `init` returns `false` without touching the bus.
* **Requirement (I2C interrupt state machine)**: `PicoI2c` runs an async request entirely from the
  `I2C0_IRQ` handler, which `init` installs once. Start: disable, program `IC_TAR`, enable, unmask
  `TX_EMPTY`, `RX_FULL`, `STOP_DET`, `TX_ABRT` (`IC_TX_TL = IC_RX_TL = 0` from `i2c_init`). The
  handler queues one `IC_DATA_CMD` word per byte: the `tx_data` bytes, then one read command per
  `rx_data` byte (`RESTART` on the first read after a write, `STOP` on the last word), never more
  read commands in flight than the 16-entry RX FIFO holds; it drains `IC_DATA_CMD` into `rx_data`
  on `RX_FULL` and masks `TX_EMPTY` once every word is queued. The request completes on
  `STOP_DET` (status `Ok`) or `TX_ABRT` (`ABRT_7B_ADDR_NOACK` -> `NackAddress`,
  `ABRT_TXDATA_NOACK` -> `NackData`, anything else -> `BusError`, `transferred_bytes = 0`); the
  handler then masks all I2C interrupts, clears `TX_ABRT` / `STOP_DET` and completes as above.
  Register usage follows the Pico SDK `i2c_write_blocking_internal` /
  `i2c_read_blocking_internal` (`src/rp2_common/hardware_i2c/i2c.c`).
* **Not yet covered**: no bus timeout (a slave holding SCL low leaves the request pending); not
  run on hardware.
* **Refines**: `[SPEC-HAL-01]`, `[SPEC-HAL-02]`
* **Method**: `Inspection` (cross-build only; no Pico host test harness or board run yet)
* **Verified by**: `memory-footprint` (Pico cross-build `cmake --build build_pico2w`), `board-bringup` (not run)
* **Implementation Target**: `targets/pico2w_rp2350/src/hal_spi.cpp`, `targets/pico2w_rp2350/src/hal_i2c.cpp`, `targets/pico2w_rp2350/include/abstractx_pico.hpp`

---

## 4. Execution Workflow & Step-by-Step Contract

### [SPEC-PICO-HAL-02] GPIO-Edge Auto Channel: One GPIO Callback, Wired Chip Select Only
* **Why**: the pico-sdk keeps one GPIO interrupt callback per core, and the last
  `gpio_set_irq_enabled_with_callback` wins. Until 2026-10-30 the I/O back end registered its own
  callback for DRDY, so either DRDY or every `PicoGpio` pin interrupt was silently lost. The back
  end also used GP13 for every channel whatever its `cs_pin` said.
* **Requirement**: a `GpioEdge` channel arms its pin through `hal::get_gpio().configure_interrupt()`
  (`PicoGpio`'s per-pin table, the only SDK callback). The handler runs on Core 0, stamps the edge
  with `now_ns()` and calls `IoEngine::on_trigger`; it never resumes a coroutine.
* **Requirement**: `PicoSpi` drives one chip select (GP13, `cs_pin` 0). A SPI channel with another
  `cs_pin` is refused by `init_channel` instead of reading the wrong device.
* **Verified by**: `host-tests` (build only on the host), `board-bringup`
* **Not verified yet**: on a Pico 2 W (no hardware on 2026-10-30).
* **Implementation Target**: `targets/pico2w_rp2350/src/io_processor.cpp`

### 4.1 IMU Auto-Sample Pipeline
1. **ICM-42688-P asserts `DRDY` (GP20 / Pin 3)**:
   - Hardware triggers GPIO interrupt on Core 0.
   - Core 0 ISR latches `uint64_t timestamp_ns = hal::get_timer().get_time_ns()`.
2. **Core 0 Triggers Full-Duplex DMA Burst**:
   - Asserts CS (`GP13 = 0`).
   - Starts simultaneous 15-byte SPI1 TX/RX DMA burst (`TEMP_DATA1` through `GYRO_DATA_Z0`).
3. **DMA Completion ISR**:
   - De-asserts CS (`GP13 = 1`).
   - Formats a 64-byte `DMA_Stream` TLP (`Channel = 0x02`, `Tag = 0x01`, latched `timestamp_ns`, 15-byte payload).
   - Enqueues into `g_sensor_ring.push_from_isr(tlp)`.
   - Writes to SIO FIFO (`sio_hw->fifo_wr = 1`) to awaken Core 1.
4. **Core 1 Awakening & Coroutine Execution**:
   - SIO FIFO interrupt awakens Core 1 from `__wfe()`.
   - Core 1 pops the TLP from `g_sensor_ring`, matches the awaiting coroutine, and resumes `imu_task`.

---

## 5. Build & Target Artifacts

Compiling for Pico 2 W generates the deployable UF2 binary:
```bash
cmake --preset pico2w
cmake --build build_pico2w
# Generates build_pico2w/apps/gps_imu_app/gps_imu_app.uf2
```
To flash, hold the **BOOTSEL** button on the Pico 2 W, connect USB, and copy `gps_imu_app.uf2` to the `RPI-RP2` volume.
