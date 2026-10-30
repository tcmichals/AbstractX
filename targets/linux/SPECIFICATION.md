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
* **Real-Time Coprocessor Offload (AMP)**: Moving `ioProcessor` to an RP2350 (Pico 2 W Core 0), Allwinner E906 RISC-V, STM32MP1 Cortex-M4, or ESP32-P4 connects physical sensor interrupts directly to bare-metal ISRs with deterministic sub-microsecond latency, streaming TLPs over shared SRAM or mailbox rings to Linux.
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

### `[SPEC-LINUX-SPI-01]` Linux SPI Bridge Request Correlation & Pipelined Completions
The Linux target `LinuxIoProcessor` bridge SPI path forwards manual SPI transactions received via egress TLPs to `SpiWorker`.
* **Goal**: Support pipelined and back-to-back asynchronous SPI requests on the Linux SPI bridge without tag corruption, channel mismatch, or buffer clobbering.
* **Requirement**:
  - `LinuxIoProcessor` MUST maintain an array of in-flight transaction descriptors (`BridgeSpiSlot`, capacity 16 matching `SpiWorker::RequestQueue`).
  - Each descriptor holds its own dedicated TX buffer (64B), dedicated RX buffer (64B), request tag, and channel.
  - The completion callback MUST be bound directly to the descriptor instance via `etl::delegate` so that the completion TLP is constructed with the exact tag, channel, and RX payload corresponding to the request.
  - If all slots are in use or `SpiWorker`'s queue is full, the request MUST be rejected immediately with `ASP_STATUS_QUEUE_FULL` (`0x04`) rather than overwriting in-flight transactions.
  - On completion, the descriptor MUST be returned to the pool for reuse.
* **Refines**: `[SPEC-ARCH-01]`
* **Method**: `Test`
* **Verified by**: `host-tests` (`sim/test_io_processor.cpp`), `sanitizer-gate`, `thread-sanitizer`
* **Implementation Target**: `targets/linux/src/io_processor.cpp`


### 2.2 I2C Subsystem (`i2c-dev` & `I2cWorker`)
* **Interface**: Linux kernel `/dev/i2c-X` via `<linux/i2c-dev.h>` and `<linux/i2c.h>`.
* **Execution**: Dedicated `I2cWorker` background thread.
* **Transfer API**: `ioctl(fd, I2C_RDWR, &rdwr_data)`.
  - **Repeated-Start (Write-then-Read)**: Two messages in one atomic ioctl (`msgs[0]` write register, `msgs[1]` read bytes).
  - **Write Burst**: Single write message with data payload.
  - **Read Burst**: Single read message.
* **Workstation Fallback**: If `/dev/i2c-*` is absent, automatically simulates barometer calibration/pressure bytes.

### `[SPEC-LINUX-HAL-01]` Linux SPI / I2C Driver Completion Delivery and Refused Submits
`LinuxSpiDriver` and `LinuxI2cDriver` (also built for Zynq) are the `ISpi` / `II2c` implementations
on Linux. They hand each request to `SpiWorker` / `I2cWorker`, whose thread runs the ioctl.
* **Goal**: an awaiter sees the real outcome of its own transfer (an ioctl failure is an error,
  never `Ok` with 0 bytes), and a request the worker cannot take never leaves a coroutine waiting.
* **Requirement**:
  - The worker MUST deliver every completion through the owning driver's
    `AsyncDriverBase::push_completion_from_isr` (a completion sink bound by the driver), which
    writes `request.result_slot` before posting the coroutine handle. A worker without a bound
    sink (the I/O processor bridge) MUST still write `result_slot` when it is set before calling
    `notify_completion`.
  - A failing `SPI_IOC_MESSAGE` / `I2C_RDWR` ioctl MUST complete with a non-`Ok` status
    (`SpiStatus::DmaError`, `I2cStatus::NackAddress`) and `transferred_bytes = 0`.
  - `SpiWorker::submit` / `I2cWorker::submit` MUST return `false` when the worker thread is not
    running or its queue is full. The driver MUST then complete that request at once through
    `push_completion_from_isr` with `SpiStatus::QueueFull` / `I2cStatus::QueueFull`.
  - The device path is a constructor argument of the driver class (default `/dev/spidev1.0`,
    `/dev/i2c-1`), and the classes live in `linux_spi_driver.hpp` / `linux_i2c_driver.hpp`, so a
    host test can run the real driver against a device that fails (`/dev/null`).
* **Refines**: `[SPEC-HAL-01]`, `[SPEC-HAL-02]`
* **Method**: `Test`
* **Verified by**: `host-tests` (`sim/test_linux_hal_completion.cpp`), `sanitizer-gate`, `thread-sanitizer`
* **Implementation Target**: `targets/linux/include/spi_worker.hpp`, `targets/linux/include/i2c_worker.hpp`, `targets/linux/include/linux_spi_driver.hpp`, `targets/linux/include/linux_i2c_driver.hpp`

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

### `[SPEC-LINUX-GPIO-01]` GPIO-Edge Auto Channel: Which Line, Which spidev, No Silent Simulation
* **Why**: the Linux A55 column of the sensor latency comparison (`TODO.md` 2a) runs the same
  channel as the E906 bench `A` ([SPEC-E906-BENCH-05]): DRDY edge -> SPI read -> TLP in the
  ingress ring. Until 2026-10-30 the backend opened `/dev/gpiochip0` and `/dev/spidev0.0` whatever
  the channel said, fell back to simulation silently when the line could not be requested, and
  stamped simulated edges with time 0.
* **Requirement**: a `GpioEdge` channel requests line `trigger_pin` on the GPIO chip set with
  `abstractx::target::set_linux_gpiochip(path)` (default `/dev/gpiochip0`). On the Cubie A5E the
  pin is the chip's line offset (PI8 = 264 on the main PIO chip, the same `bank * 32 + n` numbering
  the E906 uses, [SPEC-E906-IRQ-07]).
* **Requirement**: the SPI device is `/dev/spidev<bus_index>.<cs_pin>` of the first SPI channel
  (a bridge transfer that arrives first uses `/dev/spidev<bus_index>.<device>`). One `SpiWorker`
  serves the backend.
* **Requirement**: when the chip opens but the line request fails (pin owned by another consumer,
  bad offset), `init_channel` returns false and `configure()` fails. Simulation is used only when
  libgpiod is not built in or the chip does not exist (host / SITL), and `linux_trigger_simulated()`
  says so.
* **Requirement**: edge timestamps are `CLOCK_MONOTONIC` ns: the kernel's
  `gpiod_edge_event_get_timestamp_ns()` on hardware (monotonic by default), `clock_gettime` for a
  simulated edge. Each simulated edge (`linux_simulate_edge(pin)`, SITL only) is delivered as one
  trigger, never merged with the next.
* **Requirement**: the trigger runs `IoEngine::on_trigger` on the thread that calls `step()` /
  `run()`; the SPI completion runs on the `SpiWorker` thread and only pushes the TLP and calls
  `on_rx_pushed` ([SPEC-LINUX-HAL-01]).
* **Not known yet**: on the Cubie A5E with kernel 7.1, bank-I edges latch `EINT_STA` but never
  reach Linux's GIC ([SPEC-E906-IRQ-07] "Not yet known"), so the hardware column is blocked.
* **Verified by**: `host-tests` (`sim/test_linux_drdy_auto.cpp`), `sanitizer-gate`, `thread-sanitizer`
* **Implementation Target**: `targets/linux/src/io_processor.cpp`, `targets/linux/include/gpio_gpiod.hpp`, `targets/linux/include/linux_io.hpp`

### 2.5 Timer Subsystem (`CLOCK_MONOTONIC` & `timerfd`)
* **Resolution**: Monotonic nanosecond hardware clock (`CLOCK_MONOTONIC`).
* **Asynchronous Delays**: Non-blocking coroutine yields backed by `timerfd` integrated into `epoll`.

### 2.6 AbstractX Reactor: Zero-Heap Asynchronous POSIX Engine (`abstractx::reactor`)

The **AbstractX Reactor** (`abstractx::reactor`) is the standalone, zero-heap event demultiplexer and coroutine engine for Linux and POSIX targets. Rather than pulling in heavy external dependencies like Boost.Asio or spawning thread-per-device worker threads, AbstractX Reactor leverages Linux `epoll` to drive C++20 stackless coroutines across network sockets, inter-core RPMsg-Lite endpoints, FPGA UIO interrupts, and high-resolution timers.

```mermaid
flowchart TD
    subgraph SOURCES["Heterogeneous Linux I/O Descriptors (Zero-Copy)"]
        SOCK["<b>UDP / TCP Sockets</b><br/><code>int sock_fd</code><br/>CTF 1.8 Telemetry • Studio CLI"]
        RPMSG["<b>Linux RPMsg-Lite</b><br/><code>/dev/rpmsgX (rpmsg_char)</code><br/>E906 / Cortex-M Shared SRAM"]
        UIO["<b>Hardware FPGA UIO</b><br/><code>/dev/uioX</code><br/>AXI Crossbar IRQ & DMA"]
        TFD["<b>High-Res Timers</b><br/><code>timerfd / eventfd</code><br/>Nanosecond Flight Deadlines"]
    end

    subgraph REACTOR["AbstractX Reactor (targets/linux/)"]
        EPOLL["<b>EpollReactor (epoll_create1)</b><br/><code>epoll_ctl(EPOLLIN | EPOLLOUT | EPOLLET)</code><br/>Zero Heap • Static <code>std::span&lt;epoll_event&gt;</code>"]
        DISPATCHER["<b>Cooperative Dispatcher (step)</b><br/>Zero thread hops • 15-cycle function pointer jump"]
    end

    subgraph CORO_ENGINE["AbstractX C++20 Coroutine Flight Stack"]
        TASK1["<code>co_await socket.recv_async(span)</code>"]
        TASK2["<code>co_await rpmsg.send_async(tlp)</code>"]
        TASK3["<code>co_await uio.await_irq_async()</code>"]
    end

    SOCK & RPMSG & UIO & TFD -->|Register FD| EPOLL
    EPOLL -->|Non-blocking Wakeup| DISPATCHER
    DISPATCHER -->|15-Cycle <code>.resume()</code>| TASK1 & TASK2 & TASK3
```

### `[SPEC-REACTOR-01]` Zero-Heap C++20 AbstractX Reactor Demultiplexer
The core reactor MUST provide non-blocking event demultiplexing around Linux `epoll_create1(EPOLL_CLOEXEC)`, `epoll_ctl`, and `epoll_wait`.
* **Zero Dynamic Heap**: The reactor engine MUST NOT perform any dynamic memory allocations (`operator new`, `malloc`). All epoll event buffer slices MUST be passed via `std::span<struct epoll_event>`.
* **One-Shot Event Registration**: File descriptor subscriptions MUST use `EPOLLONESHOT` or edge-triggered modes to prevent spurious wakeups and stampeding coroutines.
* **Refines**: `[SPEC-ARCH-01]`
* **Method**: `Test`
* **Verified by**: `host-tests`, `sanitizer-gate`
* **Implementation Target**: `targets/linux/include/abstractx/reactor/epoll_reactor.hpp`

### `[SPEC-REACTOR-02]` Non-Blocking Asynchronous Socket Coroutine Awaiter
The reactor MUST provide zero-heap C++20 coroutine awaitables for UDP and TCP network sockets (`AsyncUdpSocket`).
* **Non-Blocking Operation**: Sockets MUST be configured with `SOCK_NONBLOCK` and `SOCK_CLOEXEC`.
* **Zero-Copy Span API**: Send and receive methods MUST operate directly on caller-provided `std::span<uint8_t>` buffers without intermediate heap buffers.
* **Symmetric Cancellation**: When a socket coroutine is cancelled or times out, the pending descriptor subscription MUST be deregistered cleanly from `epoll` via `EPOLL_CTL_DEL`.
* **Refines**: `[SPEC-ARCH-01]`, `[SPEC-ARCH-08]`
* **Method**: `Test`
* **Verified by**: `host-tests`, `sanitizer-gate`
* **Implementation Target**: `targets/linux/include/abstractx/reactor/async_socket.hpp`

### `[SPEC-REACTOR-03]` Non-Blocking Linux RPMsg-Lite Character Device Awaiter
The reactor MUST provide a C++20 coroutine awaitable (`AsyncRpmsgEndpoint`) for Linux `virtio_rpmsg_bus` character devices (`/dev/rpmsgX` / `/dev/rpmsg_ctrlX`).
* **Hardware Inter-Core Synchronization**: Reading from `/dev/rpmsgX` MUST suspend the coroutine until the Linux kernel virtio interrupt signals `POLLIN`.
* **Zero-Copy 64B TLP Transport**: Directly moves 64-byte `Tlp64` datagrams between user memory and shared SRAM coprocessor mailboxes without intermediate serialization.
* **Refines**: `[SPEC-ARCH-01]`, `[SPEC-ARCH-08]`, `[SPEC-TLP-01]`
* **Method**: `Test`
* **Verified by**: `host-tests`, `sanitizer-gate`
* **Implementation Target**: `targets/linux/include/abstractx/reactor/async_rpmsg.hpp`

### `[SPEC-REACTOR-04]` Non-Blocking Hardware UIO Interrupt Awaiter
The reactor MUST provide a C++20 coroutine awaitable (`AsyncUioDevice`) for FPGA hardware interrupts exposed via `/dev/uioX`.
* **Hardware Interrupt Await**: Coroutines await physical FPGA crossbar switch interrupts via `co_await uio.await_irq_async()`.
* **Zero Polling**: Suspends until the Linux kernel UIO subsystem signals `POLLIN`, unmasking the interrupt vector with zero CPU busy-spinning.
* **Refines**: `[SPEC-ARCH-01]`, `[SPEC-HAL-01]`
* **Method**: `Test`
* **Verified by**: `host-tests`, `sanitizer-gate`
* **Implementation Target**: `targets/linux/include/abstractx/reactor/async_uio.hpp`

---

## 3. Directory Layout

```
targets/linux/
├── CMakeLists.txt              # Builds abstractx_target_linux (alias abstractx_target_host)
├── SPECIFICATION.md            # Authoritative SSOT target contract
├── include/
│   ├── abstractx/reactor/      # Standalone AbstractX Reactor Subsystem
│   │   ├── epoll_reactor.hpp   # Core zero-heap epoll demultiplexer
│   │   ├── async_socket.hpp    # Zero-heap UDP/TCP coroutine socket
│   │   ├── async_rpmsg.hpp     # Zero-copy Linux RPMsg-Lite character endpoint
│   │   └── async_uio.hpp       # FPGA UIO interrupt awaiter
│   ├── eventfd.hpp             # Lightweight C++20 EventFd wrapper
│   ├── epoll_reactor.hpp       # Backward compatibility wrapper
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
