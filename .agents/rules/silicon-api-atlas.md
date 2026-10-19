# Rule: AbstractX Multi-Target Silicon & API Atlas (Zero-Lookup Reference)

This rule provides an instant, authoritative reference for the AbstractX C++20 core API and the hardware memory maps, registers, clocks, pinmuxes, and IRQ vectors across all supported silicon platforms. AI agents MUST use these pre-compiled constants directly without performing repetitive search commands.

---

## 1. AbstractX C++20 Core API Reference

| API Primitive | Signature / Usage | Role & Behavioral Contract |
| :--- | :--- | :--- |
| `abstractx::init(config)` | `void init(const Config &)` | Initializes platform clocks, pinmux, and dispatcher. Called once at boot. |
| `abstractx::step()` | `void step() noexcept` | Paces the event loop: drains `Dispatcher::process()`, steps reactors, flushes trace. Sole caller of `.resume()`. |
| `abstractx::step_async()` | `co_await abstractx::step_async()` | Cooperatively yields execution back to the event loop. |
| `abstractx::spawn(coro)` | `void spawn(coro::Task<void> task)` | Spawns a cooperative coroutine task into the active execution pool. |
| `AsyncQueue<T, N>` | `co_await q.pop()`, `q.try_pop(out)`<br/>`co_await q.push(val)`, `q.try_push(val)` | Zero-heap lock-free cooperative SPSC channel. Embedded static buffer. |
| `DomainDispatcher` | `DomainDispatcher::isr_post_resume(h)` | Lock-free ISR-safe handoff queue (< 100 ns). Pushes handle from interrupt to main loop. |
| `ITraceTransport` | `acquire(max_bytes)`, `commit(buf, used)` | Zero-copy barectf binary CTF trace packet emission over platform egress. |

---

## 2. Allwinner T527 / A527 XuanTie E906 (and A733 E902) Co-Processor

* **Core**: XuanTie E906 (RV32IMAFCX, single-precision FPU, up to 200 MHz, M & U modes, no MMU).
* **Interrupt Controller**: T-Head CLIC at **`0xE0800000`** (per-irq ctrl at `+0x1000 + 4*irq`). Line 7 = Timer, Line 17 = MSGBOX.

### Memory Windows (Linux Host PA vs. E906 Core DA)
| Region Name | E906 Core DA | Host PA | Size | Invariant / Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **SRAM Space 0** (`r_sram`) | `0x3FFC0000` | `0x07280000` | 256 KB | **100% Firmware Pool** (vectors, text, data, bss, stack, trace0). 0 DDR! |
| **SRAM Space 1** (`r_sram1`) | `0x40000000` | `0x072C0000` | 256 KB | VirtIO vrings, message buffers (`cubie-a5e-rpmsg-sram`). |
| **Cadence HiFi4 DSP RAM** | `0x00020000` | `0x00020000` | 128 KB | Quarantined MicroPython GC heap (`gc_init`). |
| **CFG Control Block** | `0x07130000` | `0x07130000` | 4 KB | `0x0204` (`STA_ADD_REG`), `0x0248` (`WORK_MODE_REG`). |

### Peripheral Base Addresses, Clocks & Pinmux
| Subsystem | Base Address | Clock Control (CCU / PRCM) | Pinmux Configuration (PIO Base: `0x02000000`) |
| :--- | :--- | :--- | :--- |
| **UART2** (GPS, 8250) | `0x02500800` | `CCU_UART_BGR_REG` (`0x0200190C`):<br/>Reset: `1 << 18`, Gate: `1 << 2` | `PB_CFG0` (`0x02000030`): PB0 (TX) & PB1 (RX) = Mux 2 (`0x22`).<br/>`PB_PULL0` (`0x02000054`): Pull-up PB1 (`1 << 2`). Header: Pins 11, 13. |
| **TWI1 / I2C1** (Compass) | `0x02502400` | `CCU_TWI_BGR_REG` (`0x0200191C`):<br/>Reset: `1 << 17`, Gate: `1 << 1` | `PB_CFG0`: PB4 (SCL) & PB5 (SDA) = Mux TWI1.<br/>Header: Pin 5 (SCL), Pin 3 (SDA). |
| **SPI0** (High-Speed) | `0x04025000` | `CCU_SPI_BGR_REG` (`0x0200196C`):<br/>Reset: `1 << 16`, Gate: `1 << 0` | Port C: PC2 (MOSI), PC3 (CS0), PC4 (MISO), PC12 (CLK) = Mux 4.<br/>Header: Pins 19, 21, 23, 24, 26. |
| **MSGBOX** (Doorbell) | `0x03003000` | Bank 0 (Host), Bank 1 (E906). FIFO 1. | Inter-core mailbox kicks over Channel 1 (Linux channel 9). |

### VirtIO RPMsg Endpoints
* **1024 (`abstractx-tlp`)**: 64-byte TLPs batched up to 7 per 512B frame (`asp_tlp64_t`).
* **1025 (`abstractx-trace`)**: Zero-copy binary barectf CTF 1.8 trace frames (up to 496 B).
* **1026 (`abstractx-repl`)**: MicroPython interactive REPL text streams (`>>> `).
* **RemoteProc `trace0` (`0x3FFEF08C`, 1 KB)**: Strictly liveness (`[E906] Online`) and crash autopsies (`hal_crash_dispatcher`). Zero general printf.

---

## 3. Raspberry Pi Pico 2 W (RP2350)

* **Architecture**: Dual ARM Cortex-M33 @ 150 MHz + Hardware Single-Precision FPU + PIO state machines.
* **Core Allocation**:
  * **Core 1**: Dedicated C++20 Flight Engine / Coroutine Event Loop (`abstractx::step()`).
  * **Core 0**: High-speed PIO state machines, DMA channels, and lwIP Wi-Fi background stack.
* **Peripherals & Drivers**:
  * PIO SPI0 / SPI1 auto-DMA ring buffers.
  * Hardware Spinlocks (SIO `SPINLOCK0`..`SPINLOCK31`) for zero-wait cross-core doorbells.
  * Static Scratchpad RAM (`0x20080000`) for lock-free SPSC inter-core queues.

---

## 4. Espressif ESP32-P4

* **Architecture**: Dual RISC-V RV32IMAFDC @ 400 MHz + Single-Precision FPU + GDMA + AI extensions.
* **Core Allocation**:
  * **Core 1**: Real-time C++20 coroutines and flight control loop.
  * **Core 0**: General DMA (GDMA), crypto hardware accelerator, and Wi-Fi 6 / Bluetooth 5 companion.
* **Peripherals & Drivers**:
  * High-speed SPI, HP-I2C, HP-UART using GDMA link lists.
  * Internal L2 Scratchpad Memory (Zero-wait-state SRAM) for SPSC queues and coroutine pools.

---

## 5. QMTECH / Gowin / Zynq FPGA (Hardware X-Fabric)

* **Switch Fabric**: `asp_router.sv` — Full 64-byte TLP crossbar switch routing frames across CPU bus and DIO lines.
* **Bus Interfaces**:
  * **Data Plane**: AXI-Stream (`tdata[63:0]`, `tvalid`, `tready`, `tlast`).
  * **Control Plane**: Wishbone B4 pipelined (`cyc`, `stb`, `we`, `adr[31:0]`, `dat[31:0]`, `ack`).
  * **TLP Framing**: Strict 64-byte framing (`asp_tlp64_t`, `alignas(64)`), exactly 40-byte payload.
