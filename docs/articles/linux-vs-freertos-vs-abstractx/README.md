# Linux vs FreeRTOS vs AbstractX on the Allwinner T527

## Part 4 of *Bringing Up Heterogeneous RISC-V on Allwinner SoCs* (Radxa Cubie A5E)

Series index: [`docs/articles/README.md`](../README.md).
**Measured on hardware 2026-10-31; partly out of date** (what and why: §8).

---

## 1. The Question, and the Short Answer

**On the T527, where should time-critical I/O run, and what does it cost to get the result to a Linux thread?**

The Allwinner T527 on the Cubie A5E has two places to run code:
* eight Cortex-A55 cores running Linux;
* a 200 MHz XuanTie E906 RISC-V core beside them, with its own 192 KB of on-chip SRAM.

On the E906 we compare two runtimes running the same application code:
* **AbstractX alone**: an event loop of C++20 coroutines, no kernel;
* **AbstractX on FreeRTOS**: the same coroutines inside one FreeRTOS task.

The short answer, from the measurements below:

1. **Linux reacts fast but not predictably under load.** A PREEMPT_RT thread at the highest real-time priority wakes 18 µs late on average, 29 µs at most, at idle. Under `stress-ng` its worst case grows 4x, to 122 µs (§3).
2. **The E906 does not care what Linux is doing.** The same timer event on the E906 is 9.8 µs late with Linux idle or loaded, worst case 10.8 µs (§3). The E906 runs from its own SRAM with its own interrupt controller, so Linux load does not reach it.
3. **Getting the result back into Linux is not measured yet**, and it is the number that decides whether offloading pays off (§4). The only crossing measured so far is a round trip from a Python program; under load its worst case reached 9.2 ms.
4. **AbstractX alone vs on FreeRTOS: close.** AbstractX alone:
   * wakes a coroutine ~0.8 µs sooner;
   * uses 12-14 KB less RAM;
   * can sleep a coroutine for less than a millisecond.

   FreeRTOS adds preemption (§5).

---

## 2. Setup

| | Linux | E906, AbstractX alone | E906, AbstractX on FreeRTOS |
| :--- | :--- | :--- | :--- |
| Hardware | Cortex-A55, CPU 2 | XuanTie E906 RV32IMAFC, 200 MHz | same |
| Software | kernel 7.1.0 PREEMPT_RT, Buildroot (cubie-a5e `024f02e`) | `apps/e906_bench_app` | `apps/e906_freertos_app` (FreeRTOS kernel, 1 kHz tick) |
| Memory | DRAM, caches on | on-chip SRAM only, I-cache on, D-cache off ([SPEC-E906-CACHE-02]) | same |
| What sleeps the core | the kernel | `wfi` in `platform_idle_wait()` until an interrupt or the next deadline | the FreeRTOS idle task (`wfi`) |
| Measured with | `cyclictest` 2.80 | `mcycle` at 200 MHz | same |

* **Clock check.** `mcycle` was checked against the 24 MHz `mtime`: a 1 s `mcycle`-timed sleep counted 24,000,410 `mtime` ticks (2026-10-21).
* **Why the D-cache is off.** Everything the E906 touches lives in on-chip SRAM, which is already fast. That covers code, data, stacks, the IPC rings shared with Linux and the DMA buffers. With the D-cache on, every buffer shared with Linux or a DMA engine would need cache maintenance (clean before handing it over, invalidate before reading it). That costs time on every message and is easy to get wrong. A cache would help little and would add timing variation. It is only worth it if the firmware moves data to DRAM ([SPEC-E906-CACHE-02], [SPEC-E906-CACHE-03]).

**The same benchmark code runs in both E906 firmwares.** [`apps/e906_bench/bench.cpp`](../../../apps/e906_bench/bench.cpp) is compiled unchanged into both. They also share the drivers and the interrupt dispatch (`hal::irq::dispatch`, [SPEC-E906-RTOS-04]). Only what runs underneath differs. Requirements: [`apps/e906_bench/SPECIFICATION.md`](../../../apps/e906_bench/SPECIFICATION.md), [`apps/e906_freertos_app/SPECIFICATION.md`](../../../apps/e906_freertos_app/SPECIFICATION.md).

**Load.** "Loaded" means `stress-ng --cpu 8 --vm 2 --vm-bytes 256M --sock 2` on the A55 cores: every core busy, plus memory traffic and socket traffic.

**How the wake reaches the code** (details in §5):

![Execution Path Comparison: Hardware IRQ to Coroutine Resume](assets/wake-path-comparison.svg)

**Not recorded for the Linux runs:** the A55 CPU frequency governor, and whether CPU 2 was isolated (`isolcpus`, IRQ affinity). Both change Linux's numbers (§8).

---

## 3. The Same Event in Three Places: a 1 ms Timer

`cyclictest` is the standard Linux measure: a thread sleeps to an absolute deadline every 1 ms and records how late it woke. The E906 bench command `C` ([SPEC-E906-BENCH-06]) does the same with a coroutine: it moves an absolute deadline forward by 1 ms, sleeps until it, and records how late it was resumed. Each run is 60,000 deadlines (one minute).

| Timer deadline → code runs, how late (µs) | idle: avg / max | loaded: avg / max |
| :--- | ---: | ---: |
| Linux PREEMPT_RT thread, SCHED_FIFO 95, pinned to CPU 2 (`cyclictest -m -p 95 -i 1000 -l 60000 -t1 -a 2`) | 18 / 29 | 29 / **122** |
| Linux normal thread (SCHED_OTHER) | *not measured* | *not measured* |
| E906, AbstractX alone (coroutine) | 9.8 / 10.8 | 9.8 / **10.1** |
| E906, AbstractX on FreeRTOS (coroutine; old tick-hook model, firmware `d4c2b8f`) | 815 / 816 | 913 / 914 |
| E906, FreeRTOS native task (`vTaskDelayUntil`, `F`) | *not measured* | *not measured* |

* **AbstractX alone**: no deadline missed in 120,000; p99 10 µs and p99.9 11 µs in both runs. The ~9.6 µs floor (about 1,900 cycles) is the timer interrupt, the dispatch and the coroutine resume. How those cycles split is not measured yet (§8).
* **AbstractX on FreeRTOS**: the lateness here is the 1 ms tick resolution, not jitter.
  * The kernel owns the E906's only timer comparator (`mtimecmp`) for its 1 kHz tick ([SPEC-E906-RTOS-03]), so a coroutine sleep ends on the first tick after its deadline.
  * The deadlines form one 1 ms grid and the ticks another, on the same 24 MHz clock, so every wake is late by the same phase difference: 815 µs in one run, 913 µs in the other, varying by under 0.5 µs.
  * The evidence's p99 reads 500 because the lateness is past the 0-499 µs histogram.
* **Why the E906 does not move under load**: its code, data and stacks are in on-chip SRAM (0 bytes in DRAM), and its interrupts come through its own controller (CLIC). So A55 cache and DRAM traffic does not reach it. A load aimed at the shared bus is still to be tried (§8).
* **Linux**: the worst case depends on what else Linux is doing. One minute is short for a worst case; `cyclictest` results are usually quoted over hours.

Evidence: [`linux_cyclictest_20261031.json`](../../verification/evidence/linux_cyclictest_20261031.json), [`e906_cyclic_e906_bench_app_idle_20261031.json`](../../verification/evidence/e906_cyclic_e906_bench_app_idle_20261031.json), [`e906_cyclic_e906_bench_app_load_20261031.json`](../../verification/evidence/e906_cyclic_e906_bench_app_load_20261031.json), [`e906_cyclic_e906_freertos_app_idle_20261031.json`](../../verification/evidence/e906_cyclic_e906_freertos_app_idle_20261031.json), [`e906_cyclic_e906_freertos_app_load_20261031.json`](../../verification/evidence/e906_cyclic_e906_freertos_app_load_20261031.json).

---

## 4. Getting the Result Into Linux

Offloading only helps if the Linux side gets the result in time. **The one-way time from an E906 event to a Linux thread is not measured yet.**

The planned test (TODO §2b):
* The E906 stamps an event and sends it over RPMsg.
* A Linux reader thread stamps when the data is in the thread.
* The reader runs as the same thread types as §3: RT (SCHED_FIFO 95) and normal (SCHED_OTHER).
* It runs with Linux idle and loaded, for both E906 firmwares, with readers written in C and in Python.

What exists today is a round trip:
1. A normal-priority Python program on Linux sends a message to the E906 over VirtIO RPMsg (endpoint 1030), through `rpmsg_char`.
2. The E906 sends it back.
3. Python times the round trip.

Each size was sent 1,000 times.

| Echo round trip, µs: median / max | AbstractX alone | AbstractX on FreeRTOS |
| :--- | ---: | ---: |
| 16 B, Linux idle | 113.5 / 231 | 101.9 / 219 |
| 256 B, Linux idle | 238.8 / 302 | 241.6 / 297 |
| 496 B, Linux idle | 327.9 / 370 | 331.9 / 382 |
| 16 B, Linux loaded | 145.5 / **3,877** | 154.8 / **4,074** |
| 256 B, Linux loaded | 265.7 / **7,400** | 266.5 / **9,189** |
| 496 B, Linux loaded | 362.2 / **8,597** | 356.3 / **3,741** |

* **Most of a round trip is Linux** (syscalls, `rpmsg_char`, Python), not the E906.
  * The two E906 runtimes are within noise of each other.
  * The 16 B median moved by ~10 µs between runs on the same day, on both runtimes.
* **Under load the worst case reaches 4-9 ms** on both runtimes, while the E906 side of the trip did not change (§3). That delay is Linux scheduling a normal-priority Python process. Whether an RT thread in C removes it is what the planned test will show.
* **Throughput at 496 B**: 2.97 MB/s (AbstractX alone) vs 2.92 MB/s (on FreeRTOS). No message lost or corrupted on either.

Evidence: [`e906_bench_abstractx_stallfix_20261031.json`](../../verification/evidence/e906_bench_abstractx_stallfix_20261031.json), [`e906_bench_freertos_stallfix_20261031.json`](../../verification/evidence/e906_bench_freertos_stallfix_20261031.json), [`e906_bench_abstractx_linuxload_20261031.json`](../../verification/evidence/e906_bench_abstractx_linuxload_20261031.json), [`e906_bench_freertos_linuxload_20261031.json`](../../verification/evidence/e906_bench_freertos_linuxload_20261031.json).

---

## 5. On the E906: AbstractX Alone vs AbstractX on FreeRTOS

### How a wake reaches the code

**AbstractX alone**:
1. The interrupt handler clears the source.
2. It posts the waiting coroutine's handle into a lock-free single-producer ring.
3. It returns with `mret`. If the core was in `wfi`, it continues past it.
4. The event loop takes the handle and resumes the coroutine. No task stack is switched and no scheduler runs.

**AbstractX on FreeRTOS**:
1. The interrupt handler makes the same post.
2. It wakes the AbstractX task (`vTaskNotifyGiveFromISR`, `portYIELD_FROM_ISR`).
3. The scheduler saves the interrupted context and switches to that task.
4. The task runs `step()`, which resumes the coroutine.

The task's wait has changed since the measurements:
* **Today** the task runs the shared FreeRTOS host (`hosts/freertos`, [SPEC-ARCH-15]): it waits until the deadline `step()` returns, rounded up to whole ticks.
* **In the measured firmware** it waited with no timeout, and sleeps were woken from `vApplicationTickHook`.

### Measured (µs; 1 µs = 200 cycles)

| | AbstractX alone | AbstractX on FreeRTOS |
| :--- | ---: | ---: |
| Yield round trip, `co_await yield_to_dispatcher()`, 10,000 runs: avg / max | 10.20 / 14.4 | 10.22 / 12.7 |
| Soft interrupt raised → handler entered, avg | 1.18 | 1.27 |
| Soft interrupt raised → waiting coroutine resumed, Linux idle, 1,000 runs: avg / max | 14.2 / 18.1 | 15.0 / 17.3 |
| Same, Linux loaded: avg / max | 14.4 / 18.5 | 15.4 / 17.8 |

* **Yield costs the same on both**, because a yield never leaves the event loop.
  * 10.2 µs (2,040 cycles) is high for a coroutine switch.
  * The likely cause is that `step()` checks every subsystem on each pass, idle or not.
  * Making `step()` skip idle subsystems is open work (TODO §2b item 2). Per-phase cycle counts (§8) will show where the time goes.
* **Interrupt to coroutine**: AbstractX alone is ~150 cycles (0.8 µs) faster on average. FreeRTOS had the lower worst case in these runs.
* **This is a software interrupt** (`msip`), raised by the bench itself. It is a different event from §3's timer, so do not compare these rows with `cyclictest`.

### Memory

Both ELFs were built with:
* xPack GNU RISC-V GCC 15.2.0, Release (`-O2 -g -DNDEBUG`);
* freestanding C++20, `-fno-exceptions -fno-rtti`, `newlib-nano`.

Both pass `tools/check_e906_layout.py` and use 0.00 MB of DRAM (`tools/analyze_elf_size.sh`). Sizes are from `riscv-none-elf-size -A`:

| Built from | | AbstractX alone | AbstractX on FreeRTOS | FreeRTOS adds |
| :--- | :--- | ---: | ---: | ---: |
| `e159de6` (benchmarked, 2026-10-31) | code (`.text` + `.fastcode`) | 77,516 B | 82,432 B | 4,916 B |
| | static RAM (`.data` + `.bss`) | 25,984 B | 37,672 B | 11,688 B |
| `e560d50` (built 2026-11-06, not benchmarked) | code (`.text` + `.fastcode`) | 141,498 B | 148,070 B | 6,572 B |
| | static RAM (`.data` + `.bss`) | 23,992 B | 37,880 B | 13,888 B |
| | `SRAM_FAST` used of 192 KB | 170.3 KB (88.7 %) | 190.3 KB (**99.1 %**) | |

* **What FreeRTOS adds**:
  * the scheduler (`tasks.c`, `list.c`, `queue.c`), the RISC-V port and trap entry;
  * static task stacks (`configSUPPORT_STATIC_ALLOCATION = 1`, no heap): 8 KB AbstractX task, 2 KB native cyclic task (`F`), 2 KB interrupt stack, 1 KB idle;
  * 72 B per task control block.

  Both builds also have the same 8 KB startup stack.
* **Both images roughly doubled since `e159de6`.** The shared bench grew (IMU DRDY test, cyclic tests, the ICM-42688-P driver); neither runtime did. The FreeRTOS build has 1.7 KB of `SRAM_FAST` left.
* **Heap**: "zero heap" is a property of our code, not yet of the binary.
  * Neither build compiles a FreeRTOS `heap_*.c`, and no AbstractX code calls `malloc()`.
  * newlib's `_malloc_r`, `_free_r` and `_sbrk` are still linked into both (`riscv-none-elf-nm`, 2026-11-06; EL-1027-31).

### Idle

* **AbstractX alone** has no periodic tick: it sleeps in `wfi` until an interrupt or a deadline it set. This firmware does not count idle time (`"idle": "not-instrumented"`).
* **FreeRTOS**: the idle hook woke 6,366 times in 3.84 s. The core was outside the idle hook for 3.25 % of that window. That is the busy share of the whole benchmark run plus the 1 kHz tick, not the cost of the tick alone. An idle-only window is still to be measured.

---

## 6. When to Use Each

**Linux on the A55 cores**: anything that needs megabytes, such as vision, planners, storage, networking, UI and Python. It handles soft real time. PREEMPT_RT kept a 1 kHz RT thread under 30 µs at idle and under 125 µs under heavy load here. That suits 100 Hz - 1 kHz control that can take an occasional 0.1 ms delay. It is not for hard deadlines under load.

**The E906, for I/O and timing Linux should not own.** Its timing does not change with Linux load. Close fast loops on the E906 and send Linux only results. How late those results reach a Linux thread is what §4 still has to measure. Until then, do not put a hard deadline on the Linux side of the link.

**AbstractX alone on the E906** suits:
* work that moves data between buses and the host;
* event-driven sensor work;
* the lowest interrupt-to-code time;
* coroutine sleeps shorter than a millisecond;
* a core that no periodic tick wakes.

**AbstractX on FreeRTOS**: when you need preemption.
* **Long compute jobs** (FFT, planners) go in a lower-priority task, and the coroutine task preempts them.
* **Libraries that expect a blocking `read()` or `sleep()`** run in their own task, through the `TaskNotifier` wrappers ([SPEC-E906-RTOS-08]).
* **An existing FreeRTOS system** adopts coroutines in one task.
* **The cost on the E906**: ~0.8 µs per wake, 12-14 KB of RAM, and 1 ms coroutine sleep resolution.

The application code is the same in both E906 setups, so this choice can change later.

---

## 7. Reproduce

On a Radxa Cubie A5E:

```bash
# 1. Cross-compile both firmwares (FreeRTOS timer mode: -DABX_FREERTOS_TIMER=WAIT|XTIMER|TICK_HOOK, default WAIT)
cmake -S . -B build_e906 -DABSTRACTX_TARGET=e906 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-none-elf.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build_e906 --target e906_bench_app e906_freertos_app -j

# 2. Set the board clock (the 2026-10-31 evidence carries 1970 timestamps), copy the bench tools
ssh root@$ABSTRACTX_BOARD_IP "date -s '$(date -u +%Y-%m-%dT%H:%M:%S)' && mkdir -p /root/abstractx_board_bench"
scp -O apps/e906_repl_app/tests/board/bench_e906_runtime.py tools/e906_rpmsg_bridge.py tools/abstractx_wire.py \
    root@$ABSTRACTX_BOARD_IP:/root/abstractx_board_bench/

# 3. For each firmware: load it, then run the suite and the cyclic test from Linux
for app in e906_bench_app e906_freertos_app; do
  scp -O build_e906/apps/$app/$app root@$ABSTRACTX_BOARD_IP:/lib/firmware/$app.elf
  ssh root@$ABSTRACTX_BOARD_IP "R=/sys/class/remoteproc/remoteproc0; echo stop > \$R/state; \
      echo $app.elf > \$R/firmware; echo start > \$R/state; sleep 2; \
      python3 /root/abstractx_board_bench/bench_e906_runtime.py --json /tmp/$app.json; \
      python3 /root/abstractx_board_bench/bench_e906_runtime.py --cyclic 60000 --json /tmp/${app}_cyclic.json"
done
# Native FreeRTOS task baseline (e906_freertos_app only):
#   bench_e906_runtime.py --native-cyclic 60000 --json /tmp/freertos_native_cyclic.json

# 4. Linux on the A55 cores (cyclictest and stress-ng are in the Buildroot image since cubie-a5e 024f02e)
ssh root@$ABSTRACTX_BOARD_IP 'cyclictest -m -p 95 -i 1000 -l 60000 -t1 -a 2 -q'
ssh root@$ABSTRACTX_BOARD_IP 'stress-ng --cpu 8 --vm 2 --vm-bytes 256M --sock 2 --timeout 75s -q & sleep 5; cyclictest -m -p 95 -i 1000 -l 60000 -t1 -a 2 -q; wait'
# For the E906 rows under load, run step 3 while the same stress-ng command runs.
```

There is no one-command runner for this bench yet. `tools/run_e906_board_suite.py` runs the board self-tests, not these benchmarks.

---

## 8. Not Measured, and Out of Date

**Out of date.** The latency and echo numbers are from firmware built at `e159de6` (2026-10-31); the FreeRTOS cyclic rows are from `d4c2b8f` the same day. Since then:
* the FreeRTOS firmware runs AbstractX through the shared FreeRTOS host. Its timer mode is `WAIT` by default, with `XTIMER` and `TICK_HOOK` as build options. So the FreeRTOS cyclic row describes the old tick-hook model;
* the E906 interrupt, UART, SPI, I²C and RPMsg code changed.

Every number in §3-§5 needs a re-run at the current commit. Memory sizes were re-measured on 2026-11-06 (§5).

**Not measured** (each is a TODO §2b item):
* E906 event → Linux thread, one way, with an RT and a normal reader (§4);
* Linux normal-priority `cyclictest` (§3);
* AbstractX on FreeRTOS cyclic with today's host, in each timer mode, and the native FreeRTOS task (`F`);
* where the E906's cycles go: per-phase `mcycle` totals inside `step()`;
* repeated runs (several per row, hours for worst cases), and p99 / p99.9 in every table;
* the Linux setup, recorded with each run: CPU governor, CPU isolation and IRQ affinity;
* a load aimed at the shared bus (DMA-heavy, not only CPU and DRAM), to test the E906's isolation;
* idle accounting in the AbstractX firmware, and an idle-only window on both;
* IMU DRDY → data, FreeRTOS vs AbstractX alone.

**Related, not covered here**:
* FreeRTOS task switches in the CTF trace and the AbstractX Studio timeline ([SPEC-E906-RTOS-07]).
* AbstractX drivers used from FreeRTOS tasks, with no second set of drivers ([SPEC-E906-RTOS-08]).
* Bugs found while measuring, which caused ~1 s echo stalls before `e159de6` (`engineering_log.md`, 2026-10-31):
  * a lost RPMsg wakeup ([SPEC-E906-IRQ-04]);
  * the core going back to sleep with work waiting ([SPEC-E906-IRQ-06]).
