# AbstractX System Review — 2026-10-22

**What this is:** a read-only review of the whole AbstractX repository at commit `7bcfa01`. It covers the HAL and bus drivers on every target, the coroutine runtime, the processor framework and IPC, the FPGA fabric, motor safety, platform/boot/memory, trace, apps, build/test/CI, how honest the specs are, and AbstractX Studio.

**What to do about it:** [`2026-10-22-handoff-todo.md`](2026-10-22-handoff-todo.md) holds the prioritized task list and the design decisions for the fixes.

**Where to find things:** shared bus APIs and transfer types §3; **each target** §4 (E906, Pico 2 W, ESP32-P4, Zynq/Linux, host/SITL); **coroutines, scheduling and channels** §5; Betaflight/INAV API parity and the compatibility requirement §15 (§15.4); the `../inav` (inav-abstractx) consumer check §16.

**How it was done:**
- Nine parallel read-only reviews, plus direct reading of the bus drivers and HAL headers.
- Nothing was built, simulated, synthesized or run on hardware. The only commands run were `tools/audit_specs.py` and `tools/spectrace_matrix.py`.
- Bit positions, FIFO depths and DRQ numbers were not checked against the T527, RP2350, ESP32-P4 or Zynq reference manuals.

**Legend:**
- **[V]**: checked directly while writing this review.
- **[R]**: reported by a review pass that read the code. Not independently re-checked.
- **[I]**: inferred, not confirmed in code or on hardware.
- **Severity**:
  - **Critical**: can injure people or damage hardware, or can be exploited from the host.
  - **High**: hangs, corrupts data, or leaves a feature broken.
  - **Med**: wrong behaviour in some cases.
  - **Low**: cleanup.

---

## 1. Executive summary

AbstractX has a good architecture with an unfinished implementation, and the documentation claims more than the code does.

The architecture holds up:
- zero-heap C++20 coroutines;
- ISR → SPSC ring → single event loop;
- primary-paced channels;
- 64-byte TLP framing;
- spec-first traceability.

The redesign in the handoff keeps all of these. The problems come from building too many things at once. Five targets, three buses each, an FPGA fabric, tracing, a GUI and MicroPython were all started together, and very few were driven end to end. The parts that were driven end to end do work: the E906 UART3/TWI1 drivers in `e906_periph.cpp`, the E906 idle path, the RPMsg vring code, and the newer Studio CTF decoders.

| Area | Verdict |
|---|---|
| **Motor safety** | **Unsafe.** No arming, no link-loss timeout, no watchdog, no clamp. Three register maps disagree. |
| Shared HAL API | Sound awaiter contract. No in-flight ownership, five completion paths, no devices, no transaction lists. UART has no async RX. |
| Bus drivers (E906 / Pico / P4 / Zynq) | No target has working async SPI, I2C and UART through `ISpi`/`II2c`/`IUart`. |
| Coroutine runtime | Works only for simple single-core FIFO use. `yield`, `sleep_ms`, `delay_*` and `wait_until` never wake up. `when_all`/`when_any` have a use-after-free. No priorities, no cancellation. |
| Processor framework / IPC | Four copies of one host-input parser with no length checks. One critical out-of-bounds read. Host TLPs never reach the E906 io_processor. Byte order disagrees between C++/Python and the RTL. |
| FPGA fabric | The TLP egress path loses and duplicates packets. Wishbone delivers each read completion twice. AXI-Stream rules are broken. Test coverage is thin. |
| Platform / boot (E906) | Trap and CLIC setup are consistent. No stack guard, PMP is never enabled, no watchdog, the crash dump doesn't fit in trace0, and there is a wfi race. |
| Trace | Decoders match the structs and are tested from C++ to Python. The TLP endpoint can still flood. The schema files are stale. |
| Apps / fusion | Not paced by DRDY. Wrong `dt` source. Fusion maths bugs. An E906 main corrupts the live remoteproc resource table. |
| Build / CI | All targets configure except ESP32-P4. CI runs host + ASan only. No cocotb in ctest. Pico links full newlib. |
| Spec honesty | `audit_specs.py` prints 100 %, but the matrix shows **28/190 complete (14.7 %)**, 100 STALE [V]. Many README numbers were never measured. |
| Studio | The newer decoders are good. The older code hardcodes byte offsets, shows "CRC OK" without checking a CRC, puts transport controls in the USER window, and is a 4011-line file. |
| Flight-controller parity (Betaflight/INAV) | Missing: no bus-device registry, segment callbacks, serial port functions, RC receivers, motor device, arming/failsafe, task rates/budgets, resource ownership, parameter groups or MSP/MAVLink (§15). |
| `inav-abstractx` (`../inav`) | Uses **none** of the AbstractX runtime/HAL; has forked the TLP, ring and BAR headers into wire-incompatible copies; re-implemented coroutines, bus, scheduler, config and MSP (§16). |

### Top 15 findings, most severe first

| # | Sev | Finding | Ref |
|---|---|---|---|
| 1 | Critical | Motors keep the last throttle forever when the host, link, E906 or Linux dies. There is no timeout, arming or watchdog in the fabric. | §2 [V] |
| 2 | Critical | Three motor register maps disagree (RTL, `pcie_bar_map.hpp`, `tools/test_asp_pcie.py`). Commanding "motor 1" writes the enable mask, and "motor 4" drives channel 0. | §2 [V] |
| 3 | Critical | E906, Pico and Linux io_processors use the host-supplied `tx_len` as `header_skip` without clamping it. The result is an out-of-bounds read of `auto_rx_buf_`, which is sent back to the host. | §7 [V] |
| 4 | High | Driver completions go to the wrong request: E906 completes the next queued one, Pico starts two, Zynq never writes `result_slot`. | §4 [V] |
| 5 | High | `YieldAwaiter` never re-queues the coroutine, and nothing calls `TimerService::tick()`. So `yield()` and `sleep_ms()` never wake up. | §5 [V] |
| 6 | High | No E906 target-HAL interrupt is routed: TWI writes a PLIC that doesn't exist, DMA line 88 is never enabled, `Pio::init` is never called. The target SPI, I2C, UART and DMA drivers have never worked. | §4.1 [V] |
| 7 | High | Unclamped `rx_len` (a `uint8_t` from the host) into a 64-byte buffer in the E906 and Pico SPI bridges. GPIO `1U<<pin` with `pin` up to 255. | §7 [R] |
| 8 | High | RPMsg `notify_host`/`reply` spin with no limit when Linux stops draining, which hangs the E906 inside `step()`. | §7 [R] |
| 9 | High | TLP byte order: C++ and Python use little-endian, the RTL reads `target_address` big-endian. CRC byte order also differs between tests and RTL. | §7 [R] |
| 10 | High | The FPGA SPI front end consumes TLPs it never sends. The router breaks AXI-Stream stability. Wishbone delivers each read completion twice. | §8 [R] |
| 11 | High | `gps_imu_app` E906 main writes uptime to `0x40000004`, which is the live remoteproc resource table. It also writes DDR. | §11 [R] |
| 12 | High | The IMU isn't paced by DRDY. `dt` comes from pop time, not the sample timestamp. The DRDY ring has no consumer. | §11 [R] |
| 13 | High | `when_all`/`when_any` resume inline and read a freed frame. The `when_any` loser is destroyed while its handle can still be queued. | §5 [R] |
| 14 | High | Zynq software uses the `asp_axi_dma` register map, but the top instantiates `asp_axi_lite_bridge`. The TLP path spins at 100 % CPU and discards every packet. | §4.4 [V] |
| 15 | High | Errors are reported as success everywhere: register reads return a bare `uint8_t`, `init_async` ignores WHO_AM_I, the Zynq workers silently fall back to mock sensors, and Studio shows "CRC OK". | §3, §4, §14 |

### Root causes

1. **The traceability gate measures tags, not behaviour.** `audit_specs.py` reads one spec file and reports 100 %. Code and Markdown were marked "Complete" and "Verified" without a test that runs the path.
2. **No shared lifecycle.** Every target reimplemented request ownership, completion and wake-up, and got them wrong in different ways.
3. **Sync and async APIs mixed.** Sync methods sit in the interfaces, so blocking calls ended up on the event loop.
4. **Breadth before depth.** No target is solid. Each finding was repeated across 3–4 targets.
5. **Too few board and integration tests.** Unit tests complete requests inline, call `.resume()` by hand, or never call the combinators at all.

---

## 2. Motor and actuator safety — Critical

Source: `rtl/motor/asp_dshot_core.sv`, `include/pcie_bar_map.hpp`, `tools/test_asp_pcie.py`, `rtl/pwm/asp_pwm_decoder.sv`, board constraints.

| Scenario | Today | Should be |
|---|---|---|
| Reset or power-up | `reg_enable=0`, pins driven low [R] | OK |
| FPGA reconfiguration | Motor pins float with the Gowin default pull. No `PULL_MODE` is set (`tang9k.cst:40`, `primer20k.cst:41`) [R]. Default pull is probably up [I]. | `PULL_MODE=DOWN` or an external pull-down |
| Host or SPI link stops | Last throttle repeats at 1 kHz forever (`asp_dshot_core.sv:76-108,133-159`) [V: no timeout/watchdog/arm logic] | Fabric timeout of about 50–100 ms forces zero throttle, then disarms |
| E906 crash, remoteproc stop, Linux death | Same: nothing in the fabric notices [R] | Same timeout, fed by a heartbeat register |
| Watchdog | None in the fabric [R] | Hardware watchdog in the DShot core |
| Out-of-range throttle | Not clamped; raw 11 bits are sent [R] | Clamp to 0 or 48..2047 |
| A malformed or stray TLP | One MemWr burst of length 8 at `0x200` sets the enable mask and all throttles [R] | Arm key or sequence register; throttle must be zero when arming |
| DShot special commands (direction, 3D, save) | Reachable whenever enabled; no gate [R] | Only while disarmed, through a dedicated command register |
| Unmapped Wishbone address | Never acknowledged, so the control plane wedges and a later "throttle 0" never runs [R] | Wishbone timeout that returns an error completion |

**Register maps disagree [V]:**

| Offset | RTL | `pcie_bar_map.hpp` | `test_asp_pcie.py` |
|---|---|---|---|
| `0x00` | ctrl | Control | `REG_MOTOR_CTRL` (`0x200`) |
| `0x04` | **enable mask** | Status | **CH1 throttle** |
| `0x08` | — | Motor1 | CH2 |
| `0x10` | **throttle ch0** | Motor3 | **CH4** |

Other findings:
- `reg_ctrl` (protocol select) is stored but never used; output is always DShot600 [R].
- The PWM decoder detects loss of signal only when the line is stuck low, but the pins are pulled up, so a disconnected receiver keeps the last pulse width [R].
- The PWM decoder edge detector uses the first synchroniser flop `sync[0]`, so the input is effectively single-flop synchronised [R].
- No spec requirement covers arming or failsafe [R].
- No C++ code writes the motor registers today [R], so the exposure is the Python tool and any future code.

---

## 3. Shared HAL API (`include/abstractx/hal/*`)

**What's good [R]:**
- Awaiters use their own `result_slot`.
- `await_suspend` returns `false` when the queue is full.
- The result is written before the coroutine is posted.
- The register awaiters keep their buffers in the coroutine frame.
- No heap and no forbidden headers in the HAL headers.

**What's wrong:**

| Sev | Finding | Where |
|---|---|---|
| High | The base class doesn't own the in-flight request. Each target tracks it, and three got it wrong (§4). | `async_driver.hpp` [V] |
| High | `Icm42688p::init_async` ignores WHO_AM_I and every write result, and always returns `true`. | `icm42688p.hpp:89-101` [R] |
| Med | Register reads return a bare `uint8_t`, so NACK, DMA error and a full queue all read as `0x00`. | `spi.hpp:165`, `i2c.hpp` [V] |
| Med | Five completion paths (`callback`, `on_tx_complete`/`on_rx_complete`, `coro_handle`, `result_slot`, output queue). Nothing calls `pop_completion()`, so the output queue fills after 16 and then drops silently. | `async_driver.hpp:47-54,129-142` [V] |
| Med | The return value of `IsrDispatcher::post` is ignored, so a full dispatcher queue loses the resume. | `async_driver.hpp:47-54` [R] |
| Med | No cancel. A destroyed task leaves queued spans and `result_slot` pointing into a freed frame. | [R] |
| Med | `start_hardware_transfer_from_isr` runs under `InterruptGuard`. On hosted builds that is one global spinlock; on bare metal it masks only the local core. | `async_driver.hpp:161-170`, `interrupt_lock.hpp` [R] |
| Med | `next_sample_async` doesn't wait for DRDY, so the "primary-paced" loop runs at bus speed. | `icm42688p.hpp:160-208` [R] |
| Med | Sync methods are part of the interfaces: I2C `*_sync` (pure virtual), UART `read`/`write`/`flush`, `ITimer::delay_*`. Drivers call sync `init()` from inside coroutines (§4 of AGENTS). | headers [V] |
| Low | `reset_queues()` doesn't take the lock. `use_register` plus `tx={reg}` would send the register twice. QMC5883L comment says 100 Hz, but it configures 200 Hz. `ublox_gps` has an unsynchronised `pending_handle_` and a zero-length UBX frame is mis-parsed. | [R] |

### 3.1 The SPI API today

```cpp
using ISpi = AsyncSpiDriver<16>;                     // queue of 16 SpiRequests, copied by value
co_await spi.transfer_async(tx_span, rx_span);       // -> SpiResult
co_await spi.read_reg_async(reg, 0x80);              // -> uint8_t (status lost)
co_await spi.write_reg_async(reg, val, 0x7F);        // -> uint8_t
co_await spi.transfer_reg_async(cmd, val);
co_await spi.transfer_dual_async(tx_a, tx_b, rx_a, rx_b);  // "dual" = two parallel links, NOT dual-I/O
spi.init(SpiConfig{freq, mode, lsb_first, use_dma, dual_spi});   // bus-wide, once
spi.select(bool);                                    // immediate, not queued
```
Problems with this shape:
- `SpiRequest` carries `cs_pin`/`auto_cs`, but no awaiter sets them, and every target ignores them (E906 always uses CS1, Pico GP13).
- `SpiConfig` is applied once to the whole bus, and the E906/Pico targets ignore mode and `lsb_first` (Pico hard-codes Mode 3).
- `transfer_dual_async` adds two spans and a bool to every request, for a feature only the Linux target touches.

### 3.2 SPI transfer types: who needs them, and what exists

| # | Transfer type | Who needs it | Today |
|---|---|---|---|
| 1 | Full-duplex exchange | anything | ✅ `transfer_async(tx, rx)` |
| 2 | Write only | display data, DACs, LED strips | ✅ (`rx` empty) |
| 3 | Read only, clocking a fill byte | ADCs, FIFO drains | ⚠️ works with `tx` empty, but the fill byte isn't configurable (E906 0xFF, Pico unknown) |
| 4 | Command then read N (half duplex) | IMU burst, register reads | ⚠️ only by padding `tx` to the full length (the ICM driver sends 15 B of tx to read 14 B; a 4 KB flash read would need a 4 KB tx buffer) |
| 5 | Command + address + dummy + data | SPI flash, FRAM, PSRAM | ❌ |
| 6 | Several segments with CS held | flash write-enable then program, scatter-gather | ❌ no segment list; `select()` is immediate, not queued |
| 7 | Dual/quad/octal data lines (1-1-2, 1-2-2, 1-4-4, QPI) | flash, PSRAM | ❌ |
| 8 | 3-wire (shared bidirectional data line) | some IMUs, magnetometers, displays | ❌ |
| 9 | Word sizes other than 8 (9, 16, 24, 32); LSB first per device | ST7789 3-wire 9-bit, DACs, ADCs | ❌ bytes only; `lsb_first` bus-wide and ignored |
| 10 | Data/command GPIO between segments | every SPI display | ❌ |
| 11 | Per-device settings (mode, speed, CS polarity) | IMU (mode 3, 24 MHz) next to baro, flash, FPGA | ❌ bus-wide at `init()` |
| 12 | CS setup/hold, gap between words, speed per segment | SD init at 400 kHz, slow ADCs, the zero CS hold in `asp_imu_auto_dma` | ❌ |
| 13 | Clocks with CS deasserted | SD card power-up (74+ clocks) | ❌ |
| 14 | Bus lock across several transactions | flash program, read-modify-write | ❌ |
| 15 | Poll until a status bit clears | flash write-in-progress, ADC ready | ❌ each driver writes its own loop |
| 16 | Continuous or double-buffered streaming (cyclic DMA) | ADC sampling, LED strips, FPGA TLP link | ❌ |
| 17 | Triggered by a pin or timer (DRDY → read) | the 8 kHz IMU | ⚠️ only as `AutoChannelConfig` in the io_processor; a second DMA owner; dead or broken on all four targets |
| 18 | SPI target (slave) mode | Pico or MCU as the FPGA or host link | ❌ (only the FPGA `asp_spi_frontend`) |
| 19 | Error, timeout, cancel per transfer | everything | ❌ `Timeout`/`DmaError` exist but nothing produces them |

### 3.3 SPI hardware capability per target

From memory; **verify against the TRMs** (handoff P5-10).

| Capability | Linux spidev (Zynq/host) | T527 SPI (E906) | RP2350 PL022 | ESP32-P4 GPSPI | Zynq PL engine |
|---|---|---|---|---|---|
| Segments with CS held | ✅ `cs_change` | ✅ manual CS | ✅ GPIO CS | ✅ | IMU only |
| Command/address/dummy | via segments | ✅ separate TX and total counts (`MBC`/`MTC`/`BCC`) | software | ✅ hardware phases | ❌ |
| Dual/quad | ✅ `tx_nbits`/`rx_nbits` | ✅ | ❌ (PIO can) | dual/quad/octal ✅ | ❌ |
| 3-wire | ✅ `SPI_3WIRE` | unknown | ❌ (PIO can) | ✅ | ❌ |
| Word size | 1–32 | 8 | 4–16 | 1–32 | 8 |
| Speed per segment | ✅ | ✅ | ✅ | ✅ | register |

The hardware covers most transfer types; the API is what limits them. Linux spidev's `spi_ioc_transfer` already supports types 1–9 and 11–13, so on Zynq and host the API is the only obstacle.

### 3.4 The I2C API today

```cpp
using II2c = AsyncI2cDriver<16>;
co_await i2c.transfer_async(addr, tx, rx);    // write-then-read inferred from tx AND rx non-empty
co_await i2c.read_reg_async(addr, reg);       // -> uint8_t (status lost)
co_await i2c.write_reg_async(addr, reg, val); // -> I2cResult  (inconsistent with read)
bool write_read_sync(...); bool write_sync(...); bool read_sync(...);   // pure virtual, mandatory
struct I2cConfig { uint32_t frequency_hz; I2cSpeed speed; bool ten_bit_addr; bool use_dma; };
struct I2cRequest { uint8_t slave_addr; bool is_read, repeated_start; uint8_t register_offset;
                    bool use_register; std::span tx_data, rx_data; on_tx_complete; on_rx_complete; };
```
Problems with this shape:
- `frequency_hz` and `speed` describe the same thing.
- `ten_bit_addr` is never read, the address is a `uint8_t`, and E906 masks it with `& 0x7F`.
- `use_register` and `register_offset` duplicate `tx_data = {reg}`, so a target that honoured both would send the register twice.
- I2C has no `[SPEC-HAL-*]` requirement.

### 3.5 I2C transfer types: who needs them, and what exists

| # | Transfer type | Who needs it | Today |
|---|---|---|---|
| 1 | Write N bytes | config writes | ✅ `transfer_async(addr, tx, {})` |
| 2 | Read N bytes | FIFO-style devices | ✅ |
| 3 | Write register address, repeated START, read | almost every sensor | ⚠️ only implied by both spans being non-empty; the E906 DMA path drops the read; the Pico async path is a stub |
| 4 | Register address and data from two buffers in one write (no restart) | block writes without copying | ❌ needs a copy into one buffer, which breaks [SPEC-ARCH-08] |
| 5 | Any message list (write, restart, write, restart, read) | multi-phase devices, EEPROM paging | ❌ |
| 6 | Multi-byte register read or write | IMU and mag bursts, calibration blocks | ❌ `read_reg_async` returns 1 byte |
| 7 | 16-bit register addresses and endianness | EEPROM 24C32+, many sensors | ❌ |
| 8 | 10-bit addressing | some sensors | ❌ |
| 9 | Zero-length write (address probe, ACK polling) | bus scan, waiting out an EEPROM write cycle | ❌ [I: empty transfer treated as already done] |
| 10 | SMBus: word, block read (length in first byte), block write, process call, PEC | batteries, PMICs, fuel gauges | ❌ block read needs a decision per byte |
| 11 | General call, software reset, ignore-NACK, no-START flags | device reset, quirky chips | ❌ |
| 12 | Per-device speed (100 kHz next to 400 kHz) | mixed buses | ❌ bus-wide |
| 13 | Clock-stretch limit and timeout | slow devices, SMBus 25–35 ms | ❌ `Timeout` status never produced |
| 14 | Bus recovery (9 SCL pulses + STOP when SDA is stuck) | after a reset mid-transfer | ❌ |
| 15 | Accurate errors: NACK on address vs data, arbitration lost, which byte | diagnosis, retries | ⚠️ `NackAddress`/`NackData` exist; targets report only `BusError`; the register helpers drop status |
| 16 | Bus lock across transactions | read-modify-write, multi-step init | ❌ |
| 17 | Mux channel select (TCA9548A) | duplicate addresses | ❌ |
| 18 | Read triggered by timer or pin | periodic mag/baro polling | ⚠️ io_processor `AutoChannelConfig` only; Pico timer trigger not implemented |
| 19 | I2C target (slave) mode | MCU as a peripheral to a host | ❌ |
| 20 | Async-only API | AGENTS §4 | ❌ three sync methods are pure virtual |

### 3.6 I2C hardware capability per target

From memory; **verify**.
- **Linux `I2C_RDWR`:** types 3–5, 8, 10, 11 (message lists, `I2C_M_TEN`, `I2C_M_RECV_LEN`, `I2C_M_NOSTART`, `I2C_M_IGNORE_NAK`).
- **ESP32-P4:** builds transfers from a hardware command list, which maps closely to a message list.
- **RP2350 (DesignWare) and Zynq PS (Cadence):** 10-bit addressing, repeated start, target mode.
- **T527 TWI:** a driver engine that sends the register address and data in one engine-run transfer (`TWI_DRV_FMT`).

The hardware covers most types; the API is what's missing.

### 3.7 The UART API today

```cpp
using IUart = AsyncUartDriver<32>;      // AsyncDriverBase<UartTxRequest,...>: TX only
virtual void   init(uint32_t baudrate) = 0;           // void, baud only
virtual void   write_byte(uint8_t) = 0;  virtual void puts(const char*) = 0;
virtual size_t write(std::span<const uint8_t>) = 0;   // sync
virtual bool   read_byte(uint8_t&) = 0;  virtual size_t read(std::span<uint8_t>) = 0;   // sync poll = the only RX
virtual void   flush() = 0;                           // sync
virtual bool   set_baud_rate(uint32_t) { return true; }  // default no-op claims success
co_await uart.write_async(span);  co_await uart.write_byte_async(b);   // completion = queued
struct UartRxRequest { destination_buffer; min_bytes; timeout_ms; delimiter_enabled; delimiter_char; };  // never used
```
Request-based RX (`UartRxRequest`) is the wrong model for UART, because bytes arrive whether or not a request is waiting. Anything that arrives between two requests is lost. Every framework that handles this well uses an always-on RX stream (§3 of the handoff, D3).

### 3.8 UART transfer types: who needs them, and what exists

| # | Transfer type | Who needs it | Today |
|---|---|---|---|
| 1 | Send a buffer or a byte | everything | ✅ `write_async`, `write_byte_async` |
| 2 | Send with "really sent" completion (TEMT) | RS-485 direction, half duplex, before a baud change | ❌ completion means queued; Pico reports Ok on enqueue and drops bytes silently when the ring is full |
| 3 | Continuous RX into a ring, no byte lost between reads | GPS, CLI, radio | ⚠️ E906 and Pico have rings; the interface exposes only sync `read_byte`; E906 interrupt not routed |
| 4 | Receive exactly N | binary headers, fixed frames | ❌ |
| 5 | Receive until delimiter | NMEA, CLI | ❌ fields exist in `UartRxRequest`, unused |
| 6 | Receive until line idle | UBX, Modbus RTU (3.5 char), CRSF | ❌ E906 code uses the RX-timeout interrupt, not exposed |
| 7 | Receive with timeout | everything | ❌ |
| 8 | Awaitable "data available" | coroutine consumers | ❌ |
| 9 | Data bits, parity, stop bits | SBUS (8E2), Modbus (8E1) | ❌ baud only |
| 10 | Inverted TX/RX | SBUS receivers | ❌ |
| 11 | Half duplex on one wire | SmartPort, ESC telemetry, Dynamixel | ❌ |
| 12 | RS-485 driver-enable pin | industrial buses | ❌ |
| 13 | RTS/CTS or XON/XOFF | high-baud radios, modems | ❌ |
| 14 | Send or detect break | LIN, DMX512, resync | ❌ |
| 15 | 9-bit multidrop | multidrop buses | ❌ |
| 16 | Runtime baud change | u-blox 9600 → 115200 | ⚠️ default no-op returns `true` |
| 17 | Error reporting (framing, parity, overrun, ring overflow) | integrity | ⚠️ enum only; Pico and E906 drop silently |
| 18 | Timestamp of the first RX byte | GPS time alignment | ❌ no RX results |
| 19 | Async drain or flush | before baud change or sleep | ❌ sync; no-op on Pico |
| 20 | Several UARTs (GPS + RC + telemetry + CLI) | a real flight stack | ❌ single `get_uart_driver()` |
| 21 | Framing helpers (COBS, SLIP, length-prefixed) | binary links | ❌ the UBX driver parses everything itself |

### 3.9 UART hardware capability per target

From memory; **verify**.
- **Linux termios:** parity, stop bits, `CRTSCTS`, `tcsendbreak`, `TIOCSRS485`.
- **RP2350 PL011:** 5–8 data bits, parity, 1/2 stop bits, RTS/CTS, break; signal inversion through the GPIO pad overrides; 32 B FIFOs.
- **ESP32-P4:** inversion, RS-485 modes, idle detect, character-pattern detect (a good fit for delimiters).
- **T527 and Zynq PS (16550/Cadence-style):** parity, stop bits, break, flow control, RX timeout. Some sunxi UARTs have an RS-485 mode.

### 3.10 Consistency across the three buses

| Aspect | SPI | I2C | UART |
|---|---|---|---|
| `init` | `bool init(SpiConfig)` | `bool init(I2cConfig)` | `void init(baud)` |
| Queue depth | 16 | 16 | 32 |
| Register helpers | `uint8_t` read and write | `uint8_t` read, `I2cResult` write | — |
| Sync methods | none (removed) | 3 pure virtual | primary RX path |
| tx/rx callbacks | yes | yes | no |
| Timeout input | none | none | `timeout_ms` in the unused RX request |
| Instances | one | one | one |
| Spec | [SPEC-HAL-02] | **none**, no `@impl` | [SPEC-HAL-03] (promises non-blocking RX that doesn't exist) |

Each bus has its own status enum, and all three have `Timeout` with no deadline input. Unsupported operations return `true` (`set_frequency`, `set_baud_rate`, `select` on stubs) instead of an error.

### 3.11 What other frameworks do (summary; the design is in the handoff, D1–D3)

| Framework | Model AbstractX should copy | Avoid |
|---|---|---|
| Zephyr | `*_dt_spec` devices; SPI and I2C message lists; `transceive_cb` with userdata; **async UART** (`rx_enable` + `RX_RDY`/`BUF_REQUEST`/`BUF_RELEASED`/`TX_DONE`); **RTIO** submission and completion queues with chains, triggers and cancel | callbacks in ISR context by default |
| Linux kernel | `spi_message` holding `spi_transfer` lists with per-segment settings; `i2c_msg` flags; `spi_async` completion callback with context; **regmap** | sync-only I2C core |
| Rust embedded-hal(-async) | one `Operation` list for SPI and I2C; `SpiDevice` owns CS and bus locking; `embedded-io` `BufRead` for UART | — |
| Tock | split-phase calls where buffer ownership goes to the driver and comes back in the callback; callbacks deferred to the kernel loop | — |
| RIOT | acquire/release bus locking; register flags (`I2C_REG16`, `NOSTOP`, `NOSTART`, `ADDR10`) | blocking calls |
| ESP-IDF | device handles; command/address/dummy phases; UART event queue and pattern detect | — |
| STM32 HAL / mbed | — | **global weak callbacks with no context** (one device, one transaction only) |

Lessons:
1. A transaction is a caller-owned operation list; register helpers are wrappers on top.
2. The device owns its settings, and the bus arbitrates.
3. Completion is pluggable: callback, signal, completion queue or async wake.
4. Callbacks always carry context.
5. Where a callback runs (ISR or loop) is always explicit.
6. UART RX is an armed stream.
7. Register access is its own layer (regmap).
8. Chains and triggers live in the I/O layer.

---

## 4. Per-target review

Each target starts with a status matrix across every area (buses, DMA, io_processor/IPC, runtime, boot, build, tests), then its bus-driver findings. Cross-cutting details are in §5–§13.

### 4.1 Allwinner E906 (`targets/allwinner_e906`)

**Target at a glance.** Quad A527 Linux plus a XuanTie E906 coprocessor (RV32IMAFC, 200 MHz, CLIC, 192 KB `SRAM_FAST`). Firmware is loaded by remoteproc and talks RPMsg over SRAM space 1.

| Area | State | Details |
|---|---|---|
| SPI0 (target HAL `spi.cpp`) | **Broken; never ran** | No pins, clock or IRQ; RX DRQ never enabled; wrong request completed (table below) |
| TWI (target HAL `i2c.cpp`) | **Broken** | PLIC instead of CLIC, `INT_FLG` not cleared, duplicate instance, TWI0 instead of TWI1 |
| UART2 (target HAL `uart.cpp`) | **Broken** | IRQ unrouted. GPS is actually on UART3. |
| App drivers `e906_periph.cpp` | **Works on board** | UART3 + TWI1 by IRQ (CLIC 90/91), board-tested. Waits synchronously for up to 20 ms from MicroPython. Bypasses `ISpi`/`II2c`/`IUart`. |
| DMA controller `dma.cpp` | **Never completes** | Line 88 not enabled, `DMA_MCU_IRQ_CTRL` unset, DA passed instead of PA |
| io_processor bridge | **Not connected, unsafe** | `set_ingress_ring` called only in a test. `wfi` waits on the loop. `header_skip` read past the buffer (Critical). `rx_len` and GPIO pin unchecked. (§7) |
| RPMsg / msgbox | **Core is solid, edges unsafe** | Vring indices and zero-copy batching are good. Unbounded spin when Linux stops draining. No resync after a ring error. `translate_da` unchecked. Msgbox drains the HiFi4 FIFOs. Channel 0 vs 1 mismatch. (§7) |
| Runtime / idle | **Best of all targets** | The mask → check → wfi idle is correct. `wfi+4` sample-return race. Single-slot sleep timer. (§5, §9) |
| Boot / memory | **Consistent, unprotected** | Trap, CLIC and ISA flags consistent. No stack guard, PMP never enabled, no watchdog, crash dump truncated by the 1 KB trace0. `SRAM_FAST` 169.8/192 KB per commit `7bcfa01` (not re-measured). (§9) |
| MicroPython | **Works, GC risks** | GC doesn't scan registers. Stack limit equals the whole stack. GC heap in unreserved HiFi4 RAM. (§9) |
| Trace | **Works** | CTF, profiler and timeline decode correctly. TLP endpoint can flood. `trace_imu` always on. (§10) |
| Apps | **Unsafe main** | The `gps_imu` E906 main writes into the live resource table. `serial_loopback` busy-polls. (§11) |
| Build / tests | **Cross-builds, few tests** | nano.specs, gc-sections. No ctest. Board tests cover only UART3, TWI1 and trace. `e906_freertos_app` skipped (submodule missing). (§12) |

**DMA vs interrupt on this target** (full table in handoff D7):
- IMU over SPI0 (15 B) → FIFO + completion interrupt.
- FPGA link (≤ 448 B) → DMA.
- TWI1 compass → TWI driver engine, no DMA.
- UART3 GPS → FIFO + RX-timeout interrupt.

Today the code uses unmeasured thresholds (SPI 4 B, I2C 32 B, UART TX 32 B) and spins on `XCH`.

**Bus driver findings:**

**Verdict:** the target-HAL SPI0, TWI0, UART2 and DMA drivers have never worked on the board. What does work is `apps/e906_repl_app/src/e906_periph.cpp` (UART3 + TWI1, by interrupt, on CLIC lines 90/91), and that file bypasses `ISpi`/`II2c`/`IUart` [R]. That driver waits synchronously for up to 20 ms from the MicroPython VM [R].

| Sev | Bus | Finding | Where |
|---|---|---|---|
| High | I2C | `init` programs a PLIC at `0x10000000`/IRQ 26, but the E906 has a CLIC at `0xE0800000`. `irq_dispatcher.cpp` routes only timer, msgbox and DMA, so `fc_twi0_isr`, `Uart2::handle_irq` and `Spi0::handle_irq` are never called. | `i2c.cpp:84-87`, `irq_dispatcher.cpp:147-152` [V] |
| High | DMA | CLIC line 88 is never enabled and `DMA_MCU_IRQ_CTRL` (`+0x38 = 0xFF00`) is never written, so no DMA completion arrives. | `dma.cpp`, `clic.hpp:23` [V] |
| High | All | `Pio::init()` has no caller. `Spi0::init`/`Uart2::init` never enable the CCU module, SPI0's module clock is never set, and nothing sets TWI0's pins. | `pio.cpp:16` [V] |
| High | DMA | LLI and buffer addresses are E906 DA, not PA. `da_to_pa` is "not implemented yet" per [SPEC-E906-HAL-01]. | `dma.cpp:85`, `i2c.cpp:157,176`, `spi.cpp:178-190`, `uart.cpp:173` [V] |
| High | SPI | Only `FCR` bit 24 (TX DRQ) is enabled. RX DRQ (bit 8 per spi-sun6i [I]) never is, and the completion is on the RX channel. | `spi.cpp:213` [V] |
| High | SPI | `E906SpiDriver::on_dma_done` pops the **next** queued request and completes it. The real one never resumes, and with a single request it hangs. | `platform.cpp:57-66` [V] |
| High | SPI/I2C | The bridge waits in `wfi` inside `service_task` (the event loop). With interrupts unrouted, the first bridge TLP hangs the E906. | `io_processor.cpp:288-290,335-344` [R] |
| High | I2C | The DMA callback is bound to static `g_e906_i2c`, but `io_processor` owns its own `E906I2c i2c_`, so `busy_` never clears. | `i2c.cpp:52`, `io_processor.cpp:402` [V] |
| High | I2C | The ISR writes `CNTR` without `INT_FLG`. Allwinner TWI clears the flag by writing 1, as `e906_periph.cpp:104,137` does, so the state machine would stall. | `i2c.cpp:292,298,306,333` [R] |
| Med | I2C | DMA done ≠ bus done. NACK is never reported. A write+read on the DMA path drops the read. TWI0 is used, but the compass is on TWI1. | `i2c.cpp:201-217,270` [V] |
| Med | SPI | `DMA_THRESHOLD=4` is unmeasured. The FIFO path spins on `XCH`, and `await_suspend` resumes inline. Completion goes through the globals `g_spi_coro_handle`/`g_spi_completed_ptr`. DMA burst is 1. | `spi.cpp:224-276` [V] |
| Med | SPI | `len = tx.size()` is used for RX, so RX overflows when `rx` is shorter. CS is hard-wired to CS1. Mode, `cs_pin` and `lsb_first` are ignored. | `platform.cpp:47` [V] |
| Med | SPI | The divider is written to CDR2 without DRS, and clamped to `0xF` though the field is 8 bits. Assumes a 24 MHz source. | `spi.cpp:93-98` [V/R] |
| Med | SPI | `start_dma_transfer` has no busy check, so DRDY auto-read can overwrite a bridge transfer in flight. | `spi.cpp:148-160` [R] |
| Med | UART | The TX ring never drains (interrupt not routed). The short path resumes inline. `DMA_TX_THRESHOLD=32` is below the 64 B FIFO. A lost RX wakeup is possible: `await_suspend` clears `packet_ready` after the ISR may have set it. | `uart.cpp` [V/R] |
| Low | UART | No `UART_HALT` sequence. Busy-detect (IIR `0x07`) isn't handled, which would storm once routed. IER read-modify-write isn't guarded. | [R] |
| Low | DMA | The clock gate written is `McuDma`, but the controller is the main DMAC at `0x03002000` [I]. | `dma.cpp:34` [R] |
| Low | Docs | The atlas says GPS is on UART2 (PB0/PB1); the board and spec use UART3 (PI11/PI12). | [R] |

**Correct as written [R]:** DMA channel ownership 8..15 using register 1 only. DMA register offsets and cfg layout match sun6i. D-cache is off, so there is no coherency issue. I2C clock maths is right at 24 MHz.

### 4.2 Raspberry Pi Pico 2 W (`targets/pico2w_rp2350`)

**Target at a glance.** RP2350: dual Cortex-M33 at 150 MHz, PIO, 16 DMA channels, CYW43 Wi-Fi. The spec says Core 1 runs the coroutines and Core 0 does PIO, DMA and Wi-Fi.

| Area | State | Details |
|---|---|---|
| SPI (`hal_spi.cpp`) | **Real DMA path, broken chaining** | Each completion starts two requests. Two instances share DMA channels. Mode 3 hard-coded. Status always Ok. |
| I2C (`hal_i2c.cpp`) | **Async is a stub** | Never programs the address or `data_cmd`; `TX_EMPTY` storm [I]. The only working path is `i2c_*_blocking`, called from `step()`. |
| UART (`hal_uart.cpp`) | **Minimal, works** | IRQ ring. Ok reported on enqueue. Silent drops. `stdio_init_all()` likely on the GPS pins [I]. |
| DMA | **Used for SPI** | Static channel claims shared by the two `PicoSpi` instances |
| io_processor | **Core 0 runner, unsafe input** | `on_auto_spi_dma_done` never called. `rx_len`, GPIO pin and `header_skip` unchecked. LED on GPIO25, the CYW43 CS [I]. SIO FIFO written with no reader. (§7) |
| Multicore | **Not implemented** | No Core 1 split (topology code unused). `InterruptLock` (PRIMASK) is local to one core. No cross-core dispatcher or doorbell. |
| Idle | **Race possible** | Bare `wfe` with no masked re-check [I] |
| Networking | **Blocking, copies** | 15 s blocking Wi-Fi connect in init. `pbuf` memcpy overflows chained pbufs and breaks zero-copy. `udp_new` failure not retried. |
| Platform | **No safety net** | No watchdog |
| Build / tests | **Unverified** | Configures with the SDK. **No `nano.specs`.** Never built during this review. No host or board tests for any Pico driver. |

**DMA vs interrupt on this target:** the PL022 SPI FIFO is only 8 entries [verify], so the 15 B IMU read needs DMA here, unlike on the E906. I2C (16-entry FIFO) and UART (32 B FIFO) use FIFO + interrupt. PIO + DMA handles anything unusual (quad, 3-wire, inverted, DShot).

**Bus driver findings:**

**Verdict:** partial.
- SPI: a real DMA path, but request chaining is broken.
- I2C: the async path is a stub.
- UART: IRQ RX/TX works minimally.
- Auto-DMA pipeline: dead.
- Build: compiles only with the SDK, and has never been built here.

| Sev | Bus | Finding | Where |
|---|---|---|---|
| High | SPI | `on_dma_complete_from_isr` calls `set_hardware_idle_from_isr()`, which starts the next request, and then pops and starts **another**. A's DMA is reconfigured mid-flight, A never completes, and B gets corrupt data. The same pattern is in the I2C ISR and the timer alarm. | `hal_spi.cpp:157-164` [V], `hal_i2c.cpp:156-166`, `abstractx_pico.hpp:153-158` [R] |
| High | SPI | Two `PicoSpi` objects (`g_pico_spi`, `PicoIoProcessor::spi_`) share static DMA channels and `g_active_spi_instance`. A completion resumes a stale handle through a dead `result_slot`. `spi_.busy_` never clears, so `while(busy) wfe` hangs Core 0. | `hal_spi.cpp:31,69`, `io_processor.cpp:162,301,437` [R] |
| High | SPI | Host `rx_len` (`uint8_t`) is unclamped into the 64 B `bridge_rx_buf_` (the I2C path does clamp). | `io_processor.cpp:298-301` [R] |
| High | I2C | The async path never programs the target address or `data_cmd`. It unmasks `TX_EMPTY` (level-triggered) and the handler ignores it, so there is an interrupt storm [I] and no completion. `Qmc5883l::init_async` hangs. | `hal_i2c.cpp:263-334` [R] |
| High | SPI | `on_auto_spi_dma_done()` has no caller, so DRDY bursts never produce a TLP. DRDY doesn't check `busy_`. | `io_processor.cpp:168` [R] |
| High | Rules | Inside `step()`: a `wfe` spin on SPI, and `i2c_write_blocking`/`i2c_read_blocking` with no timeout. | `io_processor.cpp:304-308,357-363` [R] |
| Med | SPI | RX overflows when `rx` is shorter than `tx`. If `init()` wasn't called, the request is dropped and `hardware_busy_` sticks forever. Mode 3 is hard-coded. Mode, `lsb_first`, `cs_pin` and `set_frequency` are ignored. Status is always Ok. | `hal_spi.cpp:92,147` [R] |
| Med | UART | `write_async` reports Ok as soon as bytes are queued, and drops bytes when the ring is full. `stdio_init_all()` probably puts printf on the GPS pins GP0/GP1 [I]. | `abstractx_pico.hpp:75-82`, `hal_platform.cpp:45` [R] |
| Med | All | `InterruptLock` uses PRIMASK, which is local to one core. It will race once coroutines run on Core 1. | `interrupt_lock.hpp:75-78` [R] |
| Med | Platform | LED written as GPIO25, which is the CYW43 chip select on Pico W [I]. `sio_hw->fifo_wr` is written with no ready check and is never read. `pbuf_alloc(PBUF_POOL)`+`memcpy` overflows a chained pbuf and is a copy. | `io_processor.cpp:249,259,184`, `hal_platform.cpp` [R] |
| Low | UART/SPI | Overrun and framing errors ignored. `flush()` does nothing. TX DMA reads past `auto_tx_buf_[32]`. `return` used where `continue` was meant, so later channels are skipped. | [R] |

### 4.3 ESP32-P4 (`targets/esp32p4`)

**Target at a glance.** ESP32-P4: dual RISC-V at 400 MHz, GDMA, caches and L2MEM. The spec says Core 1 runs coroutines and Core 0 does GDMA and Wi-Fi.

| Area | State | Details |
|---|---|---|
| SPI | **Stub** | 36 lines. Every transfer returns `Ok` with zeros. `init` and `select` are no-ops. Completion is inline inside `submit_request`, which recurses up to 16 deep with interrupts masked. |
| I2C, UART, GPIO | **Missing** | No files. The spec §3.3 names `hal_uart.cpp`/`hal_gpio.cpp`, which don't exist. |
| Platform / io_processor | **Missing** | No `platform_init`, no Core 1 launch, no DRDY ISR |
| Timer | **Blocks** | `delay_ms` busy-spins with `ets_delay_us`, reachable from coroutines |
| Mailbox | **No-op** | The doorbell returns `true` and does nothing |
| Locking | **Single core only** | `InterruptLock` uses `csrrc mstatus`, which masks only the local hart |
| Build | **Doesn't build** | No IDF `project()`. Outside IDF it uses the host compiler and links a library that doesn't exist. The only toolchain file is for Xtensa, but P4 is RISC-V. `etl` isn't provided to IDF. |
| Claims | **False** | "1.8 µs loop", AMP fences "verified on ESP32-P4", TRACE-01 `[x]`, "runs identically with zero `#ifdef`s" |

**What a real port needs** (handoff P3-04):
- IDF project and component.
- SPI via `spi_device_queue_trans` + `post_cb` that completes the in-flight transaction (found through `trans.user`).
- DMA-capable, cache-line-aligned buffers with `esp_cache_msync`.
- I2C via `i2c_master` async with `on_trans_done`.
- UART via the driver ring plus event queue (`UART_DATA`, `UART_PATTERN_DET`).
- Platform layer, cross-core doorbell and DRDY GPIO ISR.

The other option is to mark the target unsupported everywhere.

**Bus driver findings:**

**Verdict:** a stub.
- `hal_spi.cpp` (36 lines) returns Ok and zeros, and ignores the config. `select()` does nothing.
- There is no I2C, UART, platform or io_processor code.
- Nothing instantiates the classes.
- The target doesn't build: there is no IDF project, and outside IDF it links a library that doesn't exist.

Markdown elsewhere claims a 1.8 µs loop, AMP fences "verified on ESP32-P4", TRACE-01 done, and zero-`#ifdef` portability. None of these can be true [R].

### 4.4 Zynq-7000 / Linux HAL (`targets/zynq7000`, `targets/linux`)

**Target at a glance.** Zynq-7020 (QMTECH): dual Cortex-A9 Linux (PREEMPT_RT) plus PL fabric. The Linux HAL (`targets/linux`) is compiled straight into the Zynq target. The PL is reached through UIO (`/dev/uio0`).

| Area | State | Details |
|---|---|---|
| PS SPI (spidev worker) | **Real, results lost** | Worker skips `push_completion_from_isr`, so `result_slot` never written. `len = max(tx,rx)` for both buffers. `speed_hz_` read and written across threads without synchronisation. |
| PS I2C (i2c-dev worker) | **Wrong device, mocks** | Opens `/dev/i2c-1`; DT exposes `/dev/i2c-0`. Falls back to fake magnetometer data. |
| UART (tty) | **Never opened** | `LinuxUartDriver::open()` has no caller. The GPS is always synthetic (a fix with 18 satellites). TX goes to stdout. |
| PL TLP path (UIO) | **Broken** | Software uses the `asp_axi_dma` map, but the top instantiates `asp_axi_lite_bridge`. 100 % CPU, all TLPs discarded, egress IRQ stays asserted. |
| PL IMU SPI engine | **Real, edge bugs** | `asp_imu_auto_dma.sv` drops triggers during a burst, has zero CS hold, and `burst_len` overflows (§8) |
| PL I2C / UART | **None** | No RTL. Channel `0x05` UART tunnelling (SPEC-ZYNQ-03) is unimplemented. |
| Motors | **Unsafe** | The PL keeps running when Linux dies, holding the last throttle (§2) |
| Threading | **Risky** | One process-wide busy-spin lock on PREEMPT_RT [I livelock]. Partial UART writes reported as Ok. |
| Rules | **Undocumented exception** | `<thread>`/`<mutex>`/`<iostream>`/`<deque>` used under `targets/` (AGENTS §2) |
| Build / tests | **Host compiler only** | No ARM Linux toolchain file. Only Buildroot config tests. `test_io_processor` covers the Linux implementation with `sleep_for` timing and no malformed input. No worker tests. |
| Board files | **Unverified** | XDC pins that may not exist on CLG400 [I]. No `uio_pdrv_genirq` boot argument [I]. |

**DMA vs interrupt on this target:** not the application's decision for the PS peripherals, because the kernel driver chooses. Keep the event loop non-blocking (workers or `io_uring`/epoll) and complete through the base class. On the PL side, DRDY → `asp_imu_auto_dma` → TLP is already the correct "DMA" design.

**Bus driver findings:**

**Architecture:** the PS controllers (spidev, i2c-dev, tty) are driven by worker threads. The only fabric bus core is the IMU SPI master `asp_imu_auto_dma.sv`, reached through Wishbone, the router and `asp_axi_lite_bridge` on `/dev/uio0`. There is no I2C or UART RTL.

| Sev | Finding | Where |
|---|---|---|
| Critical | The software uses the `asp_axi_dma` DDR-ring register map, but the top instantiates `asp_axi_lite_bridge` (BRAM map). Both report `ASP6` at `0x40`, so the probe passes. The loop then fires up to 128 fake RX events per step at 100 % CPU, discards every TLP in both directions, and never writes the 16 DWORDs. | `zynq_uio.hpp:34-47` [V], `top_qmtech_zynq7020.sv:152` [V], `io_processor.cpp:67-96` [R] |
| High | Workers call `notify_completion` directly, so `result_slot` is never written. Awaiters return the default `Ok`, and ioctl failures look like success. | `spi_worker.hpp:168-170` [V], `i2c_worker.hpp:185-187` [R] |
| High | `len = max(tx, rx)` is used for both buffers, so the kernel reads past `tx` or writes past `rx`. | `spi_worker.hpp:97-106` [R] |
| High | If a device fails to open, `start()` still returns `true` and serves mocks (WHO_AM_I `0x47`, a synthetic GPS fix with 18 satellites). `LinuxUartDriver::open()` is never called, so the GPS is always synthetic. | `spi_worker.hpp:41-51`, `i2c_worker.hpp:38-47`, `hal_uart.cpp:81-110` [R] |
| High | Opens `/dev/i2c-1`; the overlay and spec expose `/dev/i2c-0`. | `hal_i2c.cpp:26` [R] |
| Med | `worker_.submit()` failure is ignored, so the request is dropped and hangs. `speed_hz_` is written and read across threads without synchronisation. Partial UART writes are reported as Ok. `<iostream>`/`<deque>` used in `targets/`. | [R] |
| Med | One process-wide busy-spin lock on `PREEMPT_RT` risks livelock [I]. | `interrupt_lock.hpp` [R] |
| Inference | XDC pins J21, J22 and others don't exist on CLG400. No `uio_pdrv_genirq` boot argument found. | [R] |

### 4.5 Host / SITL (`ABSTRACTX_TARGET=host`)

| Area | State | Details |
|---|---|---|
| Role | **The only target with ctest** | 4 CppUTest, 16 sim, 18 examples and about 14 Python tests. CI runs host + ASan. **No TSan in CI.** |
| Bus HAL | **Linux workers or mocks** | The same worker `result_slot` bug as Zynq, so a SITL bus failure reads as `Ok` |
| How tests drive coroutines | **Mostly unrealistic** | The IMU mock completes inline inside `await_suspend`, so deferred completion (the real ISR path) is never exercised. The GPS baud test calls `task.resume()` in a loop (AGENTS §10.6). `test_coro_combinators.cpp` isn't registered and never calls `when_all`/`when_any`. |
| Steady-state tests | **Partial** | Exist for `AsyncQueue` deferred resume and the `gps_imu` frame pool. Missing for drivers, timers, the dispatcher-full case and I2C/UART. |
| What a green host suite proves | **Little about targets** | It shows the shared code compiles and the narrow paths tested work. It doesn't show any target bus, DMA, IRQ or IPC path works. |

**Takeaway:** most of the bugs in this review are invisible to the current host suite. Handoff P1 adds host tests that complete from a separate thread (standing in for the ISR), drive coroutines only through `step()`, and run 1000+ iterations with flat counters. They are what would have caught these bugs.

---

## 5. Coroutine runtime, scheduling and channels

**Verdict:**
- **Works:** simple single-core FIFO use, where each primitive has one waiter and wakes go through `Dispatcher::push`/`post`. The E906 idle path is the most carefully built part.
- **Broken:** several advertised primitives under the project's own rule that only `step()` resumes coroutines: `yield`, `delay_*`, `sleep_*`, `wait_until`, `TimerService::sleep_ms`, `when_all` and `when_any`.
- **Missing:** priorities, cancellation, timeouts, select, latest-value channels and cross-core scheduling.

The primary-paced channel pattern (AGENTS §5) is only half met: the consumer side is right, the producer pacing and `dt` source are wrong.

### 5.1 What the runtime provides (`include/asp_coro.hpp`, `abstractx/coro.hpp`, `timer.hpp`, `domain_dispatcher.hpp`, `src/runtime.cpp`)

| Primitive | API | Intended semantics | Status |
|---|---|---|---|
| `Task<T>` / `Task<void>` | `co_await child()` | Lazy start (`suspend_always`), symmetric transfer back to the caller in `FinalAwaiter` | ✅ core mechanics correct [V] |
| Frame pool | `ABSTRACTX_CORO_POOL_SIZE` (default 60 KB; E906 4 KB), `coro_pool_used/high_water/capacity` | Zero heap: bump allocator plus an exact-size free list, 64 B-aligned frames | ⚠️ exhaustion returns a null task that looks ready (§5.3) |
| Dispatcher | `Dispatcher::post/push/isr_post_resume/process/dropped_resumes` | ISR-safe ready queue; only `process()` resumes | ⚠️ 32 entries, one FIFO, drops when full [V] |
| `spawn(task[, name])` | up to 16 tasks (`etl::vector`) | Starts a top-level task | ⚠️ resumes inline at spawn; erases done tasks each `step()` |
| `step()` | `Dispatcher::process()` → `io_proc.step(0)` → `poll_network` → trace flush → status → erase done | One event-loop pass | ⚠️ fixed per-pass overhead regardless of priority; work duplicated with `service_task` |
| `run(main)` | resume main, then loop `step()` + idle | Top-level loop | ✅ idle only when nothing is pending; relies on the target's `platform_idle_wait` re-checking under a mask |
| `step_async()` | `co_await step_async()` | Yield until the next pass | ✅ **works**: pushes the handle [V] |
| `yield()` | `co_await yield()` | Yield | ❌ **never resumes**: empty `await_suspend` [V]. A second, broken spelling of `step_async`. |
| `delay_us/ms`, `delay_until`, `sleep_for/until` | `delay_us(delta, current_time, timer_reg*)` | Sleep | ❌ never registers the handle. The API also makes the caller pass the current time and a raw comparator pointer. |
| `wait_until(pred)` | predicate polling | Wait for a condition | ❌ never re-queued. Polling a predicate is also the wrong model for an event-driven runtime. |
| `TimerService` / `sleep_ms` | ETL callback timers, 32 slots | Sleep | ❌ `tick()` has no caller [V]. Slot reuse and leak bugs. |
| `ITimer::sleep_us_async` | driver awaiter | Sleep via hardware compare | ⚠️ the only sleep that can work. The E906 driver has **one** slot. |
| `Event` | `co_await ev; ev.set(); ev.reset()` | Manual-reset event | ⚠️ single waiter [V] |
| `Semaphore` | `co_await sem.acquire(); sem.release()` | Counting semaphore | ⚠️ single waiter; a woken waiter may proceed without a count [V] |
| `AsyncQueue<T,N>` | `co_await q.pop()/push(v)`, `try_pop/try_push/try_peek` | SPSC channel | ⚠️ details in §5.5 |
| `CoroutineIoEngine` | `MemRead/MemWrite/Stream` awaiters, 256 tags | TLP split transactions | ⚠️ resumes inline; tag leak on TX failure; tag 255 unused |
| `when_all` / `when_any` | combinators | Structured fan-out | ❌ inline resume inside `FinalAwaiter`, use-after-free, loser destroyed while queued |
| Cancellation, timeouts, select, mutex, latest/broadcast channels, priorities, cross-core | — | — | ❌ absent |

### 5.2 Execution model as built

```
run(main):  main.resume()                      // inline start (acceptable once)
            loop: step(); if (!Dispatcher::has_pending_resumes()) platform_idle_wait();
step():     Dispatcher::process()              // drain a snapshot of the FIFO, resume each
            io_proc.step(0)                    // target I/O reactor
            platform_poll_network()
            g_tracer.flush(now)
            status_service()
            erase finished tasks (≤16)
service_task(): loop { io_proc.step(0); g_tracer.flush(now); co_await step_async(); }   // duplicates step()
```

What this means in practice:
1. **No priority.** An 8 kHz IMU wake waits behind every resume already in the snapshot (GPS parse, CLI, telemetry). After that it also waits for the io_processor, network and trace work in the same pass.
2. **No time budget.** One long resume, a large trace flush or a blocking io_processor call (they exist on E906 and Pico, §4) delays everything. Worst-case latency is unbounded.
3. **No timer service in the loop.** No pass checks deadlines, which is why the sleep primitives that aren't driver-based never wake.
4. **Inline starts.** `spawn()` resumes the new task inside the caller. If `spawn` is called from inside a coroutine, that is a nested resume outside `process()`.

### 5.3 `Task<T>` and the frame pool

**Good:**
- Lazy start.
- `FinalAwaiter` returns the continuation (symmetric transfer, no stack growth).
- `operator new`/`delete` are class-scoped to the pool.
- `unhandled_exception` traps through a hook.
- Pool usage and high-water are observable.

**Issues:**
- **Exhaustion:** `get_return_object_on_allocation_failure()` returns `Task{nullptr}`. `await_ready()` treats a null handle as done, so `co_await` of a `Task<void>` silently "succeeds" and `Task<T>` dereferences an empty optional [R]. Nothing counts the failure.
- **Fragmentation:** frames round up to 64 B, wasting up to 63 B each, and the free list only reuses exact sizes. A new frame size bumps the pool even if enough total space is free. On the E906 4 KB pool, a few distinct call sites can exhaust it.
- **Locking:** the free list takes a spinlock, which can deadlock if a frame is ever freed from an ISR on the same core. Today frames aren't freed from ISRs [I].
- **Destroying a suspended task:** `~Task()` destroys the frame even if its handle is still in the dispatcher queue, a timer slot, a driver request or a queue waiter slot. The next post resumes freed memory. There is no cancel-on-destroy.
- **No detach:** top-level tasks are owned by `spawn`'s 16-slot vector. There is no detached or fire-and-forget task with a defined lifetime, and no join or result for spawned tasks.

### 5.4 Dispatcher

- **Queue:** `etl::queue_spsc_isr<coroutine_handle<>, 32>` guarded by `InterruptLock`.
  - Several ISRs plus the loop push into it, so it is really MPSC protected by interrupt masking. That works on one core only.
  - Hosted builds use one process-wide spinlock for every queue.
- **Full queue:** the wake is dropped permanently and counted in `dropped_resumes`. Each primitive, timer and driver feeds this one queue, so a burst can lose wakes, and a lost wake is a hung coroutine.
- **Entries:** only coroutine handles. A plain callback (as in the handoff's D1 transaction model) can't be deferred to the loop.
- **Fairness:** `process()` drains a snapshot, which is fair within a pass but has no priority.

### 5.5 Channels: `AsyncQueue<T, N>`

```cpp
std::array<T, Capacity + 1> buffer_;  size_t head_, tail_;   // plain size_t, modulo (Capacity+1)
std::coroutine_handle<> push_waiter_, pop_waiter_;           // one waiter per side
PopAwaiter::await_resume()  { return q_.try_pop(); }        // T{} if empty
PushAwaiter::await_resume() { q_.try_push(std::move(item_)); }   // result ignored
```

| Property | Today | Problem |
|---|---|---|
| Producer/consumer model | SPSC by intent | Not enforced. A second producer or consumer races; a second waiter overwrites the first. |
| ISR or cross-core push | Documented "lock-free" | No atomics or fences [V]. Unsafe from an ISR on another core, and on hosted builds from a worker thread. |
| Wake-up | `Dispatcher::push` (good: never inline) | The waiter doesn't re-check after waking: `pop` can return `T{}`, `push` can silently drop |
| Overflow policy | Producer suspends (`push`) or fails (`try_push`) | No "overwrite oldest" or "latest wins" mode, which sensors need |
| Index maths | `% (Capacity+1)` | Division on every operation; not power-of-two |
| Element type | `std::array<T>` | `T` must be default-constructible; `try_pop()` returns `T{}` as "empty", which is ambiguous |
| Timeouts | none | `co_await q.pop()` can wait forever; no `pop(deadline)` |
| Select / multi-wait | none | A task can't wait on "IMU sample **or** command **or** timeout" |
| Close / end-of-stream | none | No way to stop a consumer cleanly |
| Observability | none | No depth high-water or drop counters (AGENTS §10.5 asks for depth checks) |

The other primitives:
- `Event` is manual-reset with one waiter.
- `Semaphore` has one waiter, and `await_resume` decrements only `if (count_ > 0)`, so a waiter that lost the race proceeds without a permit.

**Missing primitives a flight stack needs:**
- `Latest<T>` / watch: a seqlock with a version, `co_await changed()`. For "newest mag/GPS/baro" data, without queue backlog.
- `Broadcast<T,N>`: several readers, such as telemetry, logging and the controller.
- `Mutex`: async, for multi-step bus sequences.
- `select`/`when_any` that actually works, with cancellation.
- `with_timeout(awaitable, deadline)`.
- Periodic timers: `co_await every(period)`, with drift-free absolute deadlines.

### 5.6 The primary-paced channel pattern (AGENTS §5) as implemented in `gps_imu_app`

| Rule | Status |
|---|---|
| Each sensor has a producer coroutine feeding an `AsyncQueue` | ✅ |
| **The primary sensor (IMU DRDY) is the physical clock** | ❌ The IMU producer issues back-to-back SPI reads (`next_sample_async` doesn't wait for DRDY), so the loop runs at bus speed. The DRDY auto-DMA ring `g_sensor_ring` has no consumer. |
| Fusion awaits `co_await g_imu_channel.pop()` | ✅ |
| Auxiliary sensors drained with `while (q.try_pop(x))` | ✅ (but a `Latest<T>` would express the intent and avoid a backlog) |
| No modulus prescalers | ❌ `seq % 10` in `gps_imu_tasks.hpp:112` |
| Integration uses the sample's own timestamp | ❌ `dt` comes from pop time; gaps over 50 ms are replaced with 1 ms |
| Behaviour when the producer can't submit (queue full) | ❌ the awaiter returns `false`, the producer loops without suspending, and `step()` livelocks [I] |

### 5.7 Concurrency and multicore

- **Single core:** `InterruptLock` masks interrupts on the local core only (PRIMASK on M33, `mstatus` on RISC-V).
- **Pico / P4:** no cross-core dispatcher, SIO/FIFO doorbell or per-core ready queue. [SPEC-ARCH-05] describes them, but they don't exist.
- **Pico idle:** a bare `wfe` without the masked re-check that E906 does [I lost wakeup].
- **Unused queue:** `MpscIsrQueue` (named by [SPEC-ARCH-04]) is unused, and its `pop` relies on `volatile` rather than acquire/release.

### 5.8 Bugs

| Sev | Finding | Where |
|---|---|---|
| High | `YieldAwaiter::await_suspend` is empty; `AsyncDelayAwaiter`/`WaitUntilAwaiter` never register. They never resume under `run()`/`step()`. | `asp_coro.hpp:424-428` [V], `:441-445,486` [R] |
| High | `TimerService::tick()` has no caller, so `sleep_ms()` never wakes | `timer.hpp:230-232` [V] |
| High | `TimerService`: with all 32 slots used, or registration failing, the awaiter hangs and the slot leaks. The ISR clears `slot_active_` before resume, causing double free and lost sleepers. Plain `bool` shared with the ISR. | `timer.hpp:249-288` [R] |
| High | E906 `Timer`: one `g_timer_coroutine` slot, so a second sleeper is lost | `targets/allwinner_e906/src/timer.cpp:153-156` [R] |
| High | `when_all`/`when_any` resume the caller inside the child's `FinalAwaiter`, then read a possibly freed frame | `asp_coro.hpp:871-877,945-952` [R] |
| High | `when_any` loser, or any destroyed parent, frees a frame whose handle is still queued | `asp_coro.hpp:956-957` [R] |
| Med | Inline resumes in `start_one`, `when_any`, `CoroutineIoEngine::poll_completions`, `spawn()`, `run()`; the comment at `:814` says otherwise | [R] |
| Med | Pool exhaustion gives a null task treated as complete; no counter | `asp_coro.hpp:191-193,293-295` [V] |
| Med | Single waiter slots, no re-check after wake (`Event`, `Semaphore`, `AsyncQueue`) | `asp_coro.hpp:496-636` [V] |
| Med | `AsyncQueue` has no atomics despite "lock-free" | `asp_coro.hpp:565-636` [V] |
| Med | Dispatcher drops wakes when its 32 entries are full | `domain_dispatcher.hpp:88-93,137-141` [V] |
| Med | `CoroutineIoEngine` tag leak on TX failure; stale response | `asp_coro.hpp:726-735` [R] |
| Med | `step()` and `service_task()` both run `io_proc.step(0)` and the trace flush | `src/runtime.cpp:300-350` [V] |
| Med | Pico idle bare `wfe` | `targets/pico2w_rp2350/src/hal_platform.cpp:75-78` [R] |
| Low | Exact-size free list, O(n) under spinlock; 64 B rounding | `asp_coro.hpp:103-160` [V] |

### 5.9 How other runtimes do it, and what to copy

| Runtime | Scheduling | Channels / sync | Timers | Cancellation | Copy for AbstractX |
|---|---|---|---|---|---|
| **Embassy** (Rust, embedded async) | One executor per priority; `InterruptExecutor` runs high-priority tasks at interrupt priority | `Channel` (MPMC, bounded), `Signal` (latest value), `Watch`, `PubSub`, `Mutex` | Global timer queue, `Timer::after`, `Ticker` (periodic) | Drop the future to cancel; `select`, `with_timeout` | Executors per priority; `Signal`/`Watch` = `Latest<T>`; `Ticker`; `with_timeout`/`select` |
| **RTIC** | Tasks bound to hardware interrupt priorities; preemptive via NVIC; Stack Resource Policy gives compile-time deadlock freedom | Message passing between tasks with capacity | Monotonic timer, `spawn_after` | — | Run the RT priority from an interrupt context if the cooperative loop can't meet 8 kHz |
| **Zephyr** | Preemptive and cooperative thread priorities; `k_work` queues; deadline scheduling (EDF) option | `k_msgq`, `k_pipe`, `k_fifo`, `zbus` (pub/sub, latest value) | `k_timer`, `k_work_delayable` | `k_work_cancel` | `k_poll`-style select; zbus "latest" channels; delayable work |
| **FreeRTOS** | Fixed-priority preemptive | Queues, stream/message buffers, task notifications (cheap single-waiter) | Software timers in a daemon task | Task delete | Task notifications ≈ a cheap single-waiter `Event`; explicit priorities |
| **C++ structured concurrency** (cppcoro, libunifex, `std::execution`) | Scheduler concept | `async_mutex`, `single_consumer_event`, `async_generator` | `schedule_after` | **`stop_token`** propagated to every awaitable; `when_all` that cancels siblings | `stop_token` cancellation; parent owns children; awaiters unregister on destroy |
| **Tokio** | Work-stealing (not embedded) | `mpsc`, `oneshot`, **`watch`** (latest), **`broadcast`** | Timer wheel, `interval` | `select!` drops losers; `timeout()` | The channel family (oneshot/mpsc/watch/broadcast) and `interval` with missed-tick policy |
| **Betaflight / INAV** | Cooperative scheduler with **per-task rate and priority**; the gyro loop is synced to gyro DRDY; Betaflight checks each task's execution-time budget | Ring buffers per serial port; sensor "latest value" structs | Task periods (`TASK_PERIOD_HZ`), `micros()` | — | **Rate-based tasks with priority and a time budget**; the RT loop driven by gyro DRDY (§15) |

Lessons for AbstractX:
1. **Priority classes.** RT, Normal and Background ready queues. RT is driven by the IMU interrupt, or runs in an interrupt-priority executor if measurements show the cooperative loop can't hold 8 kHz.
2. **One timer heap** in the runtime, with absolute deadlines and periodic tickers.
3. **Structured concurrency.** A parent owns its children. Cancellation is a token, and every awaiter unregisters in its destructor.
4. **A channel family:** SPSC queue (atomic), `Latest<T>`, `Broadcast`, oneshot, plus `select` and `with_timeout`.
5. **Measured budgets:** each task gets a rate and a time budget, and the runtime reports overruns (Betaflight-style task stats, already partly in `runtime_stats`).

The concrete design is in handoff D4, and the tasks are P1-02 to P1-06.

### 5.10 Tests and spec drift

**Tests:**
- `test_coro_combinators.cpp` isn't registered in ctest and never calls `when_all`/`when_any` [R].
- Nothing tests:
  - `yield`, `delay`, `sleep_ms`, `wait_until`;
  - two concurrent sleepers;
  - two waiters on one primitive;
  - a full dispatcher;
  - pool exhaustion;
  - destroying a parent with a suspended child;
  - an ISR or thread pushing into `AsyncQueue` under TSan.
- Steady-state tests exist only for the queue deferred resume and the `gps_imu` pool.

**Spec drift:**
- [SPEC-ARCH-02] names `CoroutineStaticPool`, which doesn't exist. The pool is a bump allocator plus a spinlocked free list.
- [SPEC-ARCH-04] names `MpscIsrQueue` as the work queue; it's unused.
- [SPEC-ARCH-05] SIO doorbell and Core 1 dispatcher don't exist.
- [SPEC-ARCH-06] auto-placement isn't implemented.
- [SPEC-ARCH-10] leaves out the combinators, IoEngine, `spawn` and `run`, which still resume inline.
- `coro.hpp`/`asp_coro.hpp` say "lock-free SPSC" and "no `.resume()` in `await_suspend`"; both are false.
- `asp_coro.hpp:16` and `runtime.cpp` carry `@status Complete`.
- README "<100 B frame, 15-cycle switch" is unmeasured.

---

## 6. DMA vs interrupt use today

Moved to the handoff, §D7, which has the full guidance.

What's wrong today:
- **Unmeasured thresholds:** SPI 4 B, I2C 32 B and E906 UART TX 32 B. All go against [SPEC-E906-HAL-01], which says to use ISR + FIFO until a threshold is measured.
- **I2C on DMA:** almost never worth it.
- **Busy-waiting where DMA was meant:** the E906 SPI "fast path" spins on `XCH`.
- **Two DMA owners on one bus:** the io_processor auto-channels and the `ISpi` driver on the same bus (E906, Pico).

---

## 7. Processor framework, TLP and IPC

**Verdict:**
- `IIoProcessor` is four copies of one host-input parser that doesn't check its input.
- The RPMsg vring code is the strongest part: aligned indices, a torn-index guard, zero-copy batching.
- The E906 io_processor isn't connected to RPMsg.

| Sev | Finding | Where |
|---|---|---|
| Critical | `header_skip = active_auto_ch_.tx_len` (host-controlled) is unclamped, but the ISR clamps its own copy to 8. `make_stream_tx` slices `auto_rx_buf_ + header_skip`: an out-of-bounds read sent to the host. | E906 `io_processor.cpp:146-153` [V], Pico `:170-176`, Linux `:242-249` [R] |
| High | Two producers on one SPSC ingress ring (worker thread or DMA ISR, plus the drain loop). TLPs are lost or torn. | Linux `:253,403` vs `:301,324`, E906 `:157` vs `:211`, Pico `:180` vs `:231` [R] |
| High | RPMsg `notify_host`/`reply` spin with no limit on a full mailbox, reachable from `step()` via `tx_commit`. | `rpmsg.cpp:216-220,498-511` [R] |
| High | Byte order: the header comment says big-endian, C++ writes little-endian, Python decodes `<`, and the RTL reads `target_address` big-endian (`0x40000100` arrives as `0x00010040`). CRC is `<I` in `tests/conftest.py:74` and big-endian in RTL and cocotb. | `asp_tlp64.h:195`, `asp_wishbone_master.sv:109`, `abstractx_wire.py:16` [R] |
| High | `set_ingress_ring` is called only in a test, so every host→E906 TLP is counted as dropped. `fc_gpio_drdy_isr` has no callers. | `platform.cpp:208-234`, `rpmsg_endpoints.cpp:112` [R] |
| Med | DMA_CFG always returns OK. SETUP uses `channel_id` as an array index while ENABLE/DISABLE search for it. SETUP doesn't re-arm the trigger pin. | all io_processors [R] |
| Med | E906: every GpioEdge channel starts SPI0 DMA from one interrupt with no pin match. The ISR reads `channels_` while they're being changed. | E906 `:119-140` [V] |
| Med | Unknown channel or type gets no completion, so the host waits forever. A Completion-type TLP on `0x01` triggers SPI. On Linux, every TLP on `0x01` (MemRd/MemWr too) runs as SPI. | E906 `:252-394`, Pico `:272-425`, Linux `:310` [R] |
| Med | After a ring error, RPMsg never resyncs `last_avail_idx`, so RX is stuck permanently. | `rpmsg.cpp:208-211,421-424` [R] |
| Low | `MpscIsrQueue` `pop` uses volatile, not proper ordering, takes no lock on host builds, and includes `<mutex>` under `include/`. | `mpsc_isr_queue.hpp:262-272` [R] |

### Untrusted-input parsers

| Parser | Where | Checked? |
|---|---|---|
| SPI bridge `rx_len` | E906 `:276`, Pico `:298` | **No** |
| SPI bridge `rx_len` | Linux `:365` | Yes |
| DMA_CFG `tx_len`/`rx_len`/`bus_type`/`trigger_pin` | E906 `:195-205`, Pico `:215-225`, Linux `:284-294` | **No** |
| GPIO `pin`/`mask` (`1U<<pin`, can drive SPI and CYW43 pins) | E906 `:365-381`, Pico `:389-410` | **No** |
| I2C `rx_len`/`tx_len`; `TlpSpiView` `tx_len` | E906 `:327`, `asp_tlp_msg.hpp:131,157` | Yes |
| RPMsg rx `hdr->len` | `rpmsg.cpp:433` | Yes, but read twice (TOCTOU) |
| vring `desc_idx`/`num`/`align`/avail index | `rpmsg.cpp:124,208,325,421` | Yes |
| Descriptor/vring addresses (`translate_da`) | `rpmsg.cpp:102-113` | **No**: unknown addresses pass through |
| TX `desc->len` | `rpmsg.cpp:330-336` | Lower bound only |
| `host_addr_` | `rpmsg_endpoints.cpp:24` | **No**: any message can redirect egress |

### Design debt

- **TLP versioning:** none. `make_stream_tx` sets MemWrite, not DmaStream.
- **Address maps:** four conflicting maps (`asp_tlp64.h`, `pcie_reg_api.h`, `pcie_bar_map.hpp`, the specs).
- **SPI command sets:** two (`0xA0-A2` vs `0x80/01/02`).
- **Mailbox:**
  - `MsgBox::init` drains the HiFi4 DSP's mailbox FIFOs.
  - The io_processor kicks Channel 0 while RPMsg uses Channel 1.
- **Topology / Core split:**
  - `PlatformTopologyTable`, `enable_auto_placement` and `platform_launch_processing_domain` are unused, so there is no Core 0/Core 1 split.
- **Dead header:** `dram_spsc_protocol.h`.

**Spec drift:**
- `TLP_BUS_SPECIFICATION.md` type `0x04` collides with CPL.
- [SPEC-TLP-02] GPS base overlaps DSHOT and BARO.
- [SPEC-TLP-03] says ring depth 64; the E906 uses 8.

---

## 8. FPGA fabric (RTL)

| Sev | Finding | Where |
|---|---|---|
| High | `o_tlp_tx_ready` pulses at the end of every A2 read, even when `tx_empty`, so TLPs become ready and are consumed without being sent. | `asp_spi_frontend.sv:363,388` [R] |
| High | Data is sampled at the command byte, but ready comes 256 SCLK later. A higher-priority CplD that arrives in between is consumed unsent, and the IMU TLP is sent twice. | `asp_spi_frontend.sv:251`, `asp_router.sv:97-112` [R] |
| Med | In the Wishbone master, `m_cpl_tvalid<=1` comes after the handshake clear and wins, so every CplD is delivered twice. A MemRd can overwrite `tdata` while valid && !ready. | `asp_wishbone_master.sv:229-231,330-331` [R] |
| Med | The egress arbiter is combinational: `tdata`/`tvalid` switch while valid && !ready (breaks AXI-Stream). Fixed priority starves ESC. | `asp_router.sv:92-113` [R] |
| Med | The Wishbone master blocks on CplD drain, so a host that only writes freezes motor processing. No timeout on unmapped addresses. | `asp_wishbone_master.sv:285-308,330-335` [R] |
| Med | `reg_irq_status` has two drivers. The module isn't instantiated anywhere. | `asp_axi_dma.sv:175/203` [R] |
| Med | The AXI-Lite bridge assembles TLPs by word position, with no CRC or resync. When the FIFO is full the TLP is dropped and the write still returns OKAY. | `asp_axi_lite_bridge.sv:233-250` [R] |
| Med | The IMU auto-DMA drops triggers during a burst, has zero CS hold, and `burst_len` overflows. | `asp_imu_auto_dma.sv:175,221-235,287` [R] |
| Low | `sys_timestamp` is named nanoseconds but counts cycles. Zynq top does a 64-bit `/` and `%` in one cycle. TLP is one 512-bit beat with no `tlast` (the atlas says `tdata[63:0]`). | [R] |

- **Reset:** mostly fine. SPI CS/SCLK use 3-flop synchronisers, which only works up to about clk/4 SCLK (~6 MHz at 27 MHz).
- **Modules without a cocotb test:** `asp_pwm_decoder`, `asp_axi_dma`, `asp_ila_trace`, and every board top. `test_asp_top_cocotb` is failing and excluded. Only the FIFO test randomises `tready` (§11 of AGENTS).

---

## 9. Platform, boot and memory

| Sev | Finding | Where |
|---|---|---|
| High | The 8 KB stack sits directly above `.bss` with no guard, and `mp_stack_set_limit(8192)` equals the whole stack. Deep Python recursion overruns `.bss` before RecursionError can fire. | `bsp/e906_ddr.ld:143-165`, `e906_repl_app/src/main.cpp:101` [R] |
| Med | Sample-only trap return goes to the wfi when `mepc == wfi+4`. A real wake that also returns at `wfi+4` can go back to sleep with work pending, for up to about 1 s. | `startup.S:194-207`, `timer.cpp:143` [R] |
| Med | The crash report (~2.2 KB [I]) is larger than trace0 (1 KB), so the cause and `mepc` are lost. Only the 4 words at `0x3FFFFF00` survive. A nested fault recurses. | `crash.cpp:39-77`, `trace.cpp` [R] |
| Med | `gc_collect` doesn't scan callee-saved registers. `mp_stack_set_top(&stack_dummy)` runs inside a coroutine after Ctrl-D [I]. | `mphalport.cpp:105-110`, `main.cpp:50-52` [R] |
| Med | The GC heap in HiFi4 DSP RAM (`0x00020000`) has no DT reservation or clock enable, and `memory_map.h:29` says "DO NOT USE FOR E906". | [R] |
| Med | PMP is never configured (`Pmp::init` has no caller). There is no watchdog, and a crash isn't reported to Linux. | [R] |
| Low | `.fastcode` is an orphan section (not in any linker script). MCOR bits may invalidate the BTB, not the I-cache [I]. `track_memory_footprint.py` checks only `nm -u`, uses the LP64 mangled name, and has `except: pass`. | [R] |

**Memory facts:**
- **From the linker script:** `SRAM_FAST` is 192 KB at `0x3FFC0000`. `SRAM_A3_2` is 128 KB, and the IPC region is 128 KB. Stack is 8 KB. The coroutine pool is 4096 B, sized from `gps_imu_app`; the `e906_repl_app` task set wasn't measured.
- **From commit `7bcfa01`:** `SRAM_FAST` 169.8/192 KB (88.5 %). These couldn't be re-checked because no ELF exists on disk.
- **Heap symbols:** the commit says there are none, but engineering_log 2026-10-21 says `free`/`_malloc_r` are linked through sized `operator delete` and "not fixed".
- **Headroom:** 88.5 % of `SRAM_FAST` leaves about 22 KB for the redesign.

**FreeRTOS app:**
- It links `hal::Timer`.
- `platform_profile_start`, `arm_wake` or `set_compare` would reprogram the FreeRTOS tick [R].

**Pico:**
- 15 s blocking Wi-Fi connect in init.
- A null `udp_new` is never retried.
- No watchdog.

**ESP32-P4:**
- No platform layer at all.

---

## 10. Trace and observability

The design is sound:
- The host decoders match the C++ structs.
- A C++→Python test checks that they agree.

Problems:
- **Console flood:** fixed only for the trace endpoint. The TLP endpoint has no lease and `host_addr_` is never cleared, so it can flood the same way [R][I].
- **Schema files:** `trace/barectf_config.yaml` and `trace_schema.json` lack streams 3/4/5. The YAML header also omits `reserved[3]`/`pad`, so it doesn't describe the 40 B wire header (breaks AGENTS §7) [R].
- **Hot-path cost:** `trace_imu` runs unconditionally in the 8 kHz `await_resume`, and timeline is on by default. That is about 500+ RPMsg packets per second with no measured cost [R].
- **Zero-copy:** `platform_send_telemetry` copies on E906 and Pico, which contradicts [SPEC-ARCH-08] [R].
- **Timestamps:** `cycles_now` is taken once per drain, so an ISR span can decode to about −21.5 s [R].
- **Docs:**
  - The `tracer.hpp:73-76` comment says `name[12]`/`token[16]`; the struct uses 8/11.
  - The CTF header struct is defined three times in Python.

---

## 11. Apps and fusion

| Sev | Finding | Where |
|---|---|---|
| High | `gps_imu_app/platforms/allwinner_e906/main.cpp` writes `*(0x40000004)=uptime`. With `e906_ddr.ld`, that address is the `num` field of the live resource table. It also writes DDR `0x4E010004`. | [R] |
| High | When the SPI queue is full, `await_suspend` returns `false` and the producer loops without ever suspending, livelocking `step()` on a single core [I]. | `icm42688p.hpp`, `gps_imu_tasks.hpp:109` [R] |
| High | Not paced by DRDY. `dt` is taken at pop time, so after a backlog dt is a few µs. Gaps over 50 ms are replaced with 1 ms. `g_sensor_ring` has no consumer. | `gps_imu_tasks.hpp:90,153-156`, `main.cpp:37-54` [R] |
| Med | `has_mag_` is never cleared, so heading keeps rotating after a magnetometer dropout. GPS course correction writes Euler yaw, which the next IMU update overwrites, so it has no effect. The D-term spikes on the first sample and there is no anti-windup. NaN cast to int is UB. | `attitude_filter.hpp:115,195,211,299` [R] |
| Med | `serial_loopback` E906 main busy-polls. Its frame detection needs exactly 64 B per read. It does a misaligned cast and prints a hard-coded "CPU < 1 %". | [R] |
| Low | `seq % 10` is a prescaler, which §5 bans. `printf("%f")` without `_printf_float` prints blank. Release flags end up at `-O3`. `e906_bench` copies payloads. | [R] |

---

## 12. Build, tests and CI

| Target | Configures? | Tests in ctest | Notes |
|---|---|---|---|
| host | yes | 4 CppUTest, 16 sim, 18 examples, ~14 Python | `test_coro_combinators` isn't registered, but docs say PASS. pytest tests are silently skipped without pytest. |
| linux | yes | none | |
| e906 | yes | none | `e906_freertos_app` skipped (submodule missing) |
| pico2w | yes | none | **No `--specs=nano.specs`** (breaks the toolchain rule) |
| esp32p4 | yes, but with the host compiler | none | Links a library that doesn't exist. Only an Xtensa toolchain file, but P4 is RISC-V. |
| zynq7000 | yes, host compiler | none | No ARM Linux toolchain file |
| cocotb | custom targets only | **0** | `test_asp_top_cocotb` known failing |
| CI | — | host + ASan | No TSan, no cross builds, no cocotb. `tools/test_*.py` never run. |

---

## 13. Spec honesty and process

**Tool output [V]:**
```
$ python3 tools/audit_specs.py
Tagged in code:       35
Traceability Coverage:100.0%
$ python3 tools/spectrace_matrix.py --matrix
  VERIFIED    : 28
  STALE       : 100
  IMPLEMENTED : 34
  PARTIAL     : 6
  SPEC-ONLY   : 22
Complete (implemented AND verified): 28/190 = 14.7%
```
`audit_specs.py` reads only `docs/DESIGN_SPECIFICATION.md`, which holds 35 of the 190 requirements.

### Unverified claims (most important) [R]

| Where | Claim | Evidence |
|---|---|---|
| README.md:167 | "9.57 µs doorbell" | None. engineering_log:1483 calls it a hard-coded fake Studio string. |
| README.md:69-70 | "<100 B frame, 2 KB stack, 15-cycle switch" | None; TODO.md:115 admits they're unmeasured. The pool is 60 KB by default. |
| README.md:151, 168-170, 791 | "formally verified", "100 % traceability/pass rate", "16 verified examples", "proves 100 % coverage" | Contradicted by the matrix and by the failing cocotb test |
| AGENTS.md:121 | "sub-16 KB binaries" | The E906 region is 192 KB, and the toolchain says -O0 overflows it |
| silicon-api-atlas.md:16 | "< 100 ns" ISR handoff | No benchmark |
| targets/SPECIFICATION.md:243, 656; linux SPEC:63; CUBIE_A5E_CYC1000_INTEGRATION.md:54 | ns-level latency and jitter | None |
| serial_loopback SPEC:22-23 | "~18 ns suspend, zero busy-poll" | The E906 main busy-polls |
| gps_imu SPEC:13, 162 | "100 % portability, zero #ifdef", "every requirement verified" | ESP32-P4 is broken; tags only |
| SPEC-APP-02/05/06/08/09/10 | Verified by tests that don't exercise them | — |
| e906_repl_app/TEST_RESULTS.md:9 | "100 % verification on live silicon" | Board log only |
| e906_repl SPEC:23 | "256 KB GC heap" | The code and the same spec say 128 KB |
| apps/gps_imu_app/README.md:218 | ESP32-P4 "loop 1.8 µs" | Doesn't build |
| engineering_log.md:167 | AMP fences "verified on ESP32-P4" | No code exists |
| 21 `@status Complete` tags | "Complete" | STALE or IMPLEMENTED in the matrix |

**Tag inflation:**
- [SPEC-HAL-02] is "implemented" by a `.dtso`.
- [SPEC-ARCH-04] is "implemented" by a heartbeat main.
- `--check-placement` reports 25 requirements whose target or RTL code is "verified" only by host skills.

### AGENTS.md contradicts the code (§10.7) [R]

1. §2 bans `<mutex>`/`<thread>`/`operator new` under `targets/`, but Linux/Zynq include them and `asp_coro.hpp` defines a pool-backed `operator new`.
2. §4 allows sync calls "pre-scheduler only", but the Pico io_processor calls I2C sync at runtime, and `ITimer::delay_ms` is public and used.
3. The toolchain rule requires `nano.specs`, but Pico links full newlib. "Sub-16 KB" is false.
4. §6 says ESP32-P4 is RV32IMAFDC. It may be single-precision [I].
5. The E906 memory rule is broken by the raw writes in the `gps_imu` platform main.
6. §2 / [SPEC-ARCH-08] (no copies) is broken by the telemetry egress and the bench.
7. §11 "registered" doesn't mean ctest runs it.

---

## 14. AbstractX Studio and host tools

**What's good [R]:**
- The CTF stream 3/4/5 decoders, the `STUDIO_TOOLS` table and the pop-to-canvas placeholder are small and match the firmware.
- They're tested against bytes produced by the C++ code.

**AGENTS §8 invariants:**

| Invariant | Status |
|---|---|
| USER window is application-domain only | **Violated**: SSH controls, packet rate and the UDP pipeline label (`flight_plugin.py:469-522`) |
| USER window floats by default | **Violated**: docked (`abstractx_studio.py:3858`) |
| One copy of each surface | **Violated**: "Dedicated Window" starts `flight_display.py`, which draws a whole Core Studio tab again and opens a second UDP:9870 listener |
| TLP debugger and event log docked, not also tabs | Held |
| Pop to Canvas / Pop In | Held for tabs. "Pop In" on a docked window resets the whole layout. |

| Sev | Finding | Where |
|---|---|---|
| High | `crc_ok=True` is hard-coded. The bridge trailer is `0xDEADBEEF`, not a CRC. | `abstractx_studio.py:1319,3359` [R] |
| High | The first `tag==2` branch catches all GPS frames, so the GPS rate is always 0 and altitude is never set. | `abstractx_studio.py:1367` vs `:1442` [R] |
| High | "Fast GPS" and "Fast Compass" send nothing, but log that they sent. | `flight_plugin.py:510-516` [R] |
| Med | Live COG and drift are synthetic (`sin(sim_phase)`), which [SPEC-STUDIO-14] forbids. | `flight_plugin.py:445` [R] |
| Med | The timeline sticks after a board reset (`max(cycles)`). | `live_profile.py:194` [R] |
| Med | Datagrams are silently discarded (`except Exception: sleep`). `bind` outside try, so the receiver thread can die silently. | `abstractx_studio.py:1461,1291` [R] |
| Med | scp, ssh, sleep and MemBrowse subprocess calls run on the GUI thread. The "0 B Heap (Pass)" badge ignores the data. "0.00 % loss" is a literal. | [R] |
| Med | The live path never uses `CtfSchema`; offsets are hardcoded (breaks AGENTS §7). The tag 2 decode doesn't match firmware `GpsFixPayload`. | `abstractx_studio.py:1322-1455` [R] |
| Low | `flight_display` duplicates surfaces. `coro_frames`/`coro_names` grow from untrusted input. No reconnect. `StrictHostKeyChecking=no` on root. Sim adds 40 per tick to fake 8 kHz. Dead code. | [R] |

**Structure:**
- `abstractx_studio.py` is 4011 lines.
- `TelemetryState` is about 1100 lines and mixes ingest, state, CLI, simulation and subprocess handling.

---

## 15. Flight-controller API parity (Betaflight / INAV)

Betaflight and INAV are the reference open-source flight-controller firmwares. Their driver and service APIs show what a flight stack built on AbstractX has to support. This section compares their API **types** with AbstractX.

**Sources:** Betaflight 4.x and INAV 6–7 from memory: `drivers/bus*.h`, `bus_spi.h`, `serial.h`, `io.h`, `dma*.h`, `motor.h`, `dshot_command.h`, `scheduler/scheduler.h`, `rx/rx.h`, `flight/failsafe.h`, `fc/runtime_config.h`, `pg/*.h`. Names and fields are approximate; **verify against the current sources** before quoting them in a spec.

**Licensing:** Betaflight and INAV are **GPLv3-only**. AbstractX is dual GPLv3/commercial (AGENTS §11), so it can adopt their **API ideas but must not copy their code**. Copying would prevent commercial licensing.

### 15.1 Subsystem comparison

| # | Subsystem | Betaflight / INAV | AbstractX today | Gap → where it lands |
|---|---|---|---|---|
| 1 | **Bus device** | BF `extDevice_t`: bus pointer, CS pin, per-device SPI clock divider and mode. `busDevice_t` holds the bus instance, its DMA streams, current segment, `useDMA`. INAV `busDevice_t` comes from hardware descriptor tables (`BUSDEV_REGISTER_SPI/I2C(tag, hwType, bus, csTag, irqTag, …)`) found with `busDeviceInit()`. | No device object. Settings are bus-wide at `init()`, CS is hard-wired. | `SpiDevice`/`I2cDevice` + a **device registration table** (INAV-style descriptors) → D2, D8 |
| 2 | **Segment transactions with callbacks** | BF `busSegment_t { txData, rxData, len, negateCS, callback }`. `spiSequence(dev, segments)` starts a **non-blocking DMA** chain; `spiWait()`/`spiIsBusy()`. Each segment's callback returns `BUS_READY` / `BUS_BUSY` (run again) / `BUS_ABORT`. Linked segments queue another device's transaction behind the current one. | One flat tx/rx pair; no segments, no per-segment callback, no chaining | D1 transactions + D2 op lists + a **per-op callback with Continue/Repeat/Abort** (added to D1) |
| 3 | **Register helpers** | `spiReadRegMskBufRB`, `spiWriteReg`, `spiWriteRegRB` (with delay), `busReadRegisterBuffer`, `busReadRegisterBufferStart` (non-blocking start, check later), `i2cRead`/`i2cWrite`/`i2cReadBuffer`/`i2cBusy` | 8-bit, 1-byte helpers; status lost | `Regmap` + `Result<T>` → D2 |
| 4 | **Sensor driver model** | Vtables per sensor class: `gyroDev_t` (init, read, temperature, intStatus, EXTI pin, DMA read duration), `accDev_t`, `magDev_t`, `baroDev_t` (a start/get conversion state machine). **Detection** probes WHO_AM_I across a list of candidate parts. Per-board **sensor alignment**. **Gyro EXTI → `spiSequence` from the ISR**, so the sample is ready when the gyro loop runs. | One hand-written ICM-42688 driver with no detection, alignment or baro state machine. Not DRDY-paced. | Sensor-class interfaces + probe tables + alignment + EXTI-triggered transactions → D8, P2-04 |
| 5 | **Serial port API** | `openSerialPort(identifier, function, rxCallback, rxCallbackData, baud, mode RX/TX/RXTX, options)`. Options: `SERIAL_INVERTED`, `SERIAL_STOPBITS_2`, `SERIAL_PARITY_EVEN`, `SERIAL_BIDIR` (half duplex), `_PP`/`_NOPULL`. One vtable: `serialWrite`, `serialRxBytesWaiting`/`serialTotalRxWaiting`, `serialTxBytesFree`, `serialRead`, `serialSetBaudRate`, `isSerialTransmitBufferEmpty`, `serialSetMode`, `beginWrite`/`endWrite`. The same vtable is implemented by **UART, soft-serial and USB VCP**. **Port functions:** MSP, GPS, RX_SERIAL, telemetry (FrSky, SmartPort, CRSF, MAVLink, LTM, HoTT), blackbox, ESC sensor, VTX (SmartAudio, Tramp), RunCam, lidar, and `findSerialPortConfig(function)`. Serial passthrough (ESC 4-way, VTX). Per-UART DMA. | One UART, baud only, TX-only async, sync RX poll | D3 UART v2 + a **port-function registry** + an `rxCallback` per byte (ISR) as an option + soft-serial/VCP behind the same `IUart` → D8 |
| 6 | **RC receiver** | `rxRuntimeState_t` with `rcReadRawFn`, `rcFrameStatusFn`, `rcProcessFrameFn`, `rcFrameTimeUsFn`. Serial providers: SBUS (100 k 8E2 inverted), CRSF/ELRS (420 k, telemetry back), IBUS, FPort (inverted, half duplex), GHST, Spektrum, SUMD, SRXL2. Frame assembly in the ISR. RSSI and link quality. Channel map and ranges. Feeds failsafe. | None. Only the PWM decoder RTL, whose loss-of-signal detection is broken (§2). | RC subsystem on UART v2 (inversion, half duplex, idle framing) + failsafe → D8, P6 |
| 7 | **Motor output** | `motorDevice_t` vtable: `enable`, `disable`, `isMotorEnabled`, `updateStart`, `write(float)`, `updateComplete`, `shutdown`, `requestTelemetry`. Protocols: PWM, OneShot125/42, MultiShot, Brushed, DShot150/300/600, ProShot. DShot by **timer DMA or bitbang**. **Bidirectional DShot** eRPM telemetry drives the RPM filter. `motorDisarmed[]`. `dshotCommandWrite(index, count, command, INLINE/BLOCKING)` goes through a **command queue, allowed only when disarmed**, with repeat counts (e.g. save settings ×10). | Raw register writes. Three conflicting maps. No protocol abstraction, no command gating, no telemetry. | `MotorDevice` interface over the FPGA DShot core (and Pico PIO) + command queue gated by arming + bidirectional telemetry → D6, D8 |
| 8 | **Arming and failsafe** | `armingDisableFlags`: no gyro, failsafe, RX failsafe, bad RX recovery, throttle not low, angle, boot grace, calibrating, CLI active, MSP, GPS, RPM filter, crash, load, … Arming only when the flags are clear. Failsafe stages (RX loss detected → landing/drop/GPS rescue → landed → recovery), `failsafe_delay`, `failsafe_throttle`, procedure DROP/LAND/GPS_RESCUE. INAV procedures LAND/DROP/RTH/NONE through navigation. | None in firmware or fabric | A firmware arming state + disable-flag set + failsafe state machine, **on top of** the FPGA hardware failsafe (D6) as the last line → D8, P0-01 |
| 9 | **Scheduler** | BF `task_t`: name, `checkFunc` (event-driven), `taskFunc`, `desiredPeriodUs`, `staticPriority` (REALTIME/HIGH/MEDIUM/LOW/IDLE). Stats: moving average and max execution time, age. The **gyro task is synced to gyro samples**. A task runs only if its expected execution time fits before the next gyro deadline. CLI `tasks` shows rate, load and max time. INAV `cfTask_t` with dynamic priority (ageing). | Single-FIFO coroutine dispatcher with no rates, priorities or budgets (§5) | Priority classes + per-task rate and budget + task stats; tasks are coroutines → D4, P1-02 |
| 10 | **Resource and IO ownership** | `IOInit(io, owner, index)` tracks who owns each pin. `resource` CLI remaps pins at runtime. `dmaAllocate(identifier, owner, index)` / `dma`/`dmaopt` CLI give DMA stream ownership with conflict checks. `timerHardware_t` tables. Unified targets (one config file per board). INAV static `target.h` + descriptor tables. | Pins hard-coded in drivers. The host may drive any GPIO (`1U<<pin`). Two DMA owners on one bus. `generated_io_config.hpp` unused. | **Resource ownership table** (pins, DMA channels, timers, bus devices) with owner tags; allocation fails on conflict; board config generated from one source (extends P0-02) → D8 |
| 11 | **Configuration and persistence** | Parameter groups (`PG_REGISTER_WITH_RESET_TEMPLATE`), versioned config with CRC in flash, `save`/`defaults`, CLI `get/set/dump/diff`. INAV generates CLI names from `settings.yaml`. | No persistent parameters. MicroPython REPL and a CLI exist. | Parameter groups (static, versioned, CRC) + storage backend per target (E906: Linux-side file over RPMsg; MCU: flash) + CLI get/set → D8 |
| 12 | **Host and GCS protocols** | **MSP v1/v2** over any serial port (Configurator), **MAVLink** (INAV; QGroundControl, Mission Planner), CRSF telemetry, LTM, SmartPort | Custom TLP + CTF + MicroPython REPL; Studio only | Keep TLP internal; add an **MSP and/or MAVLink gateway** service so standard tools work → D8 |
| 13 | **High-rate logging** | Blackbox: binary log at loop rate with a field-definition header, I/P frames, to flash, SD or serial; a rate divider | CTF trace (similar idea, schema-driven), `trace_imu` ungated with unmeasured cost | Gyro/PID/motor logging at loop rate within a measured budget (P5-06) |
| 14 | **Later features** | Beeper, LED strip, OSD (MAX7456/MSP DisplayPort), VTX control, ESC 4-way passthrough (BLHeli/BlueJay/AM32), ESC sensor (KISS serial), RPM filter, dynamic notch, GPS rescue / RTH / navigation | None | Out of scope until 1–12 exist |

### 15.2 How these map onto the AbstractX redesign

| Flight-controller need | AbstractX mechanism (handoff) |
|---|---|
| Non-blocking segment chains with per-segment callbacks (`spiSequence`) | D1 `Transaction` + D2 `Op` list + per-op callback returning Continue/Repeat/Abort; `Deliver::Isr` for the gyro path |
| Gyro EXTI → DMA read in the ISR → gyro loop | D1 `Trigger::arm_multishot(txn)` + `Deliver::Isr` → SPSC ring → RT-priority coroutine (D4) |
| Per-device SPI speed and mode, CS per device | D2 `SpiDevice` |
| Serial ports with functions and options | D3 `UartConfig` (invert, parity, stop, half duplex) + D8 port-function registry |
| `rxCallback` per byte | D3 driver-level `UartRxHandler` (`on_data`, `Deliver::Isr`) |
| Tasks with rate, priority and budget | D4 priority classes + D8 task table (period, priority, budget, stats) |
| Motor device, DShot commands, arming | D6 (fabric) + D8 `MotorDevice` + arming/failsafe service |
| Resource and DMA ownership | D8 resource table (and D5 "one owner per DMA engine") |
| Parameter groups, CLI, MSP | D8 services |

### 15.3 What AbstractX should do differently from Betaflight/INAV

- **Keep coroutines and the zero-heap transaction model.** Betaflight's callbacks run in ISR context and use global state per bus. AbstractX's explicit `Deliver::Loop`/`Isr` and caller-owned transactions are safer, and they can express the same chains.
- **Keep the hardware failsafe in the FPGA.** Betaflight/INAV rely on the MCU being alive, and AbstractX has a fabric that can enforce zero throttle on its own.
- **Keep the TLP/CTF observability.** Add MSP/MAVLink as gateways rather than replacing it.

### 15.4 Requirement: API compatibility with Betaflight and INAV

Borrowing ideas is not enough. AbstractX must **run Betaflight/INAV code against its HAL**, or **be called through their APIs**, so that their flight code (sensor drivers, RX, PID, mixer, failsafe, OSD, MSP, navigation) works on AbstractX targets.

Today none of the surfaces listed in §15.1 exist, so there is **0 % compatibility**.

#### Ways to be compatible

| Option | How | Pros | Cons |
|---|---|---|---|
| **A. AbstractX as a Betaflight/INAV platform** (recommended) | A clean-room **compat shim** (`compat/betaflight`, `compat/inav`) implements their platform and driver interfaces (time, IO/EXTI, bus, serial, DMA, timer, motor, config storage, system) on top of the AbstractX HAL. Their unmodified or lightly patched `src/main` builds as a new "target". | Their flight code runs as is; upstream updates can be merged; the native AbstractX API stays primary. | The interface surface is large. Their runtime model (bare-metal loop, ISR callbacks, `delay()`/`spiWait()` busy-waits) conflicts with AGENTS §3–4, so it needs its own execution domain. Memory is tight on E906. |
| B. Link their drivers into AbstractX apps | Call `busReadRegisterBuffer` etc. from AbstractX code | Reuses sensor drivers | Pulls GPLv3 into every app that uses them. Their drivers assume their globals and scheduler. Not recommended. |
| C. Process / processor boundary | Run Betaflight/INAV SITL (both have one) on the Linux side (A527 or Zynq PS). AbstractX on the E906 or FPGA is the real-time I/O coprocessor, connected through MSP/TLP. | Clean licence boundary (separate programs); little porting | Control-loop latency crosses RPMsg/UIO; their SITL expects simulated sensors, so a new I/O bridge target is needed; not suitable at 8 kHz without measurement |

**Decided 2026-10-22:**
- Option A.
- **Both** Betaflight and INAV through a shared shim.
- **Pico 2 W first**, with the compat domain on Core 1.
- GPLv3-only compat builds accepted.

For now this is an **API design goal**: the AbstractX API must be able to support these surfaces, and no shim code is written yet. Details are in handoff D9 and P7.

#### Licensing consequence (not legal advice)

- **Shim:** written clean-room by AbstractX against the published function signatures, it stays dual-licensable.
- **Combined build:** any firmware that **links** Betaflight/INAV code is a combined work under **GPLv3 only**. That's fine for open-source use, but those builds can't be offered under the commercial licence.
- **Commercial customers** use the native AbstractX API, and optionally option C's process boundary.
- **Record it** in `docs/THIRD_PARTY.md` and in the build system: `ABSTRACTX_COMPAT=betaflight|inav` marks the output as GPLv3-only.

#### Compatibility surface: what the shim must provide

From memory; verify against the pinned upstream version.

| Area | Betaflight surface | INAV surface | Implemented on AbstractX by |
|---|---|---|---|
| Time | `micros()`, `millis()`, `microsISR()`, `delay()`, `delayMicroseconds()`, `getCycleCounter()`, `clockCyclesToMicros()` | `micros()`, `millis()`, `delay()`, `delayMicroseconds()` | `ITimer` (busy-wait delay allowed only before the scheduler starts, or inside the compat domain) |
| GPIO / EXTI | `ioTag_t`, `IO_t`, `IOInit(io, owner, index)`, `IOConfigGPIO`, `IORead/IOWrite/IOHi/IOLo/IOToggle`, `EXTIHandlerInit`, `EXTIConfig`, `EXTIEnable` | same family | `IGpio` + edge triggers (P2-04) + `ResourceTable` (P6-07) |
| SPI | `spiInit`, `spiSetClkDivisor`, `spiSetBusMode`, `spiSequence(dev, segments)` with per-segment callbacks, `spiWait`, `spiIsBusy`, `spiReadWriteBuf`, `spiReadRegMskBufRB`, `spiWriteReg(RB)`, `extDevice_t`/`busDevice_t` | `busDevice_t`, `busRead/busWrite/busReadBuf/busWriteBuf/busTransfer`, `busSetSpeed`, `BUSDEV_REGISTER_SPI` descriptors | D1 transactions + D2 ops + per-op callback (`Deliver::Isr`); `spiWait` waits on the transaction |
| I2C | `i2cRead/i2cWrite/i2cReadBuffer/i2cWriteBuffer/i2cBusy`, `busReadRegisterBuffer(Start)` | `busRead/busWrite` on I2C devices, `BUSDEV_REGISTER_I2C` | D2 `I2cDevice` + `Regmap` |
| Serial | `serialPort_t` vtable (`serialWrite`, `serialRxBytesWaiting`, `serialTxBytesFree`, `serialRead`, `serialSetBaudRate`, `isSerialTransmitBufferEmpty`, `serialSetMode`, `writeBuf`, `beginWrite/endWrite`), `uartOpen`, `openSerialPort(..., rxCallback, ...)`, port options (inverted, bidir, parity, stop bits), VCP | same model | D3 UART v2: an always-on ring + `UartRxHandler` (`Deliver::Isr`) for `rxCallback` |
| DMA | `dmaAllocate`, `dmaGetIdentifier`, `dmaSetHandler`, stream ownership | `dmaGetByTag`, ownership | Driver-owned DMA (D5) exposed through `ResourceTable`; the shim reports ownership but never hands raw DMA channels to foreign code |
| Timers / motors | `timerHardware_t` tables, `motorDevice_t` vtable (`enable`, `disable`, `write`, `updateStart/Complete`, `isMotorEnabled`, `shutdown`), DShot/bitbang, bidirectional DShot telemetry | `pwmMotor*`, `timerHardware`, DShot | D8 `IMotorDevice` over the FPGA DShot core (with the D6 fabric failsafe always on) or Pico PIO |
| Config storage | `config_streamer` (flash erase/program), EEPROM emulation, `persistentObjectRead/Write` | `config_streamer` | Storage backend from P6-08 (flash on MCU; Linux file over RPMsg on E906) |
| System | `systemReset`, `systemResetToBootloader`, `failureMode`, LEDs, beeper, `isMPUSoftReset` | similar | Platform layer |
| Target description | Unified target config (`config.h` + CLI `resource`/`timer`/`dma`) | `target.h` + `target.c` (`timerHardware`) | Generated from the board YAML (P0-02 / P4-06) |

#### Mismatches the shim has to bridge

1. **Execution model:**
   - BF/INAV run `while (1) scheduler();` with ISR callbacks.
   - In the shim, their loop runs inside **one AbstractX RT task in a separate compat domain**: its own core (Pico Core 1, or the whole E906) or its own budgeted task.
   - Their `spiWait()`/`delay()` busy-waits are allowed only inside that domain, documented as an explicit exception to AGENTS §3–4.
2. **Callbacks in ISR context:** per-segment and serial `rxCallback`s run with `Deliver::Isr`, matching their expectations.
3. **Global state:** they expect global singletons (one `busDevice_t` per bus). The shim keeps static adapters, and no heap is needed (they don't use one either).
4. **Hardware differences:** motors come from the FPGA instead of timer DMA, the gyro comes from E906 SPI, and so on. Their "target" maps these to AbstractX devices.
5. **Memory:**
   - Betaflight builds usually need hundreds of KB of flash and tens of KB of RAM [I].
   - E906 `SRAM_FAST` has about 22 KB free with MicroPython present, so a BF/INAV image on E906 would need MicroPython removed and SRAM space 1 used. Measure it.
   - Pico 2 W (520 KB SRAM + 4 MB flash) and Linux SITL have room.
6. **Upstream status:** Betaflight is believed to have recent RP2350 (Pico) platform work, and both projects have SITL targets [I: verify]. The Pico 2 W may be able to use or follow upstream's platform layer directly.
7. **Version pinning:** pin one upstream release per firmware, and run a CI job that compiles it against the shim. API drift becomes a build failure, not a surprise.

---

## 16. Consumer check: `inav-abstractx` (`../inav`)

**What it is:** `../inav` is a GPLv3 C++20 coroutine **reimplementation** of INAV/Betaflight flight algorithms, aimed at Cubie A5E, Tang Nano 9K, Pico 2 W, ESP32-P4 and SITL. The real INAV and Betaflight are submodules used only as reference. It locates AbstractX through `ABSTRACTX_PATH=../AbstractX`.

**Does the AbstractX API support it? No.** It uses **none** of the AbstractX runtime or HAL API:
- `coro::`, `hal::`, `abstractx::step/init/spawn/run`, the dispatcher, `AsyncQueue` and `TimerService` have **0 uses** in `src/`, `include/` and `test/` [V].
- The only shared surface is three wire/ring headers (`asp_tlp64.hpp`, `spsc_tlp_ring.hpp`, `pcie_bar_map.hpp`).
- An uncommitted `CMakeLists.txt` change puts inav's **own forked copies** of those three ahead of AbstractX's [V], and the forks are **wire-incompatible** [R].

Everything else it built itself, because the AbstractX gaps in §3 and §5 forced it to.

| AbstractX header | inav's use | Status [R] |
|---|---|---|
| `asp_tlp64.hpp` (47 includes) | Sensors, DShot writes, CRSF/GPS frames, logging | **Forked and incompatible.** inav: 44 B payload, no CRC, LE `make_mem_write`. AbstractX: 40 B payload + CRC32, BE `make_mem_write`. Different enum values (DmaStream 0x00 vs 0x10). With the fork, there are **three** wire formats (AbstractX C++/Python LE, AbstractX RTL BE, inav). |
| `spsc_tlp_ring.hpp` (31) | IMU and logging rings | Forked. Functionally the same; the fork has an acquire-ordering fix AbstractX doesn't have. |
| `pcie_bar_map.hpp` (25) | FPGA BAR addresses | **Forked and address-incompatible** (Baro 0x2000 vs 0x3000, Mag 0x3000 vs 0x4000, Esc 0x4000 vs 0x2000) |

What it re-implemented, and why [R]:

| inav file | Replaces AbstractX | Why | Quality |
|---|---|---|---|
| `src/abstractx_engine/coroutine_task.hpp` | `coro::Task`, sleep, combinators, pool | AbstractX timers never resume; combinators broken | **Polling model:** `TimerAwaiter::await_suspend` is empty [V], so `sleep_us(125)` only lasts until the next poll. `Task::await_suspend` resumes the child inline. The frame pool never frees (`deallocate` is a no-op). `std::chrono` used on bare metal. |
| `include/abstractx/io/async_regmap.hpp`, `async_stream.hpp`, `async_frame.hpp` (untracked) | `ISpi`/`II2c`/`IUart` | No device/regmap; bare `uint8_t`; no RX stream | **A good concept-based regmap design** (`read_reg`/`read_burst`/`update_reg` → `Task<uint32_t>`). Worth adopting into AbstractX (handoff D2). |
| `src/drivers/bus/bus_concepts.hpp`, `src/target/*/*_abstractx_backend.hpp` | AbstractX HAL backends | AbstractX HAL completes the wrong request | **Every "async" method blocks:** `spi_write_read_blocking`, `i2c_*_blocking`, `spi_device_polling_transmit` and `ioctl` wrapped in a coroutine. DMA claimed but unused. spi1/i2c1 hard-coded. |
| `src/scheduler/scheduler.hpp` | (no AbstractX priorities) | INAV task table | A port of INAV `scheduler.c`. Used only by tests, not by any target main. |
| `src/config/*`, `src/msp/*`, `src/logging/*` | (missing in AbstractX) | Config storage, MSP, blackbox | Own implementations (Pico flash; Asio/POSIX/lwIP transports) |

**Gaps that block a real flight [R]:**
- Mixer output is discarded (`flight_engine_template.hpp:168`).
- DShot only builds TLPs; PIO is a stub.
- RC and GPS parsers are not wired into the loop, and there's no UART RX path on Pico.
- AbstractX RTL has no motor safety (§2).

**Parity claims vs tests [V/R]:**
- `test_full_stack_parity.cpp`: "Stack B" (`FullAbstractxStack`) wraps and calls "Stack A" (`FullInavStack`) [V], so "60,000 steps bit-exact" compares the C++ code with itself.
- `test_submodule_differential.cpp` doesn't compile the upstream C. It compares against formulas hand-copied into the test. DIFF 1–2 match upstream on a spot check. DIFF 3/5/6/7 only check direction, yet print "100% BIT-EXACT MATCH".
- The tests use `assert`, which a Release build with `-DNDEBUG` turns off. There are no sanitizer runs. The last passing log predates the uncommitted edits.

**What AbstractX has to do so inav can use it** (handoff P7-04):
1. One wire format and BAR map: settle the byte order, freeze the offsets in the spec, and have inav delete its forks.
2. A working coroutine core: continuations, a ticked timer heap, timeouts, safe combinators (P1).
3. A device/regmap layer: adopt inav's `IsAsyncRegmap` concept on fixed HAL completion (P1-01, P2-01).
4. A UART RX stream (P2-03).
5. Priorities, multi-waiter primitives and `Latest<T>` (P1-02, P1-05).
6. A motor path with RTL safety (P0-01, P6-04).
7. Optional: config and MSP service interfaces (P6-08, P6-09).

---

## 17. What works today

- **E906 drivers:** `e906_periph.cpp` UART3/TWI1 by interrupt on CLIC lines 90/91. Board tests: `test_e906_irq.py`, `test_e906_planes.py`, `test_e906_trace.py` [R].
- **E906 core:** the trap entry, CLIC setup, ISA flags and nano.specs are consistent [R].
- **RPMsg:** the vring index handling and zero-copy batching [R].
- **Trace:** CTF decoders agree with the C++ structs, with a cross-language test [R].
- **Host suites:** ASan and CI for host [R]. The steady-state tests for the queue and `gps_imu` paths [R].
- **Shared HAL:** the awaiter contract [R].

---

## 18. Not checked

- No build, ctest, sanitizer, cocotb, synthesis, lint, cross-compile or board run was done for this review.
- No E906 ELF or map exists on disk, so no memory figure was re-measured.
- Hardware facts weren't checked against the TRMs:
  - DRQ numbers;
  - TWI_DRV, SPI and MCOR bit layouts;
  - FIFO depths;
  - RP2350 PL011, stdio and CYW43 pins;
  - the ESP32-P4 ISA;
  - the Zynq package.
- Not read, or read only in passing:
  - the MicroPython C modules;
  - `asp_neopixel_core`, `asp_spi_reg_bank`, `asp_ila_trace`;
  - Zynq UIO bridge internals;
  - Studio CLI and sim bodies;
  - `ctf_schema_loader.py`;
  - mailbox and remoteproc internals in depth.
- [R] items come from review passes that read the code but were not re-checked line by line. Re-read the cited lines before changing anything.
