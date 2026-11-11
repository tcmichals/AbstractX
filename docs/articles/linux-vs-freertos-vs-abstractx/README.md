# Linux vs FreeRTOS vs AbstractX on the Allwinner T527

## Part 4 of *Bringing Up Heterogeneous RISC-V on Allwinner SoCs* (Radxa Cubie A5E)

Series index: [`docs/articles/README.md`](../README.md).
**Measured on hardware 2026-11-11, firmware at AbstractX [`2af893b`][c-2af893b].** Linux at idle and the E906 at idle
are measured twice. Under load, Linux and AbstractX alone are measured once. The FreeRTOS rows under load are not
measured: the board reset when they ran (§8).

---

## 1. The Question, and the Short Answer

**On the T527, where should time-critical I/O run, and what does it cost to get the result to a Linux thread?**

The Allwinner T527 on the Cubie A5E has two places to run code:
* eight Cortex-A55 cores running Linux (four online on this board: CPUs 4-7 are offline in its configuration);
* a 200 MHz XuanTie E906 RISC-V core beside them. It has 256 KB of on-chip SRAM, and the firmware runs in the first
  192 KB of it (`SRAM_FAST`, [SPEC-E906-MEM-01][spec-e906]).

On the E906 we compare two runtimes running the same application code:
* **AbstractX alone**: an event loop of C++20 coroutines, no kernel;
* **AbstractX on FreeRTOS**: the same coroutines inside one FreeRTOS task.

The short answer, from the measurements below:

1. **Linux reacts fast but not predictably under load.** A PREEMPT_RT thread at the highest real-time priority wakes
   18-28 µs late on average at idle, 43 µs at most. Under `stress-ng` its worst case was 123 µs. A normal thread
   (SCHED_OTHER) reached 18 ms under the same load (§3).
2. **CPU and DRAM load on Linux did not change the E906's timing.** The same 1 ms timer on the E906 is 12.3 µs late on
   average, 14.3 µs at most, with Linux idle or loaded (§3). A load aimed at the shared bus was not tried.
3. **An E906 event reaches a real-time Linux thread in C in about 0.12 ms.** At idle: 118-129 µs on average, 99.9 %
   within 155 µs, 250 µs at most. Under load the average held (95 µs), but one message in the minute took 2.7 ms. A
   normal-priority reader waited up to 13.7 ms under load (§4). The two clocks are not one counter, so every one-way
   number carries ±43-60 µs.
4. **AbstractX alone vs on FreeRTOS: close.** AbstractX alone:
   * resumes a coroutine after a soft interrupt 3.1 µs sooner (24.0 vs 27.1 µs; neither core is asleep in this test);
   * uses 13.9 KB less static RAM;
   * can sleep a coroutine for less than a millisecond. On FreeRTOS a coroutine sleep ends on a kernel tick, so it is
     late by up to 1 ms.

   FreeRTOS adds preemption (§5).

---

## 2. Setup

| | Linux | E906, AbstractX alone | E906, AbstractX on FreeRTOS |
| :--- | :--- | :--- | :--- |
| Hardware | Cortex-A55, CPU 2 (CPUs 0-3 online) | XuanTie E906 RV32IMAFC, 200 MHz | same |
| Software | kernel `7.1.0 #2 SMP PREEMPT_RT Sat Nov 7 2026`, Buildroot | `apps/e906_bench_app` | `apps/e906_freertos_app` (FreeRTOS kernel, 1 kHz tick) |
| Memory | DRAM, caches on | on-chip SRAM only, I-cache on, D-cache off ([SPEC-E906-CACHE-02][spec-e906]) | same |
| What sleeps the core | the kernel | `wfi` in `platform_idle_wait()` until an interrupt or the next deadline | the FreeRTOS idle task (`wfi`) |
| Measured with | `cyclictest` 2.80 | `mcycle` at 200 MHz | same |

* **Linux setup, recorded with each run**: CPUs 0-3 online, no `isolcpus`, default IRQ affinity `7f` (every online CPU,
  CPU 2 included), no cpufreq driver (so no governor; the cores run at the boot clock), kernel command line without
  real-time options. The board's fan was on (about 47 °C idle, at most 51.5 °C during the runs).
* **Clock check.** `mcycle` was checked against the 24 MHz `mtime`: a 1 s `mcycle`-timed sleep counted 24,000,410
  `mtime` ticks (2026-10-21).
* **FreeRTOS build for timing**: `-DABX_FREERTOS_DEBUG=OFF` (no asserts, no stack-overflow check, no per-switch hooks),
  because AbstractX alone has none of them ([SPEC-E906-BENCH-06][spec-bench]). The default debug build is measured too
  (§5).
* **Why the D-cache is off.** Everything the E906 touches lives in on-chip SRAM, which is already fast. That covers code,
  data, stacks, the IPC rings shared with Linux and the DMA buffers. With the D-cache on, every buffer shared with Linux
  or a DMA engine would need cache maintenance (clean before handing it over, invalidate before reading it). That costs
  time on every message and is easy to get wrong. It is only worth it if the firmware moves data to DRAM
  ([SPEC-E906-CACHE-02][spec-e906], [SPEC-E906-CACHE-03][spec-e906]).

**The same benchmark code runs in both E906 firmwares.** [`apps/e906_bench/bench.cpp`](../../../apps/e906_bench/bench.cpp)
is compiled unchanged into both. They also share the drivers and the interrupt dispatch (`hal::irq::dispatch`,
[SPEC-E906-RTOS-04][spec-rtos]). Only what runs underneath differs. Requirements:
[`apps/e906_bench/SPECIFICATION.md`](../../../apps/e906_bench/SPECIFICATION.md),
[`apps/e906_freertos_app/SPECIFICATION.md`](../../../apps/e906_freertos_app/SPECIFICATION.md).

**Load.** "Loaded" means `stress-ng --cpu 8 --vm 2 --vm-bytes 256M --sock 2` on the A55 cores: every online core busy,
plus memory traffic and socket traffic.

**How the wake reaches the code** (details in §5):

![Execution path comparison: hardware IRQ to coroutine resume](assets/wake-path-comparison.svg)

---

## 3. The Same Event in Three Places: a 1 ms Timer

`cyclictest` is the standard Linux measure: a thread sleeps to an absolute deadline every 1 ms and records how late it
woke. The E906 bench command `C` ([SPEC-E906-BENCH-06][spec-bench]) does the same with a coroutine: it moves an
absolute deadline forward by 1 ms, sleeps until it, and records how late it was resumed. Each run is 60,000 deadlines
(one minute). Where there are two runs, both are shown, separated by `;`.

| Timer deadline → code runs, how late (µs) | idle: avg / p99 / p99.9 / max | loaded: avg / p99 / p99.9 / max |
| :--- | ---: | ---: |
| Linux PREEMPT_RT thread, SCHED_FIFO 95, pinned to CPU 2 | 18 / 22 / 25 / 39; 28 / 32 / 35 / 43 | 25 / – / – / **123** (1) |
| Linux normal thread (SCHED_OTHER), pinned to CPU 2 | 37 / 85 / 90 / 1,794; 62 / 105 / 107 / 143 | 105 / 2,147 / 3,202 / **18,144** |
| E906, AbstractX alone (coroutine) | 12.3 / 13 / 13 / 14.3 (both runs) | 12.3 / 13 / 13 / **14.3** |
| E906, AbstractX on FreeRTOS, `WAIT` mode (coroutine) | 806; 449 (2) | *not measured* (3) |
| E906, AbstractX on FreeRTOS, `XTIMER` mode | 383; 61 (2) | *not measured* (3) |
| E906, AbstractX on FreeRTOS, `TICK_HOOK` mode | 856; 554 (2) | *not measured* (3) |
| E906, FreeRTOS native task (`vTaskDelayUntil`, `F`) | *not measured* (4) | *not measured* |

1. Run by hand with the E906 stopped, without the histogram, so no percentiles. The suite's own row reset the board
   (§8).
2. The lateness is the 1 ms tick resolution, not jitter:
   * the kernel owns the E906's only timer comparator (`mtimecmp`) for its 1 kHz tick
     ([SPEC-E906-RTOS-03][spec-rtos]), so a coroutine sleep ends on the first tick after its deadline;
   * the deadlines form one 1 ms grid and the ticks another, on the same 24 MHz clock, so every wake in a run is late
     by the same phase difference, which changes from run to run. Within a run, max minus min is at most 1.2 µs.
   * The evidence's p99 reads 500 when the lateness is past the 0-499 µs histogram.
3. Each FreeRTOS row under load reset the board or timed out (§8).
4. The first `F` run read 0 ns for all 60,000 samples: it took the next tick, not the one that woke the task, as its
   reference. Fixed after the run, not yet re-run ([SPEC-E906-BENCH-06][spec-bench]).

* **AbstractX alone**: no deadline missed in 180,000. The floor of about 12.1 µs (2,430 cycles) is the timer
  interrupt, the dispatch and the coroutine resume. It was 9.6 µs on 2026-10-31, in firmware built at
  [`e159de6`][c-e159de6]; the yield cost in §5 grew in the same period.
* **CPU and DRAM load did not change the E906's timing**: its code, data and stacks are in on-chip SRAM (0 bytes in
  DRAM), and its interrupts come through its own controller (CLIC). A load aimed at the shared bus (DMA, eMMC, network)
  was not tried.
* **Linux**: the worst case depends on what else Linux is doing. One minute is short for a worst case; `cyclictest`
  results are usually quoted over hours.

Evidence: run 1 [`part4_bench_2026-11-11T080247/`](../../verification/evidence/2026-11/part4_bench_2026-11-11T080247/),
run 2 [`part4_bench_2026-11-11T092420/`](../../verification/evidence/2026-11/part4_bench_2026-11-11T092420/) (rows
`*_cyclic_*` and `linux_cyclictest_*`; each directory has a `manifest.json` with the time, commit, ELF SHA-256, kernel
and Linux setup), and the hand-run RT row in
[`part4_load_manual_20261111.json`](../../verification/evidence/2026-11/part4_load_manual_20261111.json).

---

## 4. Getting the Result Into Linux

Offloading only helps if the Linux side gets the result in time. The one-way test ([SPEC-E906-BENCH-07][spec-bench]):
* The E906 sleeps to a 1 ms deadline, stamps the event and sends 32 bytes over RPMsg.
* A Linux reader thread stamps when `read()` returns, so the data is in the thread.
* The reader runs as the same thread types as §3: RT (SCHED_FIFO 95, `mlockall`) and normal (SCHED_OTHER), both on
  CPU 2, written in C and in Python.

**Two clocks.** A one-way time needs one clock on both sides. Each run bounds the offset between the E906's `mtime` and
the A55's `CNTVCT_EL0` with 2,000 probes before and 2,000 after. **The two are not the same counter**: on every run the
offset is about -377.6 million ticks (about 15.7 s), and the bounds before and after agree. So the runs report
`calibrated`, and every one-way number carries the offset's uncertainty: ±43-49 µs with the C reader, ±54-60 µs with
Python (half the shortest probe round trip).

| E906 event → data in the Linux thread, µs: avg / p99.9 / max (60,000 per run) | Linux idle (two runs) | Linux loaded (one run) |
| :--- | ---: | ---: |
| AbstractX alone, C reader, RT | 118 / 155 / 250; 129 / 138 / 150 | 95 / 141 / **2,713** |
| AbstractX alone, C reader, normal | 111 / 130 / 2,927; 109 / 117 / 165 | 353 / 5,690 / **13,676** |
| AbstractX alone, Python reader, RT | 129 / 165 / 1,310; 120 / 144 / 2,410 | *not measured* (1) |
| AbstractX alone, Python reader, normal | 125 / 153 / 2,541; 124 / 143 / 184 | *not measured* (1) |
| FreeRTOS `WAIT`, C reader, RT: E906 send → data in the thread (2) | 108 / 127 / 156; 101 / 124 / 134 | *not measured* (1) |

1. These rows reset the board or timed out (§8).
2. On FreeRTOS the event-to-thread time includes the tick phase of §3 (12-763 µs, depending on the run), so only the
   send-to-read part is compared. The same part on AbstractX alone, C, RT: 105 / 142 / 237; 116 / 125 / 136.

* **No message lost or reordered** in any run (60,000 of 60,000 received each time). On the E906 side the deadline is
  12.5 µs late (as §3), and sending takes about 2,550 cycles (12.8 µs) on both runtimes.
* **About 0.1 ms is the cost of crossing into Linux**: kick, mailbox interrupt, virtio and `rpmsg_char`, and the wake of
  the reader. At idle, C and Python, RT and normal, all average 109-129 µs: the language and the priority change the
  worst case, not the average.
* **Under load the RT reader kept its average but not its worst case**: one message of 60,000 took 2.7 ms, against
  123 µs for the RT `cyclictest` thread. Where that time goes inside Linux was not measured.

The round trip measured before (2026-10-31) was re-run too. A normal-priority Python program sends a message to the
E906 over VirtIO RPMsg (endpoint 1030) through `rpmsg_char`, the E906 sends it back, and Python times it. Each size was
sent 1,000 times per run.

| Echo round trip, µs: median / p99 / max (range over the runs) | AbstractX alone | AbstractX on FreeRTOS (`WAIT`) |
| :--- | ---: | ---: |
| 16 B, Linux idle | 165-166 / 210 / 231-258 | 166 / 211-214 / 245-265 |
| 256 B, Linux idle | 254-255 / 285-286 / 315-334 | 257-258 / 293-298 / 318-321 |
| 496 B, Linux idle | 347 / 378-385 / 400-406 | 348-349 / 378-382 / 403-409 |
| 16 B, Linux loaded | 186 / 4,735-5,397 / **9,422-18,261** | *not measured* |
| 256 B, Linux loaded | 266 / 5,102-5,458 / **12,455-14,372** | *not measured* |
| 496 B, Linux loaded | 367-370 / 5,591-5,628 / **10,452-10,897** | *not measured* |

* The two E906 runtimes are within 3 µs of each other at idle: most of a round trip is Linux (syscalls,
  `rpmsg_char`, Python).
* Throughput at 496 B, idle: 2.79 MB/s (AbstractX alone) vs 2.77 MB/s (on FreeRTOS); loaded, AbstractX alone:
  1.06-1.08 MB/s. No message corrupted.
* The 16 B median is 52-64 µs slower than on 2026-10-31 (113.5 / 101.9 µs then). The firmware, the kernel and the board
  configuration (four cores online) all changed since; which of them caused it was not measured.

Evidence: rows `*_oneway_*` and `*_bench_*` in the two run directories of §3.

---

## 5. On the E906: AbstractX Alone vs AbstractX on FreeRTOS

### How a wake reaches the code

**AbstractX alone**:
1. The interrupt handler clears the source.
2. It posts the waiting coroutine's handle into the ready queue: an `etl::queue_spsc_isr` with `InterruptLock` as its
   lock policy. The ISR side pushes without a lock; the event loop masks interrupts around its pop
   ([`include/abstractx/domain_dispatcher.hpp`](../../../include/abstractx/domain_dispatcher.hpp)).
3. It returns with `mret`. If the core was in `wfi`, it continues past it.
4. The event loop takes the handle and resumes the coroutine. No task stack is switched and no scheduler runs.

**AbstractX on FreeRTOS**:
1. The interrupt handler makes the same post.
2. It wakes the AbstractX task (`vTaskNotifyGiveFromISR`, `portYIELD_FROM_ISR`).
3. If the task was blocked, the scheduler switches to it and loads its stack.
4. The task returns from `ulTaskNotifyTake()` in the shared FreeRTOS host (`hosts/freertos`,
   [SPEC-ARCH-15][spec-arch]), runs `step()`, and `step()` resumes the coroutine. The host waits until the deadline
   `step()` returns, rounded up to whole ticks.

### Measured (µs; 1 µs = 200 cycles; ranges over two runs at idle, one run loaded)

| | AbstractX alone | AbstractX on FreeRTOS (`WAIT`) | FreeRTOS debug build |
| :--- | ---: | ---: | ---: |
| Yield round trip, `co_await yield_to_dispatcher()`, 10,000 runs: avg / max | 19.7 / 24.3-24.5 | 20.8-20.9 / 24.0-24.3 | 23.7 / 26.8-27.2 |
| Soft interrupt raised → handler entered, avg | 1.13-1.17 | 1.31-1.33 | 1.43 |
| Soft interrupt raised → waiting coroutine resumed, Linux idle, 1,000 runs: avg / max | 24.0-24.2 / 28.5-28.8 | 27.1 / 30.2-30.3 | 28.8-29.0 / 31.4-31.9 |
| Same, Linux loaded: avg / max | 23.8-24.1 / 28.7 | *not measured* | *not measured* |

* **The yield costs about twice what it did on 2026-10-31** (10.2 µs, 2,040 cycles then; 3,931 cycles now) on both
  runtimes, because a yield never leaves the event loop and `step()` checks every subsystem on each pass. The
  per-phase build ([SPEC-E906-BENCH-09][spec-bench], `*_step_profile_bench_idle.json`, not a timing row) shows where a
  pass goes on AbstractX alone: TLP and trace flush 1,988 cycles (47 %), dispatch 1,126, deadline 349, timer 324,
  network 217, I/O 152, task list 46. FreeRTOS shows the same order of phases. Making `step()` skip idle subsystems is
  open work (TODO §2b item 2).
* **Interrupt to coroutine**: AbstractX alone is about 620 cycles (3.1 µs) faster on average, and its worst case is
  lower too.
  * **Neither runtime is asleep in this row.** The bench raises the interrupt from inside the running coroutine. Both
    runtimes save the context, run the ISR and restore that same context.
  * **A wake from sleep is a different path on each.** AbstractX alone leaves `platform_idle_wait()`, then runs
    `step()`. FreeRTOS does the same save, ISR and restore, and adds the notify, the scheduler's choice of task, and
    loading a different task's stack. So the difference between the runtimes while sleeping is not shown here. `C`
    covers AbstractX alone; `F` (native FreeRTOS) has no valid run yet (§3).
* **This is a software interrupt** (`msip`), raised by the bench itself. It is a different event from §3's timer, so do
  not compare these rows with `cyclictest`.
* **FreeRTOS debug cost**: the default build (asserts, stack check, idle-hook statistics) adds about 3 µs to a yield and
  2 µs to the soft-interrupt resume.

### Memory

All three ELFs were built at [`2af893b`][c-2af893b] with:
* xPack GNU RISC-V GCC 15.2.0, Release (`-O2 -g -DNDEBUG`);
* freestanding C++20, `-fno-exceptions -fno-rtti`, `newlib-nano`.

All pass `tools/check_e906_layout.py`, use 0 bytes of DRAM and pass the zero-heap check, which fails the build on any
heap symbol (`tools/track_memory_footprint.py`, [SPEC-E906-MEM-03][spec-e906]). Sizes are from `riscv-none-elf-size -A`:

| | AbstractX alone | AbstractX on FreeRTOS (`WAIT`, timing) | FreeRTOS adds | FreeRTOS debug build |
| :--- | ---: | ---: | ---: | ---: |
| code (`.text` + `.fastcode`) | 83,208 B | 88,692 B | 5,484 B | 89,772 B |
| static RAM (`.data` + `.bss`) | 23,557 B | 37,429 B | 13,872 B | 37,437 B |
| `SRAM_FAST` used of 192 KB (sum of sections, as `tools/analyze_elf_size.sh`) | 113.0 KB (58.8 %) | 131.9 KB (68.7 %) | | 133.0 KB (69.2 %) |

* **What FreeRTOS adds**:
  * the scheduler (`tasks.c`, `list.c`, `queue.c`), the RISC-V port and trap entry;
  * static task stacks (`configSUPPORT_STATIC_ALLOCATION = 1`, no heap): 8 KB AbstractX task, 2 KB native cyclic task
    (`F`), 2 KB interrupt stack, 1 KB idle;
  * 72 B per task control block.

  Both builds also have the same 8 KB startup stack, and about 3.5 KB of cold code in SRAM Space 1 (`.text_cold`).
* **Since [`e159de6`][c-e159de6] (2026-10-31)**: code grew 7-8 % on both (77,516 → 83,208 B alone; 82,432 → 88,692 B
  on FreeRTOS), static RAM shrank on AbstractX alone (25,984 → 23,557 B), and what FreeRTOS adds went from 11.7 KB to
  13.9 KB. A row for [`e560d50`][c-e560d50] in the previous revision of this article (141,498 B of code, `SRAM_FAST`
  99.1 % on FreeRTOS) was not reproduced and is withdrawn.

Evidence: [`part4_size_20261111.json`](../../verification/evidence/2026-11/part4_size_20261111.json).

### Idle

* **AbstractX alone** has no periodic tick: it sleeps in `wfi` until an interrupt or a deadline it set. This firmware
  does not count idle time (`"idle": "not-instrumented"`).
* **FreeRTOS** (debug build, the only one that counts it): the idle hook woke 6,606 times in 4.43 s, and the core was
  outside the idle hook for 6.2 % of that window (run 2: 6,424 in 4.24 s, 6.5 %). That is the busy share of the whole
  benchmark run plus the 1 kHz tick, not the cost of the tick alone. An idle-only window was not measured.

---

## 6. When to Use Each

**Linux on the A55 cores**: anything that needs megabytes, such as vision, planners, storage, networking, UI and
Python. It handles soft real time. PREEMPT_RT kept a 1 kHz RT thread under 45 µs at idle and under 125 µs under heavy
load here. That suits 100 Hz - 1 kHz control that can take an occasional 0.1 ms delay. It is not for hard deadlines
under load, and a normal-priority thread is not real time at all under load (18 ms).

**The E906, for I/O and timing Linux should not own.** CPU and DRAM load on Linux did not change its timing. Close fast
loops on the E906 and send Linux only results. A result reaches an RT Linux thread in C in about 0.1 ms, but under load
one in 60,000 took 2.7 ms, so a deadline on the Linux side of the link is still a soft one.

**AbstractX alone on the E906** suits:
* work that moves data between buses and the host;
* event-driven sensor work;
* a shorter path from an interrupt to the coroutine (3.1 µs on average in the soft-interrupt test; a wake from sleep
  was not compared);
* coroutine sleeps shorter than a millisecond;
* a core that no periodic tick wakes.

**AbstractX on FreeRTOS**: when you need preemption.
* **Long compute jobs** (FFT, planners) go in a lower-priority task, and the coroutine task preempts them.
* **Libraries that expect a blocking `read()` or `sleep()`** run in their own task, through the `TaskNotifier`
  wrappers ([SPEC-E906-RTOS-08][spec-rtos]).
* **An existing FreeRTOS system** adopts coroutines in one task.
* **The cost on the E906**: about 3 µs per interrupt-to-coroutine resume, 11-14 KB of RAM (13.9 KB today, 11.7 KB on
  2026-10-31), and 1 ms coroutine sleep resolution.

The application code is the same in both E906 setups, so this choice can change later.

---

## 7. Reproduce

On a Radxa Cubie A5E with remoteproc and `rpmsg_char` (the kernel above), from a checkout of AbstractX. Set:
* `ABSTRACTX_BOARD_IP`: the board's address (root login over SSH with a key);
* `BUILDROOT_OUT`: the board's Buildroot output directory, for its `host/bin/aarch64-linux-gcc` (the one-way reader is
  cross-compiled with it).

**One command** ([SPEC-E906-BENCH-08][spec-bench]) builds every firmware variant, checks the board is the A5E, sets the
board clock, copies the tools and firmware over SSH (the board's busybox has no `scp`) and checks each by MD5, loads
each firmware through remoteproc, runs every row of §3-§5 idle and under `stress-ng`, stops if a thermal zone passes
85 °C, and saves the evidence with a manifest:

```bash
python3 tools/run_e906_bench_suite.py --ip $ABSTRACTX_BOARD_IP --buildroot-out $BUILDROOT_OUT --build --record
```

It completed every idle row on 2026-11-11 (twice); its E906 rows under load reset the board (§8). By hand, for one
firmware:

```bash
# 1. Build (FreeRTOS timing build: no debug cost; timer mode WAIT, XTIMER or TICK_HOOK)
cmake --preset e906 -B build_e906 && cmake --build build_e906 --target e906_bench_app -j
cmake --preset e906 -B build_e906_timer_wait -DABX_FREERTOS_TIMER=WAIT -DABX_FREERTOS_DEBUG=OFF
cmake --build build_e906_timer_wait --target e906_freertos_app -j
$BUILDROOT_OUT/host/bin/aarch64-linux-gcc -std=c11 -O2 -o build_e906/e906_oneway_reader \
    apps/e906_repl_app/tests/board/e906_oneway_reader.c

# 2. Copy over SSH (no scp on the board) and compare the MD5 on both sides
B=root@$ABSTRACTX_BOARD_IP; D=/root/abstractx_board_bench
ssh $B "date -u -s '$(date -u '+%Y-%m-%d %H:%M:%S')' && mkdir -p $D"
for f in apps/e906_repl_app/tests/board/bench_e906_runtime.py tools/e906_rpmsg_bridge.py tools/abstractx_wire.py \
         build_e906/e906_oneway_reader; do ssh $B "cat > $D/$(basename $f)" < $f; done
ssh $B "chmod +x $D/e906_oneway_reader"
ssh $B 'cat > /lib/firmware/abstractx.elf' < build_e906/apps/e906_bench_app/e906_bench_app
md5sum build_e906/apps/e906_bench_app/e906_bench_app; ssh $B md5sum /lib/firmware/abstractx.elf

# 3. Load it and run the rows (watch /sys/class/thermal/thermal_zone*/temp)
ssh $B "R=/sys/class/remoteproc/remoteproc0; echo stop > \$R/state; echo abstractx.elf > \$R/firmware; \
        echo start > \$R/state; sleep 2; cd $D; \
        python3 bench_e906_runtime.py --json /tmp/bench.json; \
        python3 bench_e906_runtime.py --cyclic 60000 --json /tmp/cyclic.json; \
        python3 bench_e906_runtime.py --oneway 60000 --rt --json /tmp/oneway_c_rt.json; \
        python3 bench_e906_runtime.py --oneway 60000 --reader python --json /tmp/oneway_py_other.json"
#    FreeRTOS firmware only: bench_e906_runtime.py --native-cyclic 60000 --json /tmp/native.json

# 4. Linux on the A55 cores
ssh $B 'cyclictest -m -p 95 -i 1000 -l 60000 -t1 -a 2 -q -h 20000'   # RT; drop -p 95 for SCHED_OTHER
ssh $B 'stress-ng --cpu 8 --vm 2 --vm-bytes 256M --sock 2 --timeout 75s -q & sleep 5; \
        cyclictest -m -p 95 -i 1000 -l 60000 -t1 -a 2 -q -h 20000; wait'
```

For a row under load, run it inside the same `stress-ng` wrapper as the last command.

---

## 8. Not Measured, and Blocked

**Blocked: the E906 rows under load reset the board.** With the E906 running its 1 ms timer test (`C`, the one-way
test or the FreeRTOS runs) and `stress-ng` on Linux, the A5E reset within seconds, with no oops, panic or shutdown on
the console: the next line is U-Boot SPL
([serial log](../../verification/evidence/2026-11/a5e_reset_serial_20261111.txt)). The hottest thermal zone was
46-51 °C. `stress-ng` alone with the firmware loaded and idle did not reset it, nor did the RT `cyclictest` under load
with the E906 stopped. Some rows under load did complete (AbstractX alone: `bench`, `C` and both C readers in run 2), so
it is intermittent. The same kind of rows ran under load on 2026-10-31, on an earlier kernel and board configuration.
Cause not found ([`part4_load_manual_20261111.json`](../../verification/evidence/2026-11/part4_load_manual_20261111.json),
[`engineering_log.md`](../../../engineering_log.md) 2026-11-11). Until it is found, these stay *not measured*:
* every AbstractX on FreeRTOS row under load (cyclic in each timer mode, echo, soft interrupt, one way);
* the Python readers under load, and a second loaded run of every row;
* `linux_cyclictest_rt_load` from the suite (the hand-run row in §3 has no histogram).

**Not measured** (each is a TODO §2b item):
* `F`, the native FreeRTOS task on `vTaskDelayUntil`: its first run is void (§3, note 4); the fix needs a board run;
* AbstractX on FreeRTOS on a wake from sleep, compared with AbstractX alone (`C` on FreeRTOS includes the tick phase);
* the cycle split of the ~2,430-cycle timer wake (the per-phase build covers `step()` only);
* why the yield and the echo got slower since 2026-10-31 (firmware, kernel and four cores all changed);
* where the 2.7 ms one-way worst case under load is spent inside Linux;
* long runs (hours) for worst cases; the runs here are one minute each;
* a load aimed at the shared bus (DMA-heavy, not only CPU and DRAM), to test the E906's isolation;
* CPU isolation for the Linux reader (`isolcpus`, IRQ affinity away from CPU 2); none was set;
* idle accounting in the AbstractX firmware, and an idle-only window on both;
* IMU DRDY → data (Part 6, optional here).

**Related, not covered here**:
* FreeRTOS task switches in the CTF trace and the AbstractX Studio timeline ([SPEC-E906-RTOS-07][spec-rtos]).
* AbstractX drivers used from FreeRTOS tasks, with no second set of drivers ([SPEC-E906-RTOS-08][spec-rtos]).
* Bugs found while measuring, which caused ~1 s echo stalls before [`e159de6`][c-e159de6]
  ([`engineering_log.md`](../../../engineering_log.md), 2026-10-31):
  * a lost RPMsg wakeup ([SPEC-E906-IRQ-04][spec-e906]);
  * the core going back to sleep with work waiting ([SPEC-E906-IRQ-06][spec-e906]).
* The 2026-10-31 measurements this revision replaces are in `docs/verification/evidence/*_20261031.json`.

[spec-e906]: ../../../targets/allwinner_e906/SPECIFICATION.md
[spec-bench]: ../../../apps/e906_bench/SPECIFICATION.md
[spec-rtos]: ../../../apps/e906_freertos_app/SPECIFICATION.md
[spec-arch]: ../../DESIGN_SPECIFICATION.md
[c-2af893b]: https://github.com/tcmichals/AbstractX/commit/2af893b
[c-e159de6]: https://github.com/tcmichals/AbstractX/commit/e159de6
[c-e560d50]: https://github.com/tcmichals/AbstractX/commit/e560d50
