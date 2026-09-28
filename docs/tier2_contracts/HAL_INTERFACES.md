# AbstractX Symmetrical HAL Interface Contracts

**Document:** `docs/tier2_contracts/HAL_INTERFACES.md`  
**Status:** Authoritative Foundation (Tier 2 SSOT)  
**Location in Code:** `include/abstractx/hal/`

---

## 1. Zero Blocking Public HAL Mandate

Public HAL interfaces in AbstractX **strictly prohibit synchronous blocking methods** (`transfer_sync()`, `read_sync()`, `write_sync()`) in application code.

All driver transactions are asynchronous C++20 coroutines returning awaitables:
```cpp
// Mandatory Asynchronous Driver API Pattern:
co_await dev.init_async();
co_await dev.read_reg_async(reg);
co_await dev.write_reg_async(reg, value);
co_await dev.next_sample_async();
```

---

## 2. The Core Hardware Abstraction Interfaces

### 2.1 Asynchronous SPI Interface (`ISpi` / `include/abstractx/hal/spi.hpp`)
* **`async_transfer(tx_span, rx_span)`**: Initiates non-blocking DMA burst and returns awaitable task.
* **`async_write_read_reg(reg, val)`**: Writes register address and clocks incoming response.
* **Hardware Doorbell Integration**: Wakes coroutines cooperatively when DMA interrupt fires.

### 2.2 Asynchronous I2C Interface (`II2c` / `include/abstractx/hal/i2c.hpp`)
* **`async_write_read(addr, tx_data, rx_data)`**: Dispatches repeated-start I2C transaction.
* **`async_write(addr, tx_data)`**: Dispatches non-blocking write burst.

### 2.3 Asynchronous UART Interface (`IUart` / `include/abstractx/hal/uart.hpp`)
* **`async_read_packet(rx_buffer)`**: Awaits next framed binary packet (e.g. U-Blox UBX).
* **`async_write(tx_data)`**: Dispatches DMA transmit burst without stalling the caller.

### 2.4 Monotonic Hardware Timer (`ITimer` / `include/abstractx/hal/timer.hpp`)
* **`sleep_async(duration)`**: Yields execution cooperatively; awakens via hardware alarm timer interrupt.
* **`now_ns()`**: Monotonic 64-bit nanosecond timestamp counter.

### 2.5 Inter-Core Mailbox & Doorbell (`IMailbox` / `include/abstractx/hal/mailbox.hpp`)
* **`signal_doorbell(core_id)`**: Rises hardware inter-core interrupt (SIO FIFO on RP2350, IPC Mailbox on ESP32-P4, msgbox on Allwinner A5E).
* **`is_pending()`**: Lock-free status check.
