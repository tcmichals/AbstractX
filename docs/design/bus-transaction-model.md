# Bus Transaction Model: Choosing Between the Merged API and the Review Handoff

**Status:** decided 2026-10-23: **option C**, with the step outcomes `Continue / Repeat / Finish /
Fail` (section 4). Written into SPEC-HAL-08..10 and the new SPEC-HAL-12 (step callbacks), HAL-13
(triggered reads) and HAL-14 (bus lock) in `docs/DESIGN_SPECIFICATION.md`; API in
`include/abstractx/hal/`, host-tested in `sim/test_bus_protocols.cpp`. No target driver implements
it yet (next: E906).
**Why now:** no target implements the new bus calls yet. The model has to be settled before the
first driver (E906) is written, or drivers get written twice.

Sources:
- **Merged API:** `include/abstractx/hal/{spi,i2c,uart,uart_rx,io_status,async_driver}.hpp`,
  requirements SPEC-HAL-06..11 in `docs/DESIGN_SPECIFICATION.md` (commits `6513616`, `d9318b3`).
- **Review handoff:** the 2026-10-22 system review refers to designs D1-D9 in
  `2026-10-22-handoff-todo.md`. That file is **not in this repository**. Section 3 is rebuilt
  from what the review text quotes from it and must be checked against the original.

---

## 1. What the bus layer has to cover

The protocol needs, condensed from the system review's checklists (§3.2, §3.5, §3.8) and the
project owner's requests:

| Bus | Needs |
|---|---|
| SPI | full duplex / write / read; command then read with CS held; several segments; **dual and quad lanes**; per-device mode, speed, CS polarity; word sizes other than 8; 3-wire; data/command pin between segments; CS setup/hold and inter-word gap; dummy *cycles*; bus lock across transactions; poll a status bit until clear (flash busy); continuous / circular streaming; **transaction started by a pin or timer (IMU DRDY at 8 kHz)**; timeout and cancel |
| I2C | write / read / register read with repeated START; message lists with no-START and STOP control; multi-byte register access; 10-bit and 16-bit register addresses; zero-length probe; SMBus block read (**length decided by the first byte**), PEC; ignore-NACK; per-device speed; timeout, clock stretching, bus recovery; distinct NACK / arbitration errors; bus lock; triggered reads |
| UART | always-on receive with no byte lost between reads; receive N / until idle / until delimiter / with timeout; format, flow control, inter-byte gap; inversion; single-wire half duplex; RS-485 enable; send and detect break; "really sent" completion; errors; several ports; for an RC receiver, frame assembly at interrupt level |
| All | one owner per DMA engine; device drivers never choose DMA vs FIFO (rule: DMA when bigger than the FIFO); no heap; interrupts never resume coroutines; caller-owned buffers whose lifetime is stated |

## 2. Option A: what is merged now (segments + coroutine result)

```cpp
struct SpiDevice  { uint8_t cs; SpiMode mode; uint32_t max_hz; bool cs_active_high, lsb_first; };
struct SpiSegment { span<const uint8_t> tx; span<uint8_t> rx; SpiLanes lanes; bool release_cs_after; };
co_await spi.transfer_async(dev, span<const SpiSegment>{...});   // -> SpiResult
struct I2cMsg     { span<const uint8_t> tx; span<uint8_t> rx; uint8_t flags; };  // Read | NoStart | Stop
co_await i2c.transfer_async(addr16, span<const I2cMsg>{...}, timeout_us);       // -> I2cResult
co_await uart.read_async(min_bytes, timeout_us);   // -> UartRxView: spans into a 4 x 64 B ring
uart.release_rx(n); uart.configure(UartConfig{...});
// every bus: capabilities() + check_*() refuse unsupported requests with NotSupported
```

- **Strengths:** small; the transaction is plain data owned by the caller's coroutine frame;
  capability checks refuse what a chip cannot do before touching hardware; one shared status
  vocabulary and a cancel path; easy to test with fake drivers (5 host tests exist).
- **Cannot express:**
  - any decision taken **during** a transaction (poll-until-ready, SMBus block-read length,
    abort on a bad header byte);
  - a transaction **started by hardware** (DRDY pin, timer) without a coroutine in between,
    which is what an 8 kHz IMU needs;
  - delivery at interrupt level (an RC frame parser that must see every byte);
  - holding the bus across several transactions.

## 3. Option B: the review handoff (rebuilt from the review text, not verified)

As quoted in the review (§3.11, §15.2):

- **D1 Transaction:** a caller-owned transaction object, completion is pluggable:
  `Deliver::Loop` (resume a coroutine from the event loop) or `Deliver::Isr` (a callback at
  interrupt level, with context). Each operation can have a **callback that returns
  Continue / Repeat / Abort** (Betaflight `busSegment_t` style). `Trigger::arm_multishot(txn)`
  lets a pin or timer start a prepared transaction repeatedly (gyro EXTI -> DMA read in the ISR).
- **D2 Devices and operations:** `SpiDevice` / `I2cDevice` own their settings; a transaction is a
  list of `Op`s; a `Regmap` layer for register access; `Result<T>` instead of bare values.
- **D3 UART v2:** an always-on receive ring plus an optional driver-level handler
  (`UartRxHandler::on_data`, `Deliver::Isr`) for byte-level protocols such as RC receivers.
- **D4/D5:** priority classes in the runtime; DMA owned by the driver, one owner per engine.

- **Strengths:** covers every row in section 1, including triggers and in-flight decisions;
  maps directly onto Zephyr RTIO, Linux `spi_message` and Betaflight/INAV (the review's
  compatibility goal).
- **Costs:** bigger surface; callbacks at interrupt level are easy to get wrong (they must be
  bounded and must not resume coroutines); two delivery paths to test on every target.

## 4. Option C (recommended): A's data model plus B's three missing mechanisms

Keep everything in Option A. Add only what A cannot express, each behind a capability so a
target can refuse it:

```cpp
// (1) A step callback on a segment / message: runs at interrupt level when that step completes,
//     sees the bytes just received, and decides what happens next. Bounded, never resumes.
// As built: BusStep { Continue, Repeat, Finish, Fail } (bus_step.hpp). Finish = end early with Ok
// (e.g. FIFO empty); Fail = end with Aborted (bad header, bad SMBus count). Repeat is limited by the
// hook's max_repeats; one more ends the transaction with Timeout. Every early end releases the bus.
enum class Step : uint8_t { Continue, Repeat, Finish, Fail };
using StepFn = Step (*)(void* ctx, std::span<const uint8_t> rx) noexcept;
struct SpiSegment { /* ... as now ... */ StepFn on_done{}; void* ctx{}; };
struct I2cMsg     { /* ... as now ... */ StepFn on_done{}; void* ctx{}; };
//   poll flash busy:  segment {tx=RDSR, rx=1 B, on_done = busy ? Repeat : Continue}
//   SMBus block read: msg 1 reads the count; its on_done sets msg 2's length (<= 32)

// (2) Triggered transactions: arm a prepared transaction on a pin edge or a timer; each run
//     pushes its result into an SPSC ring the coroutine drains (the 8 kHz IMU path).
template <class Bus, class Result, size_t N> struct Trigger {
    IoStatus arm(const SpiDevice&, std::span<const SpiSegment>, TriggerSource, SpscRing<Result, N>&);
    void disarm();
};
//   imu: arm({dev, {write(reg), read(14 B)}}, Gpio::rising(DRDY), samples); co_await samples.pop();

// (3) Bus lock: several transactions with no other device in between (flash WREN + PROGRAM).
auto lock = co_await spi.lock(dev);     // RAII guard; other devices queue behind it

// Per-segment attributes, all capability-checked (default = today's behaviour):
struct SpiSegmentExtra { uint8_t word_bits{8}; uint32_t hz{0}; uint16_t dummy_cycles{0};
                         uint16_t cs_setup_ns{0}, cs_hold_ns{0}, word_gap_ns{0};
                         int8_t dc_level{-1}; bool three_wire{false}; };
// I2C: I2cDevice{addr16, max_hz} for per-device speed; flags IgnoreNack, RecvLen (SMBus).
// UART: UartConfig gains invert_rx/invert_tx, half_duplex, rs485_de_pin, send_break(); several
//       instances via get_uart(n); an optional on_data(span) handler at interrupt level for
//       byte-level protocols, beside (not instead of) the ring.
```

Why C rather than B: the data model, capability checks, cancel and tests already exist and
match the owner's decisions (one driver per bus, DMA by FIFO size, UART receive always DMA into
4 x 64 B buffers). C adds B's three mechanisms where they are needed instead of replacing the
model, so the five host tests keep working and only new behaviour needs new tests.

## 5. Coverage

| Need | A (merged) | B (handoff) | C (recommended) |
|---|---|---|---|
| Segments, CS held, dual/quad, per-device mode/speed | ✅ | ✅ | ✅ |
| I2C message lists, 10-bit, timeout, recovery, distinct errors | ✅ | ✅ | ✅ |
| UART ring, until idle, timeout, format, errors | ✅ | ✅ | ✅ |
| Word size, CS timing, dummy cycles, D/C pin, 3-wire | ❌ | ✅ | ✅ (segment attributes) |
| Poll until ready, SMBus block read, abort mid-transaction | ❌ | ✅ | ✅ (step callback) |
| DRDY / timer-triggered transactions | ❌ | ✅ | ✅ (Trigger + ring) |
| Bus lock across transactions | ❌ | ✅ | ✅ |
| Byte-level UART protocols at interrupt level (RC) | ❌ | ✅ | ✅ (on_data beside the ring) |
| UART inversion, half duplex, RS-485, break, several ports | ❌ | ✅ | ✅ |
| Fits the existing tests and owner decisions without rework | ✅ | ⚠️ rework | ✅ |
| Betaflight/INAV shim can map onto it | ⚠️ | ✅ | ✅ (step callback = segment callback, Trigger = EXTI read) |

## 6. Rules every option must keep (AGENTS.md)

- No heap; transactions and rings are caller- or driver-owned static memory with a stated lifetime.
- Interrupt-level code (step callbacks, triggers, `on_data`) never resumes a coroutine; it pushes
  into an SPSC ring or posts to the dispatcher.
- Awaiters write their own result slot, return `false` on a refused submit, bind spans in
  `await_suspend`.
- Zero copy: the reader gets spans into the driver's buffers.
- Device drivers never choose DMA; the bus driver applies the FIFO rule per segment.

## 7. After the decision

1. Write the chosen additions into SPEC-HAL-08..10 (and a new requirement for triggers and bus
   lock) before any code.
2. Host tests with fake drivers for each new mechanism, including 1000-iteration steady state.
3. First implementation on the E906: UART receive DMA ring, TWI driver mode (one interrupt per
   compass read), SPI with the FIFO rule; board tests for each.
4. Then Linux (spidev `SPI_IOC_MESSAGE`, `I2C_RDWR`), then Pico 2 W.
