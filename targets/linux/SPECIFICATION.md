# Unified Linux Target Specification (`targets/linux`)

* **Architecture**: POSIX / Linux Character Device Driver Model (ARM64, x86_64, RISC-V Linux)
* **Kernel Interfaces**: `spidev`, `i2c-dev`, `termios2`, `libgpiod v2`, `eventfd`, `epoll`
* **Status**: Complete & Verified

---

## 1. Architectural Overview

The unified Linux target (`targets/linux/`) provides production-ready, POSIX/Linux-native HAL implementations designed for both embedded Linux Single Board Computers (SBCs, such as Allwinner A5E, Raspberry Pi CM4/5, BeagleBone) and workstation desktop SITL simulation.

### The Hybrid Reactor + Dedicated Worker Pattern
On Linux, kernel `/dev/spidev*` and `/dev/i2c-*` drivers are **strictly synchronous blocking `ioctl()` calls** (`SPI_IOC_MESSAGE` and `I2C_RDWR`) that do not support non-blocking `epoll()`. Running them on the reactor thread would stall the event loop for milliseconds. Conversely, `termios2` serial, `libgpiod` v2 GPIO lines, and inter-thread `eventfd` doorbells **natively support `epoll()`**.

AbstractX splits Linux responsibilities cleanly:
1. **The Signaling Plane**: Linux **`epoll`** and **`eventfd`** manage thread sleep and instant wakeups with zero CPU busy-spinning.
2. **The Data Plane**: **ETL (Embedded Template Library)** provides lock-free, zero-heap SPSC rings (`etl::queue_spsc_isr`) and type-safe delegates (`etl::delegate`).
3. **Dedicated Bus Workers**: Independent background worker threads (`SpiWorker`, `I2cWorker`) execute blocking bus `ioctl()`s without contending with or stalling each other.

```
+--------------------------------------------------------------------------------+
|                             Main epoll Reactor                                 |
|                      (Monitors UART, GPIO DRDY, RxEventFd)                     |
+--------------------------------------------------------------------------------+
             |                                              |
      etl::queue_spsc_isr                            etl::queue_spsc_isr
     + spi_eventfd.notify()                         + i2c_eventfd.notify()
             v                                              v
+-----------------------------+                +-----------------------------+
|      SPI Worker Thread      |                |      I2C Worker Thread      |
| Waits on: spi_eventfd       |                | Waits on: i2c_eventfd       |
| Hardware: /dev/spidevX.Y    | (Runs 100% in  | Hardware: /dev/i2c-X        |
| Speed:    10 - 24 MHz       |   parallel)    | Speed:    100 - 400 kHz     |
| Action:   ioctl(SPI_IOC_MSG)|                | Action:   ioctl(I2C_RDWR)   |
+-----------------------------+                +-----------------------------+
             |                                              |
             +----------------------+-----------------------+
                                    |
                    notify_completion() via etl::delegate
                    g_rx_ring.push(Tlp64) + rx_eventfd.notify()
                                    v
+--------------------------------------------------------------------------------+
|                   Main epoll Reactor Wakes Up & Resumes Coro                   |
+--------------------------------------------------------------------------------+
```

### 1.1 Industrial IIO Subsystem Model & Seamless Hardware Offload

`targets/linux/` operates conceptually as a **user-space Industrial I/O (IIO)** subsystem:
1. **Triggered Streaming Data Plane**:
   - Equivalent to Linux `/dev/iio:deviceX` trigger buffers.
   - Hardware events (e.g., `libgpiod` Pin 3 IMU `DRDY` rising edge interrupt) trigger instant autonomous sampling, packaging samples into 64-byte `DMA_Stream` TLPs stamped with 64-bit nanosecond kernel timestamps (`gpiod_edge_event_get_timestamp_ns()`).
2. **Split-Transaction Control Plane**:
   - Equivalent to Linux IIO `sysfs` channel attributes.
   - Non-blocking `MemRd`, `MemWr`, and `IOCTL` TLPs perform runtime sensor configuration (scale, filter cutoff, calibration) and return asynchronous `Cpl`/`CplD` packets without blocking coroutine execution.

#### Seamless Offload to Coprocessor or FPGA
While `targets/linux/` executes this pipeline entirely within Linux user-space via POSIX character devices (`spidev`, `i2c-dev`, `termios2`, `libgpiod` v2), the **Application Coroutine Domain remains 100% decoupled from the physical bus drivers**.

For mission-critical industrial applications where Linux kernel scheduling jitter or CFS latencies cannot be tolerated at high sampling rates (e.g. 1 kHz to 8 kHz IMU loops), **the `ioProcessor` can be relocated to a dedicated real-time coprocessor or FPGA without changing a single line of application code**:
* **Real-Time Coprocessor Offload (AMP)**: Moving `ioProcessor` to an RP2350 (Pico 2 W Core 0), Allwinner E907 RISC-V, STM32MP1 Cortex-M4, or ESP32-P4 connects physical sensor interrupts directly to bare-metal ISRs with deterministic sub-microsecond latency, streaming TLPs over shared SRAM or mailbox rings to Linux.
* **FPGA Hardware Offload**: Moving `ioProcessor` to an FPGA (Zynq, Cyclone V, or PCIe accelerator card) moves bus clocking and timestamp latching into hardware logic (< 10 ns jitter), streaming 64-byte TLPs across AXI DMA or PCIe MSI-X directly into Linux host RAM.

### 1.2 Sensor HW Fusion Engine on Linux

`targets/linux/src/io_processor.cpp` provides the native Linux implementation of the **Sensor HW Fusion API**:
1. **Trigger Handling (`epoll` Integration)**:
   - **Physical ISR Trigger**: `libgpiod` v2 edge detection (`gpiod_line_request_get_fd()`) is registered directly in `epoll_wait()`. Zero background polling threads are required.
   - **Periodic Auto-Trigger**: POSIX `timerfd` can be registered directly in `epoll_wait()` to drive periodic sensor sweeps.
2. **Autonomous ASAP Read & Forward**:
   - On trigger, the reactor latches the nanosecond kernel timestamp (`gpiod_edge_event_get_timestamp_ns()`) and immediately dispatches the transfer to `SpiWorker`.
   - On bus completion, the payload is wrapped into a 64-byte `DMA_Stream` TLP stamped with the kernel timestamp and forwarded immediately into `g_rx_ring`, signaling the coroutine doorbell.
3. **Bus Lockout Invariant (`ASP_STATUS_BUS_LOCKED`)**:
   - When Auto-DMA is armed (`auto_mode == true`), the bound bus is locked exclusively for autonomous streaming.
   - Any manual transfer request (`make_spi_transfer`) arriving on `g_tx_ring` is immediately rejected with completion status `ASP_STATUS_BUS_LOCKED` (`0x05`).
   - The application disarms auto-mode via `make_hw_fusion_control(ch, false)` to unlock the bus for manual register configuration, calibrations, or diagnostics.

---

## 2. Hardware Peripheral Subsystems

### 2.1 SPI Subsystem (`spidev` & `SpiWorker`)
* **Interface**: Linux kernel `/dev/spidevX.Y` via `<linux/spi/spidev.h>`.
* **Execution**: Dedicated `SpiWorker` background thread.
* **Transfer API**: Full-duplex simultaneous TX/RX via `struct spi_ioc_transfer`.
* **Dynamic Speed Control**: `ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz)`.
* **Lock-Free Concurrency**: Dedicated `etl::queue_spsc_isr<SpiRequest, 16>` ensures strict FIFO hardware serialization with zero mutex locks.
* **Workstation Fallback**: If `/dev/spidev*` is absent, automatically simulates sensor responses (e.g. ICM-42688 WHO_AM_I = 0x47) for desktop CTest validation.

### 2.2 I2C Subsystem (`i2c-dev` & `I2cWorker`)
* **Interface**: Linux kernel `/dev/i2c-X` via `<linux/i2c-dev.h>` and `<linux/i2c.h>`.
* **Execution**: Dedicated `I2cWorker` background thread.
* **Transfer API**: `ioctl(fd, I2C_RDWR, &rdwr_data)`.
  - **Repeated-Start (Write-then-Read)**: Two messages in one atomic ioctl (`msgs[0]` write register, `msgs[1]` read bytes).
  - **Write Burst**: Single write message with data payload.
  - **Read Burst**: Single read message.
* **Workstation Fallback**: If `/dev/i2c-*` is absent, automatically simulates barometer calibration/pressure bytes.

### 2.3 Serial / UART Subsystem (`termios2` with `ASYNC_LOW_LATENCY`)
* **Interface**: `/dev/ttyX` (`/dev/ttyS*`, `/dev/ttyUSB*`, `/dev/ttyAMA*`).
* **Arbitrary Baud Rates**: Uses `struct termios2` with the `BOTHER` flag (`<asm/termbits.h>`) to set any arbitrary custom speed (e.g., 921,600 or 1,500,000 baud) with 1-baud precision.
* **Sub-Millisecond Low Latency**: Configures `ASYNC_LOW_LATENCY` via `ioctl(fd, TIOCSSERIAL)` to bypass kernel TTY flip-buffer batching.
* **Non-Blocking Reactor Integration**: Opened with `O_NONBLOCK` and registered directly in `epoll`.

### 2.4 GPIO Subsystem (`libgpiod` v2)
* **Interface**: Modern `libgpiod` v2 API (`<gpiod.h>`).
* **Edge Detection**: Configures hardware edge detection (e.g. Rising edge for Pin 3 IMU DRDY) using `gpiod_line_settings_set_edge_detection()`.
* **Epoll Integration**: Line request file descriptor (`gpiod_line_request_get_fd()`) is added to `epoll`.
* **Nanosecond Timestamps**: On trigger, extracts `gpiod_edge_event_get_timestamp_ns()` for hardware-level nanosecond correlation into `Tlp64::timestamp_ns`.

### 2.5 Timer Subsystem (`CLOCK_MONOTONIC` & `timerfd`)
* **Resolution**: Monotonic nanosecond hardware clock (`CLOCK_MONOTONIC`).
* **Asynchronous Delays**: Non-blocking coroutine yields backed by `timerfd` integrated into `epoll`.

---

## 3. Directory Layout

```
targets/linux/
├── CMakeLists.txt              # Builds abstractx_target_linux (alias abstractx_target_host)
├── SPECIFICATION.md            # Authoritative SSOT target contract
├── include/
│   ├── eventfd.hpp             # Lightweight C++20 EventFd wrapper
│   ├── epoll_reactor.hpp       # Lightweight C++20 EpollReactor wrapper
│   ├── spi_worker.hpp          # Dedicated SPI worker thread
│   ├── i2c_worker.hpp          # Dedicated I2C worker thread
│   └── gpio_gpiod.hpp          # libgpiod v2 edge detection wrapper
└── src/
    ├── hal_spi.cpp             # AsyncSpiDriver implementation
    ├── hal_i2c.cpp             # AsyncI2cDriver implementation
    ├── hal_uart.cpp            # AsyncUartDriver termios2 implementation
    ├── hal_gpio.cpp            # IGpio libgpiod v2 implementation
    ├── hal_timer.cpp           # ITimer monotonic timer implementation
    └── io_processor.cpp        # Linux epoll reactor event loop
```
