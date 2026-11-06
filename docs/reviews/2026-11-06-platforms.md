# Hosting AbstractX on other OSes, and how it compares (2026-11-06)

This answers the full audit's "platforms" checklist ([SPEC-AUDIT-17]). The host contract it measures against is [SPEC-ARCH-15]:

- AbstractX owns no loop.
- The host waits and calls `step()`.
- `step()` returns `kNow`, a deadline in µs, or `kNever`.
- Event sources store the result, post the handle, and call `wake()` (or `wake_from_isr()`).

Each OS below is marked by what exists:

- **Exists, tested**: runs on the PC and passes the six host conformance checks (`sim/host_conformance.hpp`).
- **Exists**: built for a target, not conformance-tested on the PC.
- **Design only**: no code, no SDK on this machine. A mapping, not a claim.

## 1. Per OS: loop, mapping, completions, ownership

| OS | Status | Native loop / async model | How `step()` + wake map onto it (one loop, no second one) | Where driver completions enter | Who owns timers, tick, IRQ priorities |
| --- | --- | --- | --- | --- | --- |
| **Bare metal** (E906, Pico) | Exists | none: `main()` | the loop calls `step()`, then `platform_idle_wait()`: `wfi` with interrupts masked around the re-check (E906 `platform.cpp:196`); the timer alarm is armed at the deadline | ISR / DMA completion → `push_completion_from_isr` → post → `wake_from_isr()` (the interrupt itself ends `wfi`) | AbstractX's timer driver owns `mtimecmp` / the SDK alarm; the target owns IRQ priorities (CLIC, NVIC) |
| **FreeRTOS** (E906 `e906_freertos_app`) | Exists | tasks, queues, task notifications, software timers | `hosts/freertos`: one task runs `step()`, waits with `ulTaskNotifyTake(timeout = deadline)`. Timer mode `WAIT` (default), `XTIMER` (one-shot software timer) or `TICK_HOOK` (`ABX_FREERTOS_TIMER_MODE`). Wake = `xTaskNotifyGive` / `vTaskNotifyGiveFromISR`. While a coroutine is ready it `taskYIELD`s (`freertos_host.hpp:19`). | ISR → post → `wake_from_isr()` (notification from ISR); other tasks → `wake()` | FreeRTOS owns `mtimecmp` and the tick ([SPEC-E906-RTOS-03]; `use_external_tick()`); AbstractX sleeps are served by `step()` deadlines, 1 tick resolution |
| **Linux, POSIX host** | Exists, tested | threads, epoll / `ppoll`, eventfd | `hosts/posix`: the calling thread loops `step()` + `ppoll(eventfd, timeout = deadline)`; wake writes the eventfd | worker threads (spidev / i2c-dev `ioctl`), the UART reader thread, the GPIO edge thread → complete the request → post → `wake()` | the kernel; AbstractX timer is `Polled` ([SPEC-HAL-05]): no thread |
| **Linux, Boost.Asio host** | Exists, tested (`7e68b94`) | `io_context`, completion handlers, `steady_timer` | `hosts/asio`: a pump handler calls `step()`; deadline → one `steady_timer`; wake → `asio::post(pump)` (one pending post); a work guard keeps `run()` alive. `attach()` shares an app's existing `io_context`. | same threads as POSIX today; Asio handlers would replace the UART / GPIO threads (async_read_some on the tty / gpiod fd) | the kernel; Asio owns the timer queue |
| **Zephyr** | Design only | threads, `k_poll`, `k_sem`, `k_timer`, `k_work` | one thread loops `step()` + `k_poll(events = {signal}, K_USEC(deadline - now))`; wake = `k_poll_signal_raise` (ISR-safe), so `wake()` and `wake_from_isr()` are the same call | Zephyr async driver callbacks (`spi_transceive_cb`, `i2c_transfer_cb`, `uart_callback_set`) run in ISR context → complete + post → `wake_from_isr()` | Zephyr owns the system timer and tickless idle; IRQ priorities through `IRQ_CONNECT`; zero-latency IRQs must not call `wake_from_isr()` (kernel calls not allowed there) |
| **ChibiOS/RT** | Design only | threads, event flags, virtual timers, OSAL | one thread loops `step()` + `chEvtWaitAnyTimeout(WAKE, TIME_US2I(deadline - now))`; wake = `chEvtSignal` / `chEvtSignalI` inside `chSysLockFromISR()` | ChibiOS HAL end-callbacks (`spiStartExchange` + `end_cb`, `i2cMasterTransmitTimeout` is blocking → needs the async I2C LLD or a helper thread) → complete + post → wake | ChibiOS owns the system tick / tickless (`CH_CFG_ST_TIMEDELTA`) and virtual timers; IRQ priorities in `mcuconf.h` (fast IRQs above kernel priority must not signal) |

### Coroutine runtimes that must never mix with AbstractX coroutines

These are forbidden by [SPEC-ARCH-15] and checked by `coro-contract-lint` check 7:

- **Linux:** Boost.Asio `awaitable`, `use_awaitable`, `co_spawn`, `experimental::coro`, and Boost.Cobalt. Asio handlers are fine; Asio coroutines are not.
- **Zephyr and ChibiOS:** no C++ coroutine runtime ships with either, so nothing to exclude. Code inside an AbstractX coroutine must never call a blocking kernel wait (`k_sem_take`, `chSemWait`). That is the same rule as AGENTS §3 (no blocking in `step()`).
- **FreeRTOS:** nothing to exclude. Same rule: no `vTaskDelay` or `xQueueReceive` with a timeout inside a coroutine.

## 2. Services: AbstractX against the RTOS / OS

| Service | AbstractX today (evidence) | vs FreeRTOS / Zephyr / ChibiOS / Linux | Verdict |
| --- | --- | --- | --- |
| Timeouts on every wait | sleeps; I2C deadline (E906 rule 8, Pico 2026-11-06); UART `read_async` deadline (E906, Linux); Linux workers fail on `ioctl` errors | RTOS: every blocking call takes a timeout | **Gap (partial)**: SPI transfers have no deadline on Pico / E906 (DMA assumed to finish); one path, the E906 SPI awaiter FIFO spin, was accepted as is |
| Multi-object waits | `when_all` / `when_any` combinators (`include/abstractx/coro.hpp`) | FreeRTOS queue sets, Zephyr `k_poll`, ChibiOS event masks | **Equivalent**, and typed (results come back as values) |
| Priorities / preemption | one Dispatcher FIFO, cooperative; hard real time goes to ISRs / DMA / the auto-DMA engine | RTOS: preemptive priority scheduling | **Gap by design**: priorities inside AbstractX are planned (P1-02, not done). Preemption is not needed for the coroutine layer: the host task can have any RTOS priority, and time-critical work never waits on a coroutine |
| Work queues | the executor / Dispatcher (`sim/test_executor.cpp`: a custom executor receives every post) | Zephyr `k_work`, FreeRTOS deferred tasks | **Equivalent** |
| Init order | `abstractx::init(config)` once; drivers `init_async()` in the app | Zephyr `SYS_INIT` levels, device dependency order | **Gap**: no init levels or device dependencies; the app orders it by hand |
| Power management | the host waits until the exact deadline (no polling interval, conformance check 1); bare metal `wfi` | FreeRTOS / Zephyr / ChibiOS tickless idle and device PM | **Equivalent** for idle (tickless by construction); **gap** for device power states (none) |
| Logging and tracing | binary CTF 1.8 (barectf), runtime status, timeline events, Studio | FreeRTOS trace hooks + Tracealyzer (paid), Zephyr tracing / CTF, ChibiOS trace buffer | **Better than stock FreeRTOS** (built in, coroutine-aware); **equivalent** to Zephyr's CTF tracing |
| Latency measurement | E906 bench: yield round trip 10.20 µs, interrupt → coroutine 14.2 µs avg (2026-10-31, `docs/articles/linux-vs-freertos-vs-abstractx`); DRDY → DMA ~6 µs | each OS needs its own tooling | **Better** on the E906 (measured, same bench for both runtimes); not measured on Pico, Linux, Zephyr or ChibiOS |

## 3. Port cost: what an application changes to move between these OSes

- **Application code changes nothing.** Coroutines, drivers through `get_*_port`, and `step()`. The same `main` runs on bare metal, the FreeRTOS host, and the POSIX or Asio hosts.
- **Per OS: one host file**, 108–142 lines: POSIX 108, FreeRTOS 132, Asio 142. It implements wait-until-deadline-or-wake plus the two wake hooks.
- **Per target: the drivers**, which are the real cost. They are written against the target's interrupt and DMA, or the OS driver API on Zephyr and ChibiOS.

## 4. What is open (from this review)

- **Zephyr and ChibiOS hosts:** no code and no SDK here. The mappings above are the plan. A host needs the six conformance checks run on that OS (on qemu or a board).
- **Priorities inside the Dispatcher** (P1-02), and **init levels**.
- **SPI deadlines** on Pico and E906.
- **Linux drivers on Asio handlers** instead of their own threads (optional; the threads pass TSan).

## Not checked

- No board.
- Zephyr and ChibiOS not built.
- FreeRTOS host not run on the PC: it has no POSIX port in this tree.
