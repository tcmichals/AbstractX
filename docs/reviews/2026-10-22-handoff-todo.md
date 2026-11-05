# AbstractX Handoff: Fix Plan and TODO (2026-10-22)

**Audience:** an AI coding agent, or an engineer, working in this repository.
**Input:** the findings in [`2026-10-22-system-review.md`](2026-10-22-system-review.md). `§N` references below point to sections of that file: buses §3, each target §4, coroutines and channels §5, Betaflight/INAV parity §15, `../inav` consumer check §16.
**Contents:** design decisions D1–D8 (transactions and callbacks, devices and ops, UART, bus API reference D3a, runtime and channels D4, processor framework D5, motor safety D6, DMA vs interrupt D7, flight-controller services D8, Betaflight/INAV API compatibility D9), then the TODO in phases P0–P7.
**How to use this file:** do the phases in order. Within a phase, tasks can run in parallel unless one lists a `Depends on`. Each task gives the files to touch, the spec step that comes first, acceptance criteria, and the checks to run.

---

## 0. Rules for the agent (read first)

1. **Follow `AGENTS.md` §0 (the operating loop) on every task:**
   - Retrieve the governing `[SPEC-*]` sections first: `python3 tools/spectrace_matrix.py --changes --prompt`.
   - **Change the spec before the code.** Each task names the spec to change.
   - Tag every changed block with `// @impl [SPEC-*] <spec file>`.
   - Record each lesson in `engineering_log.md` (`python3 tools/log_mistake.py ...`), with a category: `product-bug`, `tool-bug`, `false-alarm`, `process-gap` or `hardware-fact`.
   - For each bug fix, add a test that **fails on the old code and passes on the new**, and say so in the commit message.
   - Quote the commands you ran and their output in the commit message. Never write "verified", "100 %" or a number into Markdown unless that same change ran the command that produced it.
2. **Run the gates from AGENTS §10.2** before any commit touching `include/`, `src/`, `targets/linux/`, `tests/` or `sim/`: host, ASan/UBSan with `-fno-sanitize-recover=all`, and TSan.
3. **Hardware safety:**
   - Until P0-01 to P0-03 are merged **and** checked on the bench, never run motor outputs with props fitted.
   - Never write motor registers from a test without a current-limited supply.
4. **Scope:** fix what the task says. Don't add features beyond it. If a task turns up something new, add it to this file under "Discovered" (end of file) instead of fixing it on the side.
5. **Hardware facts:** anything marked "verify (TRM)" must be checked against the vendor manual before code depends on it. Record the result as a `hardware-fact` entry in `engineering_log.md`.
6. **Git:** author `Tim Michals <tcmichals@gmail.com>`. No AI `Co-Authored-By` trailers (AGENTS §12). Commit and push when a task is done.
7. **End every report** with a list of what you did *not* check (target, hardware, tool).

**Task format:**
```
### <ID> <title>
Why: finding reference (§ in system review)
Spec first: which [SPEC-*] to add or change
Files: ...
Do: steps
Accept: observable criteria
Checks: commands / skills (.agents/skills/<name>)
Depends on: ...
```

---

## 1. Design decisions (normative inputs for the spec changes)

The tasks implement these designs. Copy the relevant parts into the `SPECIFICATION.md` files as `[SPEC-*]` requirements with `Verified by` lines. The C++ below shows the shape of the API; exact names can change in review.

### D1. Transaction and completion model (shared by every driver)

Modelled on Zephyr RTIO (submission and completion queues, chains), Tock (split-phase calls that hand buffer ownership back), and Linux `spi_message`/`i2c_msg`.

**Core rules (spec text):**
1. **Exactly once:** every `submit()` produces exactly one completion. That holds for success, error, timeout and cancel.
2. **Ownership:** from `submit` until the completion, the driver owns the transaction and its buffers. After the completion they belong to the caller again. Resubmitting from inside the completion callback is allowed.
3. **Lifecycle:** the **base class** owns the state machine (`Idle → Queued → Active → Done → Idle`) and the in-flight transaction. A target implements only `start(const Txn&)` and calls `complete(status, bytes)` from its ISR, DMA handler or worker. A target never pops the queue.
4. **Delivery** of the completion:
   - `Deliver::Loop` (default): the ISR writes `result` and links the transaction onto an intrusive completed list. `abstractx::step()` runs the callback. This path can't drop a completion, because the transaction is its own list node.
   - `Deliver::Isr`: the callback runs in the ISR. It may only push to an SPSC ring or `submit()`. It may not resume a coroutine or block.
5. **Coroutine API:** an adapter (`co_await bus.run(txn)`) over the callback path. The awaiter's destructor cancels an unfinished transaction.
6. **Deadline and cancel:** every transaction may carry `deadline_us`. The timer driver completes it with `Timeout`. `cancel()` on a queued transaction removes it; on an active one it aborts if the hardware can, otherwise it waits. Either way exactly one completion is delivered.
7. **Chains:** `chain_next` starts only after the previous link succeeds. On error, the rest complete with `Cancelled`, and only the last link's callback runs.
8. **Unsupported features** complete with `Status::Unsupported`. No silent no-ops returning `true`.
9. **Per-op callback** (optional, Betaflight `busSegment_t` style): an `Op` may carry `on_op_done` returning `Continue`/`Repeat`/`Abort`. It runs in ISR context under the `Deliver::Isr` restrictions.

```cpp
enum class Deliver : uint8_t { Loop, Isr };
struct TransactionBase : IntrusiveNode {
    using Callback = etl::delegate<void(TransactionBase&)>;
    std::span<Op>      ops;
    Callback           on_done{};
    Deliver            deliver{Deliver::Loop};
    uint32_t           deadline_us{0};
    TransactionBase*   chain_next{nullptr};
    Result             result{};   // status, bytes_done, failed_op, t_start_us, t_end_us
    std::atomic<State> state{State::Idle};
    void*              user{nullptr};
};
template <class Dev> struct Transaction : TransactionBase { const Dev* dev{nullptr}; };

Status submit(TransactionBase&) noexcept;   // never blocks, never completes inline
bool   cancel(TransactionBase&) noexcept;
// adapters: co_await bus.run(txn); txn.then(&Obj::fn, obj); txn.to_ring(ring, Deliver::Isr)
```
**Dispatcher change:** the ready-queue entry becomes `{fn, ctx}`, and a coroutine resume is just `fn = resume_trampoline`. `step()` drains the completed list first, then the ready queue.

### D2. Devices, operation lists and register maps (SPI, I2C)

Modelled on Zephyr `*_dt_spec`, Rust `embedded-hal` `Operation`, Linux regmap and RIOT register flags.

```cpp
enum class OpKind : uint8_t { Write, Read, Transfer, Delay, Configure, AwaitEdge };
struct Op {
    OpKind kind;
    std::span<const uint8_t> tx{};  std::span<uint8_t> rx{};
    uint16_t len{0};            // 0 -> max(tx,rx); base bounds-checks against both spans
    uint16_t flags{0};          // I2C: Restart|NoStart|Stop|RecvLen|IgnoreNack ; SPI: CsKeep|CsOff|DcHigh
    uint8_t  lines{1};          // SPI 1/2/4/8
    uint8_t  dummy{0};          // SPI dummy cycles
    uint8_t  fill{0x00};        // SPI read-only fill byte
    uint32_t arg{0};            // Delay us, per-op hz override, edge pin
};
struct SpiDevice { SpiBus& bus; uint8_t cs; SpiMode mode; uint32_t hz; uint8_t word_bits{8};
                   bool lsb_first{false}, cs_active_high{false}, three_wire{false};
                   int16_t dc_pin{-1}; uint16_t cs_setup_ns{0}, cs_hold_ns{0}; };
struct I2cDevice { I2cBus& bus; uint16_t addr; bool ten_bit{false}; uint32_t hz{400'000};
                   uint16_t stretch_timeout_us{0}; bool pec{false}; };
struct RegFormat { uint8_t addr_bytes{1}, val_bytes{1}, read_mask{0}, write_mask{0}, auto_inc{0};
                   std::endian order{std::endian::big}; };
template <class Dev> struct Regmap { /* read(reg, span), read<T>(reg) -> Result<T>, write, update_bits */ };
```
- The base checks every op's `len` against its spans **before** the target sees it. That fixes the RX-overflow bugs on E906, Pico and Zynq by construction.
- Register reads return `Result<T>`, never a bare byte. Mark them `[[nodiscard]]`.
- I2C mux (TCA9548A) is an `I2cBus` decorator.
- Target (slave) modes are separate interfaces: `ISpiTarget`, `II2cTarget`.
- Platform accessors take an index: `get_spi(n)`, `get_i2c(n)`, `get_uart(n)`, or a named role.

**Coverage this gives.**
- **SPI:** full duplex, write, read with fill byte, command then read (no padding), command/address/dummy, segments with CS held, dual/quad, 3-wire, word size, data/command pin, per-device mode and speed, CS timing, clocks with CS high, bus lock, poll-until (helper), triggered (D6), streaming (`SpiStream`, later).
- **I2C:** write, read, repeated START, gather write, message lists, multi-byte and 16-bit registers, 10-bit addresses, probe and ACK polling (zero-length write), SMBus block (`RecvLen`) and PEC, general call, ignore-NACK, per-device speed, stretch timeout, recovery (`co_await bus.recover()`), lock, mux.

### D3. UART

Modelled on the Zephyr async UART API, Rust `embedded-io` `BufRead`, and ESP-IDF UART events.

- **RX is an always-on stream, not a queue of requests.** The driver keeps a ring filled by interrupt or cyclic DMA. A single reader does `co_await rx.read(buf, Until{min, delimiter, idle_chars, deadline})`. The result reports: bytes; reason (Delimiter, Idle, Full, Timeout, Break); errors (overrun, framing, parity, ring_overflow); and `first_byte_us`.
- **Driver-level hook** for zero-copy users: `rx_enable(handler, first_buf, idle_chars)`, with events `on_data` / `on_buf_request` / `on_buf_release` / `on_error`.
- **TX** is a normal transaction with `TxDone::{Queued, Sent}`. `Sent` waits for TEMT, which RS-485 and half duplex need.
- **Config:** `UartConfig{baud, data_bits, parity, stop, flow, tx_invert, rx_invert, duplex, de_pin, rx_idle_chars}`. Change it at runtime with `co_await uart.configure(cfg)`, which returns `Unsupported` if the hardware can't do it.
- **Other operations:** `send_break(us)`, `drain()`, `transact(tx, rx, Until)` for half duplex, and a `caps()` query.
- **Delete** the sync `read`/`write`/`flush` from `IUart`. Bring-up printing moves to a separate `IBringupConsole`.
- **Framing:** COBS, SLIP, UBX and NMEA decoders sit on top of `read()`, outside the HAL.

### D3a. Bus API design reference (SPI, I2C, UART)

This section brings D1–D3 together into one API, with usage examples and edge-case rules. The gaps it closes are listed in system review §3.2, §3.5 and §3.8. Names are proposals.

#### SPI: every transfer type expressed with one awaitable

```cpp
// One per chip, static. The bus re-applies settings only when the device changes.
struct SpiDevice {
    SpiBus& bus; uint8_t cs;                // or kNoCs for manual CS
    SpiMode mode; uint32_t hz;
    uint8_t word_bits{8}; bool lsb_first{false}, cs_active_high{false}, three_wire{false};
    int16_t dc_pin{-1};                     // data/command GPIO (displays)
    uint16_t cs_setup_ns{0}, cs_hold_ns{0};
    RegFormat regs{};                       // for Regmap
};

co_await dev.transact(ops, deadline);                       // types 1–13: CS held across ops
co_await dev.write(tx);  co_await dev.read(rx, fill);       // 2, 3
co_await dev.write_then_read(cmd, rx);                      // 4 (no padding)
co_await dev.cmd_addr_read({.cmd=0xEB,.addr=a,.addr_bytes=3,.dummy=6,.lines=4}, rx);  // 5, 7
co_await dev.poll_until(cmd, mask, value, interval, deadline);                        // 15
auto lk = co_await dev.bus.lock();                          // 14 (RAII; released on scope exit)
co_await Regmap{dev}.read(reg, rx);                         // shared with I2C
SpiStream<2> s{dev, bufs};  auto b = co_await s.next(); s.release(b);   // 16 (later)
Trigger t{gpio, drdy_pin, Edge::Rising};  t.arm_multishot(txn);         // 17
SpiBusCaps c = dev.bus.caps();          // quad, three_wire, word_bits_max, max_hz, dma
class ISpiTarget;                       // 18: separate interface, host-initiated
```

#### I2C

```cpp
struct I2cDevice { I2cBus& bus; uint16_t addr; bool ten_bit{false}; uint32_t hz{400'000};
                   uint16_t stretch_timeout_us{0}; bool pec{false}; RegFormat regs{}; };
// Op flags for I2C: Restart (default between ops), NoStart (gather), Stop, RecvLen, IgnoreNack
co_await dev.transact(ops, deadline);              // types 1–5, 11
co_await dev.write_then_read(reg_bytes, rx);       // 3
co_await dev.write_gather(reg_bytes, data);        // 4 (NoStart, zero copy)
co_await Regmap{dev}.read(reg, rx);  auto v = co_await Regmap{dev}.read<uint16_t>(reg);   // 6, 7
co_await dev.probe();  co_await dev.ack_poll(deadline);                                   // 9
co_await smbus::block_read(dev, cmd, rx);  co_await smbus::read_word(dev, cmd);           // 10
co_await dev.bus.recover();  auto lk = co_await dev.bus.lock();                           // 14, 16
I2cMux mux{bus, mux_dev, channel};   // 17: an I2cBus decorator
class II2cTarget;                    // 19
// Result carries: status {Ok, NackAddress, NackData, ArbLost, Timeout, BusError, Unsupported, Cancelled},
//                 failed_op, bytes_done, t_start_us, t_end_us
```

#### UART

```cpp
struct UartConfig { uint32_t baud; uint8_t data_bits{8}; Parity parity{None}; StopBits stop{One};
                    Flow flow{None}; bool tx_invert{false}, rx_invert{false};
                    Duplex duplex{Full}; int16_t de_pin{-1}; uint8_t rx_idle_chars{0}; };
IUart& gps = get_uart(UartRole::Gps);          // several instances
co_await gps.configure(cfg);                   // Unsupported if HW can't (e.g. invert)
// TX
co_await gps.write(buf, TxDone::Sent);         // or Queued
co_await gps.send_break(us);  co_await gps.drain();
// RX: an always-on ring; one reader
auto r = co_await gps.read(buf, Until{.min=1, .delimiter='\n', .idle_chars=3, .deadline=t});
//   r.bytes, r.reason {Delimiter, Idle, Full, Timeout, Break}, r.errors, r.first_byte_us
co_await gps.read_exact(buf, n, deadline);
size_t n = gps.try_read(buf);
co_await gps.transact(tx, rx, Until{.idle_chars=2});    // half duplex (SmartPort, ESC telemetry)
// Zero-copy / per-byte option (Betaflight rxCallback style), driver level:
gps.rx_enable(UartRxHandler{.on_data=..., .on_buf_request=..., .on_error=..., .deliver=Deliver::Isr}, first_buf, idle);
```

#### Callback transactions (no coroutine needed)

```cpp
// 1. Periodic compass read with callbacks only (Deliver::Loop default)
struct Compass {
    std::array<uint8_t,1> reg{0x00};  std::array<uint8_t,6> raw{};
    Op ops[2] = { {OpKind::Write, reg}, {OpKind::Read, {}, raw, 0, I2C_RESTART} };
    Transaction<I2cDevice> txn{};
    void start(I2cBus& bus) {
        txn.dev = &kQmc;  txn.ops = ops;  txn.deadline_us = 2'000;
        txn.on_done = decltype(txn.on_done)::create<Compass, &Compass::on_read>(*this);
        bus.submit(txn);
    }
    void on_read(TransactionBase& t) {                       // runs in step(), not ISR
        if (t.result.status == Status::Ok) g_mag.publish(parse(raw));   // Latest<MagSample>
        else ++errors;
        g_timer.submit(tick_txn);                            // 50 Hz: tick_txn's callback resubmits txn
    }
};

// 2. IMU on DRDY, ISR delivery straight into the RT ring (replaces AutoChannelConfig)
imu_txn.deliver = Deliver::Isr;
imu_txn.on_done = Callback::create<&push_imu_sample>();     // only g_imu_ring.push_from_isr()
drdy.arm_multishot(imu_txn);                                // edge ISR -> submit -> DMA -> callback

// 3. Chain: flash write-enable -> program -> poll WIP (one callback for the chain)
wren.chain_next = &program;  program.chain_next = &poll_wip;
poll_wip.on_done = Callback::create<Flash, &Flash::programmed>(*this);
spi.submit(wren);

// 4. TLP bridge: completion becomes a TLP response (no wfi on the loop)
bridge_txn.on_done = Callback::create<Bridge, &Bridge::reply>(*this);   // pushes Tlp64 CplD to egress

// 5. The coroutine adapter is just another callback
template <class Bus, class Txn> struct RunAwaiter {
    Bus& bus; Txn& txn; std::coroutine_handle<> h{};
    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> hh) noexcept {
        h = hh; txn.deliver = Deliver::Loop;
        txn.on_done = TransactionBase::Callback::create<RunAwaiter, &RunAwaiter::done>(*this);
        return bus.submit(txn) == Status::Ok;              // false -> resume now; result says why
    }
    void done(TransactionBase&) noexcept { h.resume(); }   // runs inside step()
    const Result& await_resume() const noexcept { return txn.result; }
    ~RunAwaiter() { if (h && !txn.is_done()) bus.cancel(txn); }
};
```

#### Per-op callback (Betaflight `busSegment_t` callback)

An `Op` may carry `on_op_done` returning `Continue`, `Repeat` (run this op again, e.g. polling a status register) or `Abort`. It runs in ISR context with the same restrictions as `Deliver::Isr`. It's optional, and it lets the gyro path build a read-and-check sequence with no loop involvement.

#### Edge-case rules (spec text)

| Case | Rule |
|---|---|
| Resubmit inside the callback | Allowed. State is `Idle` before `on_done` runs. |
| Submit from an ISR callback | Allowed: `submit()` is ISR-safe |
| `cancel()` while Queued | Removed from the queue, completed with `Cancelled`, callback runs once |
| `cancel()` while Active | Abort if the hardware can, otherwise complete normally. Callback runs exactly once either way. |
| Deadline | The timer driver calls `cancel()` with `Timeout`; same rules |
| Callback bound to a temporary lambda | Forbidden (`etl::delegate` doesn't own it). Bind to a member or static function and name the owner in a comment (§10.3). |
| Linux worker threads | `complete()` pushes to the completed list under the hosted lock; the callback still runs in `step()` |
| Cross-core (Core 0 ISR → Core 1 loop) | Atomic MPSC push to the completed list + cross-core doorbell |
| Two DMA users on one bus | Impossible: the bus driver is the only DMA owner, and triggers are transactions |
| `op.len` larger than a span | Rejected by the base with `BadRequest` before the target sees it |
| Unsupported op (quad on PL022, invert on a 16550 without support) | `Unsupported`, never silently done differently |

#### Migrating the existing API

| Today | Becomes |
|---|---|
| `spi.transfer_async(tx, rx)` | `co_await dev.transact({Op::transfer(tx, rx)})`; a thin compatibility wrapper is kept during P3 |
| `read_reg_async(reg, 0x80)` → `uint8_t` | `co_await Regmap{dev}.read<uint8_t>(reg)` → `Result<uint8_t>` |
| `i2c.transfer_async(addr, tx, rx)` | `co_await dev.write_then_read(tx, rx)` |
| `I2cRequest::use_register/register_offset` | deleted (use `Op` lists) |
| `on_tx_complete`/`on_rx_complete`/`callback`/output queue/`result_slot` | `on_done` + inline `result` |
| `uart.write_async` / sync `read_byte` | `write(buf, TxDone)` / `read(buf, Until)` |
| `AutoChannelConfig` | `Trigger` + `Transaction` |
| `get_spi_driver()` | `get_spi(n)` / `SpiDevice` table |

### D4. Runtime: scheduling, timers, channels

1. **Wake primitives must enqueue:**
   - `yield()` posts its own handle.
   - `sleep`/`delay`/`wait_until` register with one runtime **timer heap**: a min-deadline in a hardware compare register, any number of sleepers, intrusive nodes inside the awaiter.
   - Delete the single-slot E906 timer and the untick'd `TimerService`.
2. **Priority scheduling:** three ready queues, `RT`, `Normal` and `Background`.
   - `step()` drains `RT` completely, then resumes one `Normal`, re-checks `RT`, and so on.
   - `Background` runs only when the others are empty, or within a per-step budget.
   - The task priority is set at `spawn(task, Priority::RT)` and carried in the promise.
   - The completion callback inherits the priority of the transaction's owner.
3. **No inline resume anywhere:** `spawn`, `run`, `when_all`/`when_any`, `CoroutineIoEngine` and the combinators post to the dispatcher. `FinalAwaiter` uses symmetric transfer (`return continuation`), never `.resume()`.
4. **Cancellation:** a `stop_token`-like flag in the promise. Every awaiter unregisters itself in its destructor (from a waiter list, timer heap or driver transaction). A parent that destroys a child cancels the child's waits first.
5. **Channels:**
   - `AsyncQueue<T,N>` keeps SPSC semantics, but uses proper atomics (`std::atomic` with acquire/release) so it's ISR-safe. The waiter re-checks the state after waking.
   - New `Latest<T>`: a seqlock with a version number and `co_await changed()`. Use it for "latest mag/GPS fix".
   - New `Broadcast<T,N>` for many readers.
   - `pop`/`acquire` take a deadline.
6. **Primitives:**
   - Intrusive **waiter lists** (any number of waiters) for `Event`, `Semaphore` and an async `Mutex`.
   - `select(q1, q2, timer)` returns the index of the one that fired.
7. **Pool:** exhaustion is a counted error (`coro_pool_alloc_failures()`), and `Task` creation reports it. Never return a null task that looks ready.
8. **Cross-core:**
   - One dispatcher per core, with an atomic MPSC submit and a doorbell (RP2350 SIO FIFO/SEV, P4 cross-core interrupt).
   - `InterruptLock` becomes per-platform: a hardware spinlock on RP2350, `amoswap` on RISC-V; on host, a per-object mutex instead of one global spinlock.
9. **Idle:** every target uses mask → check → `wfi`/`wfe` → unmask, the pattern the E906 already uses.

**Runtime API sketch** (replaces the broken primitives listed in system review §5.1):

```cpp
// Tasks with priority, rate and budget (Betaflight task_t ideas, coroutine body)
enum class Priority : uint8_t { RT, High, Normal, Background };
struct TaskSpec { const char* name; Priority prio; uint32_t period_us{0}; uint32_t budget_us{0}; };
spawn(fusion_loop(), {"fusion", Priority::RT});            // woken by the IMU ring, not a period
spawn(gps_task(),    {"gps",    Priority::Normal});
spawn(telemetry(),   {"telem",  Priority::Background, 20'000, 300});
// stats per task: runs, avg/max exec us, overruns, last wake latency -> runtime_stats / Studio

// Time
co_await sleep_for(5ms);  co_await sleep_until(t_abs);
Ticker tick{1ms};  for (;;) { co_await tick.next(); /* drift-free, reports missed ticks */ }
auto r = co_await with_timeout(q.pop(), 2ms);            // optional<T> / Result

// Channels
AsyncQueue<ImuSample, 8>   imu_q;     // SPSC, atomic indices, ISR-safe push, overflow policy:
                                      //   Block | DropNewest | OverwriteOldest (+ drop counter)
Latest<MagSample>          mag;       // seqlock + version: mag.publish(x); auto [x, ver] = mag.read();
                                      //   co_await mag.changed(last_ver);
Broadcast<FixSample, 4>    gps_fix;   // many readers, each with its own cursor
Oneshot<Result>            reply;     // one value, one waiter
// Sync
Event ev; Semaphore sem{0}; Mutex m;  // intrusive waiter lists, any number of waiters
auto idx = co_await select(imu_q.pop(), cmd_q.pop(), tick.next());   // index of the winner
// Structured concurrency
auto [a, b] = co_await when_all(read_mag(), read_baro());   // cancels siblings on parent cancel
StopToken st = current_stop_token();                       // every awaiter honours cancellation
```

**Primary-paced pattern, corrected:**
```cpp
Task<void> fusion_loop() {
    for (;;) {
        ImuSample s = co_await imu_q.pop();            // produced by DRDY trigger (Deliver::Isr)
        float dt = (s.t_us - last_t_us) * 1e-6f;       // sample timestamp, not pop time
        last_t_us = s.t_us;
        if (auto m = mag.read_if_newer(mag_ver))  filter.fuse_mag(*m);    // Latest<T>, no backlog
        if (auto g = gps_cursor.try_next())        filter.fuse_gps(*g);  // Broadcast<T>
        filter.update_imu(s, dt);
        // no `% N` prescalers: lower-rate work is its own task with its own period
    }
}
```

### D5. Processor framework (TLP, io_processor, IPC)

1. **One shared `TlpDispatcher`** in `include/`, tested on the host, replaces the four io_processor parsers. Every length, count, pin, enum and address is validated against compile-time limits **before** use. A rejected TLP gets an error completion (`status=BadRequest`). An unknown channel or type gets `UNSUPPORTED`. Every request is answered.
2. **One owner per DMA engine.** Auto-channels are clients of the bus driver (a D1 transaction with an edge trigger, D6), not a second DMA path.
3. **One ingress ring per producer** (worker or ISR, and the drain loop), or completions bounce through the loop. Never two producers on an SPSC ring.
4. **Wire format:**
   - Header fields are **little-endian** and the CRC is defined explicitly; the spec states both.
   - The RTL (`asp_wishbone_master.sv` address decode), C++, Python and cocotb change to match.
   - Add a version (steal a `flags` nibble, or a HELLO TLP that both sides check).
   - `make_stream_tx` uses `DmaStream`.
5. **One register and address map** generated from a single source (YAML → C++ header, Python, SV package). Delete `pcie_reg_api.h`/`pcie_bar_map.hpp` duplicates and the second SPI command set.
6. **RPMsg:**
   - Bounded waits.
   - When Linux isn't draining: drop and count, never spin.
   - Resync after a ring error.
   - Validate `translate_da` against the remoteproc carveouts.
   - Read `hdr->len` once.
   - The `host_addr_` lease is set by HELLO and cleared on close.
7. **Topology:** either implement `platform_launch_processing_domain` (Core 0 I/O and Core 1 loop) on Pico and P4, or delete the topology table and the spec text that claims it. No half-state.

### D6. Motor and actuator safety (FPGA + firmware)

1. **State machine in `asp_dshot_core`:**
   - States: `DISARMED → ARMING → ARMED → FAILSAFE`.
   - Arming needs: a magic arm key written to an `ARM` register, every throttle zero for at least N frames, and a live heartbeat.
   - DShot special commands (1–47) are accepted only in `DISARMED`, through a `CMD` register.
2. **Heartbeat watchdog in the fabric:**
   - The host writes `HEARTBEAT` at 100 Hz or faster.
   - If no write arrives for `T_fs` (default 100 ms, a register with a floor), outputs go to zero throttle for a configurable time and then `DISARMED`.
   - Firmware may not be able to disable it.
3. **Clamp in RTL:** throttle is 0, or within 48..2047. A value out of range goes to 0.
4. **Pins:** `PULL_MODE=DOWN` on motor outputs in every `.cst`/`.xdc`, and a documented external pull-down.
5. **Wishbone error and timeout:**
   - An unmapped address returns an error completion.
   - A host that only writes must not block motor processing.
6. **Crash paths:** on the E906 crash handler, remoteproc stop, Linux death or Zynq PS reset, the heartbeat stops and the fabric fails safe. Test each path.
7. **RC input:** treat PWM-decoder loss of signal (stuck high **or** low, plus no edge for 50 ms) as failsafe. Move edge detection to `sync[1]`.

### D7. DMA vs interrupt vs polling: policy and per-path guidance

**General rule** (goes into [SPEC-E906-HAL-01] and a new shared requirement):

| Use | When |
|---|---|
| **Poll** (busy-wait) | Only before the scheduler starts, or when the whole operation takes ≤ ~2 µs. Never inside `step()`. |
| **FIFO + one completion interrupt** | The transfer fits in the peripheral FIFO, **or** the protocol needs a decision per byte (I2C ACK/NACK, SMBus block length, UART delimiter), **or** the refill interval `t_refill = (FIFO_bytes × bits_per_byte) / bitrate` is at least 50 µs. |
| **DMA** | The transfer is larger than the FIFO and contiguous, **or** it's a continuous stream whose refill interval would be under 50 µs, **or** the CPU must not be interrupted during the transfer (the 8 kHz loop), **or** a memory copy is at least `kDmaCopyMinBytes`. |
| **Cyclic DMA ring + idle interrupt** | Continuous RX of unknown length at high rate (UART ≥ 921600, radio links). |
| **Peripheral engine** (TWI driver engine, ESP32 command list, PIO) | When it can run a whole transaction on its own and give one completion. Prefer this over DMA for I2C. |

Thresholds are **measured** by `abstractx.dma_bench()` on each board (DMA setup + completion interrupt vs CPU copy, and ISR entry/exit cost) and recorded in the spec with the command output. Until a threshold is measured, mark it `not measured` and use FIFO + interrupt.

**Per-path guidance.** FIFO depths and features are **from memory: verify (TRM)** before relying on them (task P5-10).

| Target / bus | Typical traffic | Use | Notes |
|---|---|---|---|
| **E906 SPI0** (64 B FIFO, ≤ 12 MHz from 24 MHz) | IMU 15 B at 8 kHz | FIFO + transfer-complete interrupt | 15 B ≈ 10 µs at 12 MHz. DMA setup (2 channels, about 20 register writes) is unlikely to pay off; measure. |
| E906 SPI0 | FPGA link, up to 7×64 B = 448 B | DMA (TX+RX channels, RX-done = complete), burst matched to FIFO trigger | Needs `da_to_pa`, RX DRQ enable, CLIC line 88 + `DMA_MCU_IRQ_CTRL`. |
| **E906 TWI1** (compass, 6 B at 50–100 Hz) | short register reads | TWI driver engine (`TWI_DRV_*`) + one completion/error interrupt; byte-FSM fallback | No DMA. Clear `INT_FLG` by writing 1. NACK and arbitration loss reported. |
| **E906 UART3** (GPS, 9600) | NMEA, ~1 char/ms | FIFO + RX threshold + RX-timeout interrupt into the ring | One interrupt per ~32 chars or per sentence. |
| E906 UART TX | CLI / log | TX ring + THRE interrupt; DMA only for blocks > 64 B at ≥ 921600 | Handle busy-detect (IIR `0x07`). |
| E906 memcpy | — | CPU below `kDmaCopyMinBytes`, DMA above | Measure. |
| E906 RPMsg | TLP/trace | No DMA: zero-copy shared SRAM buffers | Already correct in design. |
| **RP2350 SPI** (PL022, 8-entry FIFO) | IMU 15 B | **DMA** (the FIFO is smaller than the transfer) | One DMA pair per bus; completion on RX. |
| RP2350 SPI | short ≤ 8 B commands | FIFO + interrupt | |
| **RP2350 I2C** (DW, 16-entry FIFO) | compass 6 B | FIFO + interrupt (`TX_EMPTY` only while feeding; mask when done) | DMA possible via DREQ, not needed. |
| **RP2350 UART** (PL011, 32 B FIFO) | GPS 9600 / CRSF 420k | FIFO + RX-timeout interrupt; DMA ring if ≥ 1 Mbaud | 32 B at 420k ≈ 760 µs refill, so an interrupt is fine. |
| RP2350 exotic (quad, 3-wire, inverted, DShot) | — | PIO + DMA | |
| **ESP32-P4 GPSPI** (~64 B buffer) | IMU 15 B | CPU buffer mode + completion interrupt (IDF `post_cb`), queued transactions | GDMA for > 64 B. DMA buffers must be cache-line aligned, in DMA-capable memory, with `esp_cache_msync`. |
| ESP32-P4 I2C | sensors | IDF `i2c_master` async + `on_trans_done` (hardware command list) | No DMA. |
| ESP32-P4 UART | GPS / RC | IDF driver ring + event queue (`UART_DATA`, `PATTERN_DET`), or UHCI DMA at high baud | Use pattern detect for the delimiter. |
| **Zynq PS SPI/I2C/UART (Linux)** | — | Not a DMA decision: the kernel driver chooses. Use worker threads or `io_uring`/epoll; never block the loop. | Completion must go through D1 `complete()` (Zynq workers today skip it). |
| **Zynq PL IMU** | DRDY-triggered 15 B | Fabric auto-DMA engine (`asp_imu_auto_dma`) → TLP | Fix trigger-drop and CS hold first. |
| Linux host / SITL | — | Worker threads | TSan must cover them. |

### D8. Flight-controller service layer (Betaflight / INAV parity)

The gap analysis is in system review §15. The layer sits **on top of** the D1–D4 HAL and runtime. Copy API **ideas** only: Betaflight and INAV are GPLv3-only, and AbstractX must stay dual-licensable.

| Service | API (proposal) | Built on |
|---|---|---|
| **Bus device registry** | A static table of `BusDeviceDesc{name, bus_type, bus_index, cs/addr, mode, hz, irq_pin, hw_type}` generated from the board config. `find_device(hw_type)`, `probe_devices()` (WHO_AM_I across candidates). | D2 devices, D8 resources |
| **Sensor classes** | `IGyroAcc` (`init`, `read_async`, `drdy_pin`, `temperature`), `IMag`, `IBaro` (conversion state machine as a coroutine), `IRangefinder`. Board `SensorAlignment` (rotation + flip) applied in one place. Detection picks the first part whose probe succeeds. | D2 `Regmap`, D1 triggers |
| **Gyro path** | DRDY edge → ISR submit of the gyro transaction → `Deliver::Isr` push to `imu_q` → RT `fusion_loop`. Optional per-op callback checks the status register (Betaflight-style segment callback). | D1, D4 |
| **Serial ports** | `SerialPortConfig{id, functions_mask, baud_per_function}`; `open_serial(function) -> IUart&`. Functions: `Msp`, `Gps`, `RxSerial`, `TelemetryCrsf/SmartPort/Mavlink/Ltm`, `Blackbox`, `EscSensor`, `Vtx`, `Cli`. Options through `UartConfig` (invert, parity, stop, half duplex). Soft-serial (PIO on Pico, FPGA on Zynq) and USB VCP implement `IUart`. Passthrough mode for ESC/VTX configuration. | D3 |
| **RC receiver** | `IRcReceiver{ co_await next_frame(); channels(span<uint16_t>); link_quality(); rssi(); failsafe_flag(); }`. Providers: SBUS, CRSF/ELRS (with telemetry back), IBUS, FPort, GHST, SRXL2, PWM/PPM (FPGA decoder). `RcMap` and per-channel ranges. Frame timeout feeds failsafe. | D3 UART stream (`Until::Idle`), FPGA PWM decoder |
| **Motor device** | `IMotorDevice{ enable(); disable(); write(span<const float> 0..1); write_raw(); request_telemetry(); telemetry() -> eRPM/temp/V/I; protocol(); }`. Protocols: DShot150/300/600 (FPGA core, Pico PIO), PWM/OneShot (FPGA PWM). `DshotCommandQueue` (beep, direction, 3D, save) **accepted only while disarmed**, with repeat counts. | D6 fabric core, P0-02 register map |
| **Arming** | `ArmingDisableFlags` bitset (no gyro, RX failsafe, throttle not low, angle, calibrating, CLI/MSP active, boot grace, load, crash, RPM telemetry missing, **fabric heartbeat missing**). `try_arm()` succeeds only when the flags are clear. Arm and disarm are written to the fabric ARM register (D6). | D6 |
| **Failsafe** | Stages: Idle → RxLossDetected (`failsafe_delay`) → Procedure (Drop / Land at `failsafe_throttle` / RTH) → Landed → Recovery. **The fabric heartbeat failsafe (D6) is the last line**, independent of firmware. | RC receiver, D6 |
| **Scheduler view** | Every service is a coroutine task with `TaskSpec{prio, period, budget}` (D4). The `tasks` CLI and Studio show rate, average/max time and overruns, like Betaflight `tasks`. | D4 |
| **Resources** | `ResourceTable`: pins, DMA channels, timers, bus devices, each with an owner tag. `claim(res, owner) -> Status` fails on conflict. The io_processor's host GPIO commands may touch only pins marked `HostControllable`. | D5 |
| **Parameters** | Parameter groups: a static descriptor table `{id, version, size, reset_template}`. Storage has a version + CRC. Backends: E906 → Linux-side file over RPMsg; MCU → flash sector; host → file. CLI `get/set/dump/diff/save/defaults`. Names generated from one YAML (like INAV `settings.yaml`). | D5 register-map generator |
| **GCS gateways** | MSP v2 (Betaflight/INAV Configurator subset: status, attitude, raw IMU, RC, motor read, set motor only when disarmed) and/or MAVLink (heartbeat, attitude, GPS, RC, params). Gateways translate to internal TLP/CTF and never bypass arming. | D3 serial ports |
| **High-rate log** | A blackbox-style log built from CTF: gyro/PID/motor at loop rate with a rate divider and a measured budget, to SPI flash, an SD card, or the Linux side. | P5-06 |

### D9. Betaflight / INAV API compatibility (goal)

**Goal:** the AbstractX API must be **able** to host Betaflight and INAV flight code through a shim, unchanged or with a small documented patch set. **No shim code is written now.** The requirement is that every platform function they need has an AbstractX primitive with matching semantics (P7). The architecture and rules below are design constraints for the API, and the plan for when the shim is built later.

The surface, the mismatches and the licensing are in system review §15.4.

**Decisions** (made by the project owner on 2026-10-22; copy them into `[SPEC-COMPAT-*]` in P7-00):
1. **Firmware: both Betaflight and INAV, through a shared shim.** One shim core (time, IO/EXTI, bus, serial, DMA/resources, motors, storage, system) with thin Betaflight and INAV front ends. Their APIs differ mostly in naming: Betaflight `extDevice_t`/`spiSequence`, INAV `busDevice_t`/`busTransfer`.
2. **First target: Pico 2 W.** The compat domain is Core 1. Native AbstractX I/O, PIO DShot and soft-serial run on Core 0. It has enough SRAM and flash. Check whether upstream Betaflight's RP2350 platform work can be followed or reused at the interface level (not copied).
3. **Licensing: accepted.** Builds that link Betaflight/INAV code are GPLv3-only. The clean-room shim and native AbstractX stay dual-licensed. `ABSTRACTX_COMPAT=betaflight|inav` stamps the output.

**Architecture:**
<!-- ascii-ok: layer sketch in a dated review record, kept as written on 2026-10-22 -->
```
  Betaflight / INAV src/main  (pinned upstream version, GPLv3, unmodified or small patch set)
        │ calls their platform API: micros, IO/EXTI, spiSequence, busRead, serialPort_t, motorDevice_t, config_streamer …
  compat/<fw>/   clean-room shim (AbstractX licence)          ← this layer
        │ maps to
  AbstractX HAL v2: D1 transactions, D2 devices/Regmap, D3 UART stream, GPIO triggers, D8 motor/resources/storage
        │
  Target drivers (E906 / Pico / Zynq / host)   +   FPGA fabric (DShot with D6 hardware failsafe)
```

**Rules:**
1. The **native AbstractX API stays primary**. The shim is a client of it and never reaches around it to hardware registers.
2. **Execution domain:**
   - Foreign code runs in one RT task in a **compat domain**: its own core where available (Pico Core 1; the E906 as a whole), otherwise a budgeted RT task.
   - Their busy-waits (`delay`, `spiWait`) are allowed only inside that domain. This is a documented exception to AGENTS §3–4, and it is recorded in AGENTS.md (§10.7).
3. **ISR callbacks** (`busSegment_t` callbacks, serial `rxCallback`, EXTI handlers) map to `Deliver::Isr` callbacks and per-op callbacks (D1 rule 9). Their return values map as `BUS_READY`→Continue, `BUS_BUSY`→Repeat, `BUS_ABORT`→Abort.
4. **Safety:** the D6 fabric failsafe and arming key stay in force under foreign code. The shim's `motorDevice_t.enable()` must go through the fabric ARM sequence, and the fabric heartbeat is fed only while their scheduler is running.
5. **Resources:** their `IOInit`/`dmaAllocate` claims go through `ResourceTable`, so a conflict with native AbstractX users fails loudly.
6. **No upstream code is copied into `include/`, `targets/` or `compat/`.** Upstream lives in a submodule or a fetched pinned tarball. The build marks any binary that links it as GPLv3-only.
7. **Version pinning:** the CI compiles the pinned upstream against the shim. The shim declares which upstream version it supports.

---

## 2. TODO by phase

Severity tags refer to the system review.

### Phase P0: Safety and stop-the-bleeding (do first, small and independent)

#### P0-01 Motor arming, heartbeat failsafe and throttle clamp in fabric
- **Why:** §2 (Critical). The motors keep running on link loss.
- **Spec first:** new `[SPEC-MOTOR-SAFE-01..05]` (arming FSM, heartbeat timeout, clamp, command gate, pin pulls) in the motor or FPGA spec, with a state diagram. Text from D6.
- **Files:** `rtl/motor/asp_dshot_core.sv`, `sim/cocotb/test_asp_dshot_core_cocotb.py` (new or extended), `sim/CMakeLists.txt`.
- **Accept:** cocotb shows:
  - no output before arming;
  - arming rejected with throttle > 0;
  - heartbeat stops → zero throttle within `T_fs` ± 1 frame, then DISARMED;
  - throttle 30 and 3000 → 0;
  - DShot command 7 is ignored while ARMED;
  - randomized Wishbone timing.

  The test fails on the current RTL (record that).
- **Checks:** `cocotb-regression`, `rtl-lint`, `rtl-synth-check`, `fpga-fit`.

#### P0-02 One motor register map, generated
- **Why:** §2 (Critical). Three maps disagree.
- **Spec first:** a register-map requirement naming the single source (e.g. `hw/common/regmap.yaml`).
- **Files:** new YAML plus generator (`tools/gen_regmap.py`), outputs replacing `include/pcie_bar_map.hpp` `reg::esc`, the `tools/test_asp_pcie.py` constants, and the RTL localparams (SV package).
- **Accept:** one generator run produces all three. A test asserts that the C++, Python and SV offsets are equal. `test_asp_pcie.py motor N` hits channel N−1 in cocotb.
- **Checks:** `host-tests`, `cocotb-regression`.
- **Depends on:** P0-01 adds the ARM, HEARTBEAT and CMD registers. Do this together with it or immediately after.

#### P0-03 Motor pin pulls and PWM-decoder loss of signal
- **Why:** §2.
- **Files:** every board `.cst`/`.xdc` (motor pins `PULL_MODE=DOWN`), `rtl/pwm/asp_pwm_decoder.sv` (detect stuck-high or stuck-low, plus a 50 ms no-edge timeout; edge detect on `sync[1]`; channels 5–6 readable), new cocotb test.
- **Accept:** cocotb covers stuck-high, stuck-low and no-edge, with a flag set for each. The constraint diff is reviewed.
- **Checks:** `cocotb-regression`, `rtl-lint`, `fpga-build`.

#### P0-04 Clamp every host-controlled length, index and pin in the io_processors (stopgap before P4-01)
- **Why:** §7 (Critical `header_skip`, High `rx_len`, GPIO `1U<<pin`).
- **Spec first:** `[SPEC-TLP-*]` input-validation requirement: "every host-supplied length is clamped to the destination buffer; pins are checked against a per-target allowlist; rejected requests get an error completion".
- **Files:** `targets/{allwinner_e906,pico2w_rp2350,linux}/src/io_processor.cpp`.
- **Accept:** a host test (Linux io_processor, plus E906/Pico parsers compiled for host where possible) feeds `tx_len=200`, `rx_len=255` and `pin=255`. Each gets an error completion and no out-of-bounds access under ASan. The test fails on the old code.
- **Checks:** `host-tests`, `sanitizer-gate`.

#### P0-05 Stop the E906 `gps_imu` main corrupting the resource table
- **Why:** §11 (High).
- **Files:** `apps/gps_imu_app/platforms/allwinner_e906/main.cpp`. Delete the raw writes to `0x40000004`/`0x4E010004`, and report uptime through the status or trace path.
- **Accept:** `nm`/objdump shows no store to those addresses. The `e906-build-layout` check passes.
- **Checks:** `e906-build-layout`, `memory-footprint`.

#### P0-06 Bound every RPMsg wait
- **Why:** §7 (High). The E906 hangs when Linux isn't listening.
- **Files:** `targets/allwinner_e906/src/rpmsg.cpp` (`notify_host`, `reply`, ring-error resync).
- **Accept:** a host simulation (`sim/test_e906_rpmsg_flow_control.cpp`) with a full mailbox and no buffers returned: `tx_commit` returns `Busy` and the count rises, and after the ring recovers, RX resumes. It fails on the old code.
- **Checks:** `host-tests`, `e906-board-selftest` (stop the host reader while streaming).

#### P0-07 Make the traceability numbers honest
- **Why:** §13.
- **Spec first:** [SPEC-AUDIT-*] change: `audit_specs.py` scans all `*SPECIFICATION.md` files and prints the matrix "Complete" figure, not tag coverage.
- **Files:**
  - `tools/audit_specs.py`;
  - `README.md`: remove or cite every number in §13's table;
  - `AGENTS.md`: fix "sub-16 KB";
  - downgrade the 21 `@status Complete` tags to match the matrix;
  - `e906_repl` SPEC 256 KB → 128 KB;
  - ESP32-P4 claims;
  - `engineering_log.md:167`.
- **Accept:** `audit_specs.py` output shows 190 requirements and the real complete percentage. A grep for `µs|ns|KB|100%|verified` in README/specs matches only claims that have a cited command.
- **Checks:** `spec-change-review`; `python3 tools/spectrace_matrix.py --matrix`.

### Phase P1: Core foundations (runtime and driver base). Everything after depends on this.

#### P1-01 `AsyncDriverBase` v2: lifecycle, in-flight ownership, single completion path (D1 items 1–4, 6, 8)
- **Why:** §3, §4 (High on 3 targets).
- **Spec first:** new `[SPEC-HAL-TXN-01..08]`, and rewrite [SPEC-HAL-01].
- **Files:** `include/abstractx/hal/async_driver.hpp` (or a new `transaction.hpp`), `include/abstractx/domain_dispatcher.hpp`.
- **Do:**
  - State machine; `active_`; `complete()`; `cancel()`; deadline hook.
  - Intrusive queue and intrusive completed list.
  - Remove the output queue, `on_tx_complete`/`on_rx_complete`, and `result_slot`; the result lives inline.
  - Keep a temporary compatibility shim so the targets still compile, and remove it in P3.
- **Accept:** new host tests:
  - 10 000 submits across 3 queued transactions: each callback runs exactly once and in order, and `active_` pairs with the right transaction;
  - resubmit inside the callback ×10 000 with no growth;
  - cancel racing completion under TSan (worker thread as the ISR): exactly one completion;
  - chain with a failing middle link: one callback, the rest `Cancelled`;
  - destroyed awaiter: cancel runs, ASan clean;
  - queue depth, `dropped_resumes` and the pool high-water mark stay flat (AGENTS §10.5).
- **Checks:** `host-tests`, `sanitizer-gate`, `thread-sanitizer`.

#### P1-02 Dispatcher: `{fn, ctx}` entries, completed list drained first, priorities (D1 dispatcher change, D4 item 2)
- **Files:** `domain_dispatcher.hpp`, `src/runtime.cpp`, the promise type in `asp_coro.hpp`.
- **Accept:**
  - A host test: an RT task woken while 20 Normal tasks are ready runs before any of them. Background runs only when idle or within its budget.
  - A full ready queue can't lose an I/O completion (it goes through the list).
  - `dropped_resumes` stays 0 in the steady-state test.
- **Checks:** `host-tests`, `sanitizer-gate`, `thread-sanitizer`.
- **Depends on:** P1-01.

#### P1-03 Fix the wake primitives and add one timer heap (D4 item 1)
- **Why:** §5 (High: yield, sleep, delay, wait_until never wake; single-slot timers).
- **Files:** `include/asp_coro.hpp`, `include/abstractx/timer.hpp`, `include/abstractx/hal/timer.hpp`, `targets/*/src/timer.cpp` (compare-register hook only).
- **Accept:**
  - Host tests driven only by `step()` (no manual `.resume()`): `yield` round-robins; 50 concurrent sleepers wake in deadline order within one tick; sleeping ×1000 with no leak.
  - The E906 board test runs two concurrent sleepers.
  - Each test fails on the old code.
- **Checks:** `host-tests`, `sanitizer-gate`, `e906-board-selftest`.

#### P1-04 Combinators and inline resumes (D4 item 3)
- **Why:** §5 (High: use-after-free).
- **Files:** `asp_coro.hpp` (`when_all`, `when_any`, `start_one`, `CoroutineIoEngine`), `src/runtime.cpp` (`spawn`, `run`).
- **Accept:**
  - Register `sim/test_coro_combinators.cpp` in ctest, and rewrite it to call `when_all`/`when_any` via `step()`.
  - A `when_any` loser that is waiting on a timer and a queue is cancelled cleanly (ASan).
  - A grep shows no `.resume()` outside `Dispatcher::process` (keep a reviewed allowlist).
- **Checks:** `host-tests`, `sanitizer-gate`.
- **Depends on:** P1-02, P1-05.

#### P1-05 Cancellation and multi-waiter primitives (D4 items 4–7)
- **Files:** `asp_coro.hpp` (`Event`, `Semaphore`, `AsyncQueue`, new `Mutex`, `Latest<T>`, `select`), pool allocation path.
- **Accept:**
  - Two waiters on Event, Semaphore and queue both wake correctly.
  - A waiter re-checks after waking (no `T{}` return).
  - `AsyncQueue` push from a TSan worker thread is clean.
  - Pool exhaustion increments `coro_pool_alloc_failures()` and the task reports failure.
  - A parent destroyed with a suspended child is ASan clean.
- **Checks:** `host-tests`, `sanitizer-gate`, `thread-sanitizer`.

#### P1-06 Idle and cross-core correctness (D4 items 8–9)
- **Files:** `include/abstractx/interrupt_lock.hpp`, `targets/pico2w_rp2350/src/hal_platform.cpp` (masked check, then `wfe`), per-core dispatcher if P3-03 adopts Core 1.
- **Accept:** a Pico board test shows a cross-core post wakes within 10 µs over 10 000 iterations. A host TSan test covers the MPSC submit.
- **Checks:** `thread-sanitizer`, `board-bringup`.

### Phase P2: HAL API v2 (devices, ops, UART stream). Depends on P1-01.

#### P2-01 SPI and I2C devices, op lists, `Result<T>`, `Regmap` (D2)
- **Spec first:**
  - rewrite [SPEC-HAL-02] (SPI): list the transfer types and the per-target mapping;
  - new I2C requirement (none exists today);
  - new `[SPEC-HAL-REGMAP]`.
- **Files:** `include/abstractx/hal/{spi,i2c}.hpp`, new `regmap.hpp`.
- **Accept:** host tests with a fake bus covering every op kind:
  - command-then-read without padding;
  - 16-bit register;
  - repeated START;
  - gather write;
  - probe;
  - RecvLen;
  - out-of-bounds `len` rejected before reaching the target.
- **Checks:** `host-tests`, `sanitizer-gate`.

#### P2-02 Migrate the drivers to `Regmap` and `Result`
- **Files:** `include/abstractx/drivers/imu/icm42688p.hpp`, `qmc5883l.hpp`, `ublox_gps.hpp`.
- **Do:**
  - `init_async` checks WHO_AM_I and every write.
  - The IMU reads with `write_then_read` (no 15 B padding).
  - Fix the QMC rate comment and the reset delay.
  - Fix the UBX zero-length frame handling and `timestamp_us`.
  - Fix the GPS `pending_handle_` race.
- **Accept:** host tests: a missing device makes `init_async` fail; a wrong WHO_AM_I fails; a NACK propagates.
- **Checks:** `host-tests`, `sanitizer-gate`.

#### P2-03 UART v2: RX stream, config, TX-sent, multi-instance (D3)
- **Spec first:** rewrite [SPEC-HAL-03].
- **Files:** `include/abstractx/hal/uart.hpp`, `platform.hpp` (indexed accessors), `ublox_gps.hpp` (use `read_until`).
- **Accept:** host tests with a fake UART:
  - delimiter, idle, exact-N and timeout reads;
  - overrun reported;
  - bytes arriving between reads are not lost;
  - `TxDone::Sent` waits for TEMT;
  - unsupported config returns `Unsupported`.
- **Checks:** `host-tests`, `sanitizer-gate`.

#### P2-04 GPIO edge await and triggers; DRDY pacing
- **Why:** §3, §11 (not DRDY-paced).
- **Files:** `include/abstractx/hal/gpio.hpp` (`co_await gpio.wait_edge(pin, edge)`, `Trigger::arm_multishot(txn)`), `icm42688p.hpp` (`next_sample_async` waits for DRDY, or consumes the trigger ring).
- **Accept:** a SITL test with a fake DRDY at 8 kHz: the fusion loop runs at 8 kHz ± 1 %, not at bus speed. The test fails on the old code.
- **Checks:** `host-tests`.
- **Depends on:** P1-03.

#### P2-05 Remove sync methods from the interfaces
- **Why:** AGENTS §4; §3.
- **Files:**
  - `i2c.hpp`: drop the `*_sync` pure virtuals;
  - `uart.hpp`: drop sync `read`/`write`/`flush`;
  - `timer.hpp`: drop `delay_*`;
  - the `init()` callers in the drivers.
- **Do:** move bring-up needs to `IBringup`.
- **Accept:** a grep shows no sync bus call reachable from a coroutine. Update AGENTS §4 to match (§10.7).
- **Checks:** `host-tests`, `spec-change-review`.
- **Depends on:** P3 ports.

### Phase P3: Targets, one at a time (E906 first, then Linux/Zynq, then Pico; ESP32-P4 decision)

#### P3-01 E906: decide the driver set, then make it real
- **Decision first:**
  - Either promote `apps/e906_repl_app/src/e906_periph.cpp` (UART3 + TWI1, working) to the target HAL behind the v2 interfaces, or fix `targets/allwinner_e906/src/{spi,i2c,uart}.cpp`.
  - Delete the loser.
  - Record the decision in the E906 spec.
- **Do** (each with an `@impl` tag and a board test):
  1. Interrupt routing through the CLIC for every peripheral used (SPI0, TWI1, UART3, DMA line 88 + `DMA_MCU_IRQ_CTRL=0xFF00`). Remove the PLIC writes.
  2. Call `Pio::init` (or per-driver pinmux); CCU gates and resets; SPI module clock and divider with DRS (verify the CDR field width in the TRM).
  3. `da_to_pa()` for every DMA descriptor and buffer address.
  4. SPI: RX DRQ enable (verify the bit), completion on RX, busy guard, FIFO + interrupt path per D7 (no `XCH` spin), DMA for > 64 B.
  5. I2C: TWI1 via the TWI driver engine or byte-FSM, clear `INT_FLG` by writing 1, report NACK and arbitration loss, no DMA.
  6. UART: RX-timeout + ring per D3; handle busy-detect; `UART_HALT` sequence for baud changes.
  7. Delete `DMA_THRESHOLD=4`, the I2C 32 and the UART TX 32; set thresholds from `dma_bench` (P5-09) or use FIFO + interrupt.
  8. Single driver instance per peripheral (remove the duplicate `E906I2c` in io_processor).
- **Accept:**
  - `e906-board-selftest` extended:
    - SPI loopback or IMU read at 8 kHz for 60 s with no lost samples;
    - TWI1 compass at 100 Hz for 60 s;
    - UART3 GPS sentences with no overrun;
    - DMA 448 B transfer;
    - each test runs ≥ 1000 iterations with flat counters.
  - Quote `./tools/analyze_elf_size.sh` (SRAM_FAST used/192 KB, DRAM 0) per `.agents/rules/e906-memory-tracking.md`.
- **Checks:** `e906-build-layout`, `e906-board-selftest`, `memory-footprint`, `e906-disassembly-check` (trap and idle paths).
- **Depends on:** P1, P2-01, P2-03.

#### P3-02 Linux / Zynq: workers, register map, devices, no silent mocks
- **Do:**
  1. Workers call the base `complete()` (fixes `result_slot`).
  2. Size each buffer from its own span; reject a mismatch.
  3. `submit()` failure completes with an error.
  4. `speed_hz_` made atomic or passed per transaction.
  5. Partial UART writes reported.
  6. **Mocks only in an explicit `--sim` or `ABSTRACTX_SIM` build.** A device that fails to open fails init loudly.
  7. I2C device path from config (`/dev/i2c-0` per the DT overlay).
  8. Call `LinuxUartDriver::open()` from platform init.
  9. Zynq: `zynq_uio.hpp` and io_processor use the `asp_axi_lite_bridge` BRAM register map, write and read 16 DWORDs per TLP, and drain egress; or switch the top to `asp_axi_dma` (decide in the spec and delete the other).
  10. Remove `<iostream>`/`<deque>` from `targets/` (AGENTS §2), or move the Linux target into an explicitly hosted exception documented in AGENTS.
- **Accept:**
  - Host tests with a fake spidev/i2c-dev that fails `open` and `ioctl` see errors propagate.
  - A TSan clean run.
  - Zynq board: an IMU TLP round trip through UIO at 1 kHz for 60 s with no drops and no CPU spin (top < 5 %).
- **Checks:** `host-tests`, `sanitizer-gate`, `thread-sanitizer`, `board-bringup`.
- **Depends on:** P1-01, P0-02, P4-02 (byte order).

#### P3-03 Pico 2 W
- **Do:**
  1. Remove the second start in the DMA, I2C and timer ISRs (P1-01 makes this impossible).
  2. One `PicoSpi` instance per bus.
  3. Real async I2C: program the target address and `data_cmd`; mask `TX_EMPTY` when done; NACK and abort mapping; timeout.
  4. Clamp the bridge `rx_len` (P0-04).
  5. Wire `on_auto_spi_dma_done` through D1 triggers, or delete it.
  6. Remove `i2c_*_blocking` and the `wfe` spins from `step()`.
  7. `--specs=nano.specs` in the Pico link.
  8. `stdio` off the GPS UART (`pico_enable_stdio_uart(... 0)` or move the GPS).
  9. Stop using GPIO25 as an LED on Pico W (use `cyw43_arch_gpio_put`).
  10. SIO FIFO protocol or no FIFO use.
  11. Zero-copy pbuf or chained-pbuf-safe copy.
  12. `InterruptLock` hardware spinlock if Core 1 is used.
- **Accept:** builds with the SDK (quote the size output). Pico board test: IMU via DMA at 8 kHz and compass at 100 Hz for 60 s, with flat counters.
- **Checks:** `board-bringup`, `memory-footprint`.
- **Depends on:** P1, P2.

#### P3-04 ESP32-P4: build properly or mark unsupported
- **Decision:** either create a real IDF project (component CMake, `etl` provided, platform layer, SPI via `spi_device_queue_trans` + `post_cb`, I2C `i2c_master` async, UART event queue, DMA-capable aligned buffers + cache sync), or move ESP32-P4 to "planned" in every spec and README and delete the stub that reports success.
- **Accept:**
  - Unsupported: no Markdown claims P4 runs.
  - Supported: an IDF build log plus a board test.
- **Checks:** `spec-change-review`, `board-bringup`.

### Phase P4: Processor framework and FPGA fabric

#### P4-01 Shared `TlpDispatcher` with validation (D5 items 1–3)
- **Spec first:** `[SPEC-TLP-*]` covering request validation, error completions for every request, `UNSUPPORTED`, and one producer per ring.
- **Files:** new `include/abstractx/tlp_dispatcher.hpp`; the three io_processors reduce to target glue; fix DMA_CFG (status, channel lookup by ID, re-arm the trigger pin); E906 auto-channel pin match.
- **Accept:**
  - A host fuzz test (≥ 100 000 random TLPs under ASan/UBSan) with no out-of-bounds access, and every request answered.
  - Per-target glue compiled for host where possible.
- **Checks:** `host-tests`, `sanitizer-gate`, `thread-sanitizer`.
- **Depends on:** P1-01, P0-04.

#### P4-02 Wire format: byte order, CRC, version (D5 item 4)
- **Files:** `include/asp_tlp64.h/.hpp`, `asp_wishbone_master.sv` (address decode), `tools/abstractx_wire.py`, `tests/conftest.py`, the cocotb helpers, the Studio decoders.
- **Accept:** a golden-vector test: one byte array checked by C++, Python and cocotb, giving the same fields and CRC. `make_mem_read(0x40000100)` decodes as `0x40000100` in RTL sim.
- **Checks:** `host-tests`, `cocotb-regression`.

#### P4-03 RPMsg hardening (D5 item 6) and E906 ingress wiring
- **Files:** `rpmsg.cpp` (`translate_da` checked against the carveouts; `hdr->len` read once; TX `desc->len` upper bound), `rpmsg_endpoints.cpp` (`host_addr_` lease and close), `platform.cpp` (call `set_ingress_ring` so host TLPs reach the io_processor), TLP endpoint flood guard like the trace endpoint's.
- **Accept:**
  - Host tests:
    - out-of-carveout descriptor rejected;
    - endpoint closed → no console flood;
    - host→E906 TLP reaches the dispatcher.
  - Board test: send a TLP from Linux and get a completion.
- **Checks:** `host-tests`, `e906-board-selftest`.

#### P4-04 FPGA egress path correctness
- **Files:** `rtl/spi/asp_spi_frontend.sv` (assert ready only after a TLP is actually sent; sample and ready tied to the same TLP), `rtl/asp_router.sv` (hold the grant until the handshake; round-robin or weighted arbitration), `rtl/asp_wishbone_master.sv` (fix the `m_cpl_tvalid` ordering; don't overwrite `tdata` while valid && !ready; error CplD on timeout; don't block writes on an undrained CplD).
- **Accept:**
  - cocotb with **randomized tready** and a **handshake scoreboard** (every TLP in = exactly one TLP out, no duplicates) for front end, router and WB master. Each test fails on the old RTL.
  - `test_asp_top_cocotb` fixed and added to ctest.
- **Checks:** `cocotb-regression`, `rtl-lint`, `rtl-synth-check`.

#### P4-05 FPGA hygiene
- **Do:**
  - Fix `asp_axi_dma` `reg_irq_status` having two drivers, or delete the module if it stays unused.
  - IMU auto-DMA: queue or flag triggers that arrive during a burst; give CS hold ≥ t_CSH (verify in the ICM-42688 datasheet); widen `burst_len`/`target_bit_cnt`.
  - AXI-Lite bridge: CRC or resync, and return SLVERR when the FIFO is full.
  - `sys_timestamp` units consistent across boards.
  - Pipeline the Zynq 64-bit divide.
  - Check the CLG400 pins against the real package.
  - Add cocotb tests for `asp_pwm_decoder` and `asp_ila_trace`, or delete them.
  - Register all cocotb tests in ctest.
- **Checks:** `cocotb-regression`, `rtl-lint`, `rtl-synth-check`, `fpga-fit`.

#### P4-06 One address and register map (D5 item 5)
- **Do:** extend the P0-02 generator to every block (sys, LED, IMU, ESC, PWM, GPS, baro). Delete `pcie_reg_api.h` duplicates and the second SPI command set. Fix [SPEC-TLP-02] overlaps and the `TLP_BUS_SPECIFICATION.md` type table.
- **Checks:** `host-tests`, `cocotb-regression`, `spec-change-review`.

#### P4-07 Topology decision (D5 item 7)
- **Do:** implement `platform_launch_processing_domain` on Pico, with Core 0 doing I/O and Core 1 running the loop, or delete `PlatformTopologyTable` and the [SPEC-ARCH-05]/[SPEC-ARCH-06] auto-placement text. Delete `dram_spsc_protocol.h`. Pick one msgbox channel and stop `MsgBox::init` draining the HiFi4 mailbox.
- **Checks:** `spec-change-review`, `board-bringup`.

### Phase P5: Platform, trace, apps, Studio, build, measurements

#### P5-01 E906 stack guard, PMP and watchdog
- **Files:** `bsp/e906_ddr.ld` (a guard gap below the stack), `pmp.cpp` (locked NAPOT guard; call `Pmp::init`), `e906_repl_app` (`mp_stack_set_limit(stack - margin)`), stack high-water in the status report, and a host-heartbeat watchdog (no Linux heartbeat within N s → crash flag).
- **Accept:** board test: deep Python recursion raises RecursionError (no `.bss` corruption), and a deliberate stack overflow traps with a clear cause.
- **Checks:** `e906-build-layout`, `e906-disassembly-check`, `e906-board-selftest`.

#### P5-02 E906 crash reporting
- **Do:**
  - A compact crash record (≤ 256 B) at a fixed SRAM address and in trace0.
  - Notify Linux through a msgbox or crash flag.
  - An `mscratch` exception stack plus a re-entry flag.
- **Accept:** a forced fault shows cause and `mepc` in `/sys/kernel/debug/remoteproc/*/trace0`, and in the Studio event log.
- **Checks:** `e906-board-selftest`.

#### P5-03 E906 idle race at `wfi+4`
- **Do:** set a "woken" flag in non-sample ISRs, and make the sample-only return go back to the wfi only if the flag is clear.
- **Accept:** a disassembly check of the trap return. A board stress test (1 kHz profiler with a msgbox flood, 10 min) shows no wake latency over 1 ms.
- **Checks:** `e906-disassembly-check`, `e906-board-selftest`.

#### P5-04 MicroPython GC correctness and heap ownership
- **Do:**
  - Scan registers with `gchelper` (the RISC-V setjmp variant).
  - Call `mp_stack_set_top` from the real C stack.
  - Reserve the HiFi4 DSP RAM in the DT, enable its clock, and keep the HiFi4 remoteproc off.
  - Fix the `memory_map.h:29` comment.
  - Soft-reset then GC stress test.
- **Checks:** `e906-board-selftest`.

#### P5-05 Fusion and app correctness
- **Do:**
  - `dt` from `imu.timestamp_us`.
  - Consume `g_sensor_ring`, or remove it.
  - The producer must not spin when the queue is full (it suspends).
  - Clear `has_mag_` after a timeout.
  - Fuse GPS course into the quaternion.
  - PID: derivative on measurement plus anti-windup.
  - NaN guards before float→int casts.
  - Replace the `seq % 10` prescaler with a `Latest<T>` / timer.
  - `serial_loopback` E906 main as a coroutine app.
  - Remove the hard-coded "CPU < 1 %".
- **Accept:** SITL tests: constant-rate rotation integrates within 1 %, heading holds after a magnetometer dropout, a GPS course step converges, and the PID/mixer is exercised (SPEC-APP-05/06 then verified for real).
- **Checks:** `host-tests`, `sanitizer-gate`.

#### P5-06 Trace fixes
- **Do:**
  - Update `barectf_config.yaml`/`trace_schema.json` with streams 3/4/5 and the real 40 B header, plus a test that compares the YAML with the C++ structs.
  - Gate `trace_imu` (compile and runtime), with a measured cycle budget.
  - Per-packet `cycles_now`.
  - Zero-copy `platform_send_telemetry` on E906 and Pico.
  - Fix the `tracer.hpp` comment.
  - Show the console-flood regression test failing on the old firmware (AGENTS §0.5).
- **Checks:** `host-tests`, `e906-board-selftest`.

#### P5-07 Build and CI
- **Do:**
  - CI adds TSan, the E906 cross-build plus `analyze_elf_size.sh`, the Pico SDK build, and cocotb (`sim-test-all`).
  - pytest is mandatory (no silent skip).
  - `tools/test_*.py` collected.
  - `riscv-none-elf.cmake` Release gives a single `-O2`.
  - The esp32p4 configure fails fast outside IDF.
  - `track_memory_footprint.py` checks defined heap symbols (`malloc`, `free`, `_malloc_r`, `_Znwj`, `_ZdlPv*`), with no `except: pass`.
  - Fix the sized `operator delete` that pulls in `free`.
- **Accept:** a CI run link in the commit. The heap check fails on the current E906 ELF (engineering_log 2026-10-21) and passes after the fix.
- **Checks:** `quality-trends`, `memory-footprint`.

#### P5-08 Studio
- **Do:**
  - Real CRC check, or show `-`.
  - Fix the tag 2 shadowing and decode `GpsFixPayload` from firmware.
  - Remove the "Fast GPS"/"Fast Compass" buttons, or implement them.
  - No synthetic data outside `--sim`.
  - Timeline reset on a board reset.
  - Count malformed datagrams and show the count.
  - `bind` errors shown in the UI.
  - scp, ssh and subprocess calls off the GUI thread.
  - Badges driven by the real fields.
  - Compute or remove the loss %.
  - Live path uses `CtfSchema` (AGENTS §7), or AGENTS §7 and [SPEC-STUDIO-07] are reconciled (§10.7).
  - USER window floats by default and holds no SSH or transport controls (AGENTS §8.1).
  - `flight_display.py` doesn't duplicate surfaces or open a second UDP listener.
  - Split `TelemetryState` (ingest, state, CLI, sim).
  - Cap `coro_frames`.
  - `StrictHostKeyChecking=accept-new`.
- **Accept:**
  - A headless test feeds datagrams to `udp_receiver_thread` (tags 1–5, malformed, reset).
  - A test asserts that USER-window widgets contain no transport controls.
  - `py_test_visualizer_sdk` runs in CI.
- **Checks:** `host-tests` (pytest).

#### P5-09 Measurements (record numbers with the command output in the spec)
1. `abstractx.dma_bench()` on E906 (and Pico): DMA setup plus completion vs CPU copy, ISR entry/exit cost, giving `kDmaCopyMinBytes` and the per-bus thresholds for D7.
2. 8 kHz loop jitter and worst case on E906 and Pico (trace spans and a GPIO toggle on a scope), before and after P1-02 priorities.
3. Trace overhead in the 8 kHz path (cycles per `trace_imu`).
4. RAM cost of the v2 transaction structs on E906 (`analyze_map.py` before and after P1/P2). Budget: about 22 KB of `SRAM_FAST` headroom at 88.5 %.

**Checks:** `board-bringup`, `memory-footprint`, `quality-trends`.

#### P5-10 Hardware facts to verify (TRM / datasheet), recorded as `hardware-fact` entries
- **T527:**
  - SPI `FCR` RX DRQ bit;
  - `CDR1/CDR2/DRS` field widths;
  - SPI FIFO depth;
  - `TWI_DRV_*` register bits and FIFO depth;
  - `DRQ_TWI*`/`DRQ_SPI0`/`DRQ_UART*` numbers;
  - MCOR bit layout;
  - `McuDma` vs main DMAC clock gate;
  - UART FIFO depth;
  - RS-485 and inversion support;
  - what is at `0x10000000` in the E906 view.
- **RP2350:**
  - PL022 FIFO depth;
  - DW I2C FIFO depth;
  - PL011 FIFO depth;
  - stdio UART defaults;
  - CYW43 pin use of GPIO25;
  - SIO FIFO use by `multicore_launch_core1`.
- **ESP32-P4:**
  - ISA (F vs D);
  - GPSPI buffer size;
  - I2C and UART FIFO depths;
  - cache line size for DMA.
- **Zynq / QMTECH:**
  - board package (CLG400 vs CLG484);
  - PS SPI, I2C and UART FIFO depths;
  - kernel UIO config.
- **ICM-42688:** t_CSH. **ESCs:** behaviour with the DShot telemetry bit set to 0.

#### P5-11 Rules and docs consistency (AGENTS §10.7)
- **Do:** resolve each contradiction listed in system review §13, either by changing the code or by changing the rule, in the same change, with an `engineering_log.md` entry. Includes:
  - the hosted-target exception for `<thread>`/`<mutex>`;
  - the pool-backed `operator new` wording;
  - the toolchain rule vs Pico;
  - the atlas UART2 vs UART3 and trace0 location;
  - AGENTS §8.2 tab list;
  - the `[SPEC-STUDIO-02]` stale entries.
- **Checks:** `spec-change-review`.

#### P5-12 Licence inventory (one-off)
- **Do:** list the third-party code and licences (MicroPython, ETL, FreeRTOS, Pico SDK, ESP-IDF, vendor headers, CppUTest), and check that each fits GPLv3 and commercial dual licensing. Write it to `docs/THIRD_PARTY.md`. Record the rule for D8/P6: Betaflight and INAV are GPLv3-only, so their **APIs may inspire the design but no code may be copied**, or AbstractX loses its commercial licence option.

### Phase P6: Flight-controller service layer (Betaflight/INAV parity, D8). Starts after P3-01 makes one target solid.

#### P6-01 Bus device registry, sensor classes, detection and alignment
- **Spec first:** `[SPEC-FC-DEV-*]` (descriptor table, probe order, alignment).
- **Files:** new `include/abstractx/fc/devices.hpp`, `sensors/*.hpp`; refactor `icm42688p.hpp` and `qmc5883l.hpp` into `IGyroAcc`/`IMag` implementations; board config YAML.
- **Accept:**
  - Host tests with fake buses: detection picks the right part, wrong WHO_AM_I falls through, and alignment rotates correctly in all 8 orientations (+ flip).
  - E906 board test: the IMU and compass are detected.
- **Checks:** `host-tests`, `e906-board-selftest`.
- **Depends on:** P2-01, P2-02.

#### P6-02 Serial port functions, soft-serial, passthrough
- **Spec first:** `[SPEC-FC-SERIAL-*]`.
- **Files:** `include/abstractx/fc/serial_ports.hpp`, Pico PIO soft-serial, board config.
- **Accept:**
  - Host tests: function lookup; per-function baud; option mapping to `UartConfig`; `Unsupported` when an option isn't available.
  - Pico board test: soft-serial loopback at 115200.
- **Checks:** `host-tests`, `board-bringup`.
- **Depends on:** P2-03.

#### P6-03 RC receiver providers (SBUS, CRSF/ELRS first)
- **Spec first:** `[SPEC-FC-RX-*]` (frame formats, timeouts, failsafe flag).
- **Files:** `include/abstractx/fc/rx/*.hpp`.
- **Accept:**
  - Host tests with recorded frames: decode, CRC rejection, frame-loss timeout raises failsafe within the configured delay.
  - Board test with a real receiver (inverted SBUS on Pico via pad override; CRSF at 420k).
- **Checks:** `host-tests`, `board-bringup`.
- **Depends on:** P6-02.

#### P6-04 Motor device, DShot command queue, bidirectional telemetry
- **Spec first:** `[SPEC-FC-MOTOR-*]`, consistent with `[SPEC-MOTOR-SAFE-*]` (P0-01).
- **Files:**
  - `include/abstractx/fc/motor.hpp`;
  - FPGA backend using the generated register map;
  - Pico PIO DShot backend;
  - bidirectional DShot (eRPM) in RTL (new cocotb test).
- **Accept:**
  - Host tests: commands are rejected while armed, repeat counts are honoured, throttle is normalised to the DShot range.
  - cocotb: eRPM decode.
  - Bench test (**no props**, current-limited supply): spin-up and stop, and heartbeat loss → stop within `T_fs`.
- **Checks:** `host-tests`, `cocotb-regression`, `board-bringup`.
- **Depends on:** P0-01, P0-02.

#### P6-05 Arming and failsafe state machines
- **Spec first:** `[SPEC-FC-ARM-*]`, `[SPEC-FC-FS-*]` with state diagrams.
- **Files:** `include/abstractx/fc/arming.hpp`, `failsafe.hpp`.
- **Accept:**
  - Host tests: each disable flag blocks arming.
  - RX loss → stage sequence with the configured timing.
  - Recovery requires valid frames for N ms.
  - Arm and disarm propagate to the fabric ARM register.
  - Fabric heartbeat loss is reported as a disable flag.
- **Checks:** `host-tests`, `cocotb-regression` (fabric side).
- **Depends on:** P6-03, P6-04.

#### P6-06 Task table view and budgets
- **Do:** expose D4 `TaskSpec` stats through `runtime_stats`, a `tasks` CLI and a Studio panel, flagging overruns.
- **Accept:** a SITL test with an injected slow task shows the overrun counter rising and the RT task's wake latency staying within budget.
- **Checks:** `host-tests`.
- **Depends on:** P1-02.

#### P6-07 Resource ownership table
- **Do:** claim and release pins, DMA channels and timers with an owner. The io_processor GPIO commands are restricted to `HostControllable` pins. Board resources are generated from the P0-02 YAML.
- **Accept:** host tests: double claims fail; host GPIO on an SPI pin is rejected.
- **Checks:** `host-tests`, `sanitizer-gate`.
- **Depends on:** P0-04, P4-06.

#### P6-08 Parameter groups and CLI
- **Do:** parameter-group descriptors, versioned and CRC-checked storage (E906 via Linux file over RPMsg; Pico flash), and CLI `get/set/dump/diff/save`.
- **Accept:** host tests: version bump resets to defaults; CRC corruption is detected; `diff` shows only changed values.
- **Checks:** `host-tests`, `e906-board-selftest`.

#### P6-09 MSP v2 and/or MAVLink gateway
- **Do:** a subset that lets Betaflight/INAV Configurator or QGroundControl show attitude, IMU, RC and GPS, and read parameters. Motor commands only when disarmed and only through arming.
- **Accept:** a host test against recorded MSP/MAVLink frames, then a manual check with the real GCS tool, recorded in `engineering_log.md`.
- **Checks:** `host-tests`.

#### P6-10 High-rate flight log
- **Do:** gyro, PID and motor logging at loop rate with a divider, built on CTF with a measured budget (P5-09).
- **Accept:** 60 s at 8 kHz/2 with no dropped records and loop overrun < 1 %.
- **Checks:** `board-bringup`, `quality-trends`.

### Phase P7: Betaflight / INAV API coverage (D9). A design goal: no shim or port code is written in this phase.

The deliverable is proof that the AbstractX API **can** support everything the Betaflight and INAV platform layers need, so a shim could be written later without changing the AbstractX API.

#### P7-00 Record the goal
- **Do:** add `[SPEC-COMPAT-01]` to the HAL spec:
  - "The AbstractX HAL v2 and service layer (D1–D4, D8) shall provide a primitive for every Betaflight and INAV platform function in the coverage matrix, with matching semantics (blocking vs non-blocking, ISR vs loop context, ownership)."
  - The decisions: both firmwares, Pico 2 W as the first intended target, and GPLv3-only for any future build that links their code.
  - The rule that no upstream code is copied.
- **Accept:** spec merged with a `Verified by` line naming `spec-change-review`.
- **Checks:** `spec-change-review`.

#### P7-01 API coverage matrix
- **Do:**
  - Write `docs/compat/BETAFLIGHT_INAV_API_COVERAGE.md`.
  - For each Betaflight and INAV platform function family, give one row: their function or type → the AbstractX API that provides it → semantic notes → status (`covered`, `covered-by-design` [in D1–D8 but not yet implemented], `gap`).
  - Families: time, IO/EXTI, SPI incl. `spiSequence`/`busSegment_t` callbacks, I2C, bus-device registration, serial (`serialPort_t` vtable, `rxCallback`, port options, VCP), DMA and resource ownership, timers/motors (`motorDevice_t`, DShot commands, bidirectional telemetry), config storage, system, target description, scheduler hooks.
  - Take the symbol list from their public headers at a pinned release. Read their signatures only; don't copy their code.
- **Accept:** every row is `covered` or `covered-by-design`, or is a `gap` with a linked change to D1–D8. No gaps are left unassigned.
- **Checks:** `spec-change-review`.

#### P7-02 Close the gaps in the API design
- **Do:** for each `gap` from P7-01, extend the relevant design (D1–D4, D8) and its `[SPEC-*]` text, for example:
  - Repeat/Abort per-op callbacks;
  - a per-byte serial RX callback in ISR context;
  - a busy-wait-capable "compat domain" execution context;
  - a resource ownership query;
  - a motor command queue.
- **Accept:** the P7-01 matrix has no `gap` rows.
- **Checks:** `spec-change-review`.

#### P7-03 Keep the API compatible as it evolves
- **Do:** add a check to the review checklist and `spec-change-review`: any change to D1–D4/D8 APIs re-checks the coverage matrix.
- **Accept:** documented in `.agents/skills/spec-change-review/SKILL.md`.
- **Checks:** `spec-change-review`.

#### P7-04 Make `inav-abstractx` (`../inav`) a real AbstractX consumer
- **Why:** system review §16. inav uses none of the AbstractX API and has wire-incompatible forks.
- **Work in this repo (AbstractX):**
  1. Settle the TLP byte order and freeze the BAR map in the spec (P4-02, P4-06). Publish them as the single source.
  2. Adopt inav's concept-based regmap design (`include/abstractx/io/async_regmap.hpp`, `async_stream.hpp`, `async_frame.hpp`) into D2/D3. Keep the concept-based shape and back it with fixed HAL completion (P1-01, P2-01, P2-03). Review it first: inav's versions currently sit on blocking backends.
  3. Deliver P1 (working Task continuations, timer heap, timeouts, safe combinators), so inav can drop `coroutine_task.hpp`.
  4. Add an INAV-style task-table view on top of D4 priorities (P6-06), so inav's `scheduler.hpp` can map onto it.
- **Work in `../inav`** (separate repo; list here, track there):
  1. Remove the forked `asp_tlp64.hpp`, `spsc_tlp_ring.hpp` and `pcie_bar_map.hpp`, and the include-order override, once item 1 lands.
  2. Replace `coroutine_task.hpp` with AbstractX `coro::Task` / the runtime, and replace the blocking backends with AbstractX HAL v2.
  3. Make the parity tests real: compile the upstream C (or link reference objects) for differential tests. Make Stack B independent of Stack A. Use a test framework that isn't disabled by `NDEBUG`, and add sanitizer runs.
  4. Wire the mixer → motor device, RC and GPS into the loop.
- **Accept:**
  - inav builds against unmodified AbstractX headers with no local forks.
  - A golden-vector TLP test passes on both repos.
  - inav's grep for its own `coroutine_task.hpp` returns nothing.
  - Its tests run under ASan/UBSan.
- **Checks:** `host-tests`, `sanitizer-gate`, `spec-change-review`.
- **Depends on:** P1, P2-01, P2-03, P4-02, P4-06.

**Out of scope for now** (future work, after the goal is met): the shim itself (`compat/betaflight`, `compat/inav`), the `ABSTRACTX_COMPAT` build option, running upstream on Pico 2 W, the motor/arming integration under foreign code, CI against pinned upstream, and the optional Linux-side bridge.

---

## 3. Suggested order and parallelism

```
P0-01 ─┬─ P0-02 ─ P0-03          (FPGA safety; independent of C++)
P0-04, P0-05, P0-06, P0-07       (parallel, small)
P1-01 ─ P1-02 ─┬─ P1-04
P1-03 ─────────┤
P1-05 ─────────┘   P1-06 (with P3-03)
P2-01 ─ P2-02 ; P2-03 ; P2-04 (after P1-03) ; P2-05 (after P3)
P3-01 (E906) → P3-02 (Linux/Zynq) → P3-03 (Pico) ; P3-04 decision any time
P4-01 (after P1-01, P0-04) ; P4-02 before P3-02 ; P4-03 ; P4-04/05/06 (RTL track, parallel)
P5-* after the relevant P3 target is solid; P5-09/10 early (they change P3 parameters)
P6-01..10 (flight-controller services) after P3-01; P6-04/05 also need P0-01/02
P7-00..03 (API coverage, design only) can start any time; P7-02 feeds changes into D1–D4/D8 before P2 and P6 are implemented
```

Make one target solid before widening to the next:
- **E906 (P3-01)** is the reference for the native HAL. It already has the most working pieces.
- **Pico 2 W (P3-03)** is the intended first Betaflight/INAV target (D9 goal). It may run in parallel with P3-01 once P1 and P2 are done.
- Don't start P3-04 (ESP32-P4) until both are solid.

---

## 4. Definition of done (every task)

- [ ] Spec changed first, with `[SPEC-*]` and a `Verified by` line naming the skills that actually exercise it.
- [ ] Code tagged with `@impl`. `python3 tools/spectrace_matrix.py --changes` maps every hunk.
- [ ] A regression test that fails on the old code and passes on the new (output in the commit message).
- [ ] Host, ASan/UBSan and TSan suites pass (for C++ changes). cocotb for RTL. Board test for target behaviour.
- [ ] Steady-state test (≥ 1000 iterations, flat counters) for any queue, coroutine, allocator or driver change.
- [ ] E906 changes quote `./tools/analyze_elf_size.sh`.
- [ ] `engineering_log.md` entry with a category, including what check should have caught it.
- [ ] The requirement shows `VERIFIED` in `--matrix`.
- [ ] The report ends with what was **not** checked.

---

## Discovered (append here while working)

- …
