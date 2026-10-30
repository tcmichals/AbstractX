# Where Should the IMU Live on the T527? Linux on the A55, or the E906?

## Part 6 of *Bringing Up Heterogeneous RISC-V on Allwinner SoCs* (Radxa Cubie A5E)

> **Status: draft (2026-10-30).** Only the E906 bare-metal column is measured on the board.
> Every other number in this article is marked *not measured* until it has an evidence file in
> `docs/verification/evidence/`. Nothing here is an estimate dressed up as a result.

**The question.** The Allwinner T527 on the Radxa Cubie A5E has eight Cortex-A55 cores running
Linux and one 200 MHz XuanTie E906 RISC-V core that Linux leaves idle. A flight controller or
robot needs an IMU sampled at a steady rate, with no lost samples and a known delay from "the
sensor has data" to "the code has the data". Where should that code run?

**AbstractX's answer is the E906** ([`[SPEC-ARCH-14]`](../../DESIGN_SPECIFICATION.md)): the
accelerator owns the bus and Linux sees results. The Linux columns below are the baseline being
measured against, run with the same channel code so the comparison is fair, not a candidate
for the product. This article puts numbers on why.

We run **the same work** on each runtime and measure it the same way:

* ICM-42688-P on SPI1 CS0 at 10 MHz, 1 kHz output data rate, gyro + accel + temperature
  (14 bytes per sample).
* The IMU's data-ready pin (INT1 -> PI8, header pin 29) starts each read. Nobody polls.
* One read per edge: `0x9D` (`TEMP_DATA1 | 0x80`) + 14 bytes, started by the edge, landing in
  a 64-byte TLP stamped with the edge time.
* Report: samples, edges, overruns, drops, sample period, and **edge -> data in the consumer**
  (min / avg / max), with the same keys on every runtime (bench `A`, [SPEC-E906-BENCH-05]).

## The four candidates

| Runtime | Edge reaches code via | Read started by | Consumer | Code |
| :--- | :--- | :--- | :--- | :--- |
| E906 bare metal + AbstractX | PI8 -> GIC 115 -> S_INTC -> CLIC line 100 | edge handler, SPI1 DMA | coroutine on `abstractx::step()` | `apps/e906_bench`, `hal::gpio_irq` |
| E906 FreeRTOS + AbstractX | same line, FreeRTOS trap -> `hal::irq::dispatch` | edge handler, SPI1 DMA | coroutine inside one FreeRTOS task | `apps/e906_freertos_app` |
| E906 MicroPython | same line and DMA | edge handler, SPI1 DMA | Python, `abstractx.imu_drdy_read_into()` | `apps/e906_repl_app/imu_drdy_bench.py` |
| Linux A55 + AbstractX | kernel GPIO IRQ -> `gpiod` event (epoll) | `spidev` ioctl on a worker thread | coroutine / engine thread | `targets/linux` (`[SPEC-LINUX-GPIO-01]`) |

The first three share every line of driver code: the same `IoEngine` auto channel, the same
GPIO-edge handler and the same DMA path. Only the consumer differs. That is the point: the
difference between columns is the runtime, not a rewrite.

## Results

| | E906 bare metal | E906 FreeRTOS | E906 MicroPython | Linux A55 |
| :--- | ---: | ---: | ---: | ---: |
| Samples / edges | 10000 / 10000 | *not measured* | *not measured* | *not measured* |
| Overruns, busy drops, DMA failed | 0, 0, 0 | *not measured* | *not measured* | *not measured* |
| Sample period min / avg / max | 971.5 / 991.3 / 1012.0 µs | *not measured* | *not measured* | *not measured* |
| Edge -> DMA started | 5.8 / 5.9 / 6.8 µs | *not measured* | *not measured* | *not measured* |
| DMA (15 B at 10 MHz) | 13.0 / 14.2 / 18.5 µs | *not measured* | *not measured* | *not measured* |
| **Edge -> data in consumer** | **30.5 / 31.0 / 45.0 µs** | *not measured* | *not measured* | *not measured* |
| Coroutine pool high water | 1152 | *not measured* | *not measured* | — |

Source: `docs/verification/evidence/e906_imu_drdy_baremetal.json` (one 10000-sample run; Cubie
A5E, kernel 7.1, commit `c97c331` on 2026-10-29, the file's 1970 timestamp is the unset board
clock). The commit message of `c97c331` reports two more back-to-back runs with the same counts
and the same pool high water; only one run is in the evidence file. Edge -> DMA and DMA times
are `mcycle` counts at 200 MHz converted to µs.

### What the bare-metal column already says

* **Nothing was lost.** In the recorded run every one of 10,000 edges produced exactly one DMA and one sample.
* **The read starts 6 µs after the edge** and is done ~20 µs after it. The remaining ~11 µs to
  the consumer is the coroutine wake path (`IsrDispatcher` -> `step()` -> resume).
* **The period spread (971..1012 µs) is not ours.** The handler cost is flat (1165..1362 cycles),
  so the ±20 µs comes from the IMU's own clock or from interrupt entry. Telling them apart needs
  a logic analyzer on DRDY; until then it is an open question, not a result.

## Why the other columns are empty

* **FreeRTOS**: until 2026-10-30 the FreeRTOS interrupt handler switched off CLIC lines 88 (DMA)
  and 100 (DRDY) as "unexpected". It now routes them through the same dispatch table as bare
  metal (`b42eb5e`). The firmware builds and its interrupt path is free of FP instructions (the
  FreeRTOS port does not save FP registers), but it has not run on the board yet.
* **MicroPython**: the binding exists (`7751c3f`); `imu_drdy_bench.py` prints the bench keys.
  Not run on the board yet. Expect the consumer column to include the interpreter: each
  `imu_drdy_read_into()` is a bytecode call plus one 22-byte copy (the Python heap lives in
  HiFi4 DSP RAM, which the DMA cannot reach).
* **Linux**: bank-I GPIO edges latch in the PIO (`EINT_STA` bit 8) but never reach Linux's GIC
  on kernel 7.1 (`gpiomon` count stays 0). Until that is fixed, or DRDY is wired to a bank Linux
  does receive, the Linux column cannot be measured at all. The Linux back end itself now
  honours the GPIO chip and `spidev<bus>.<cs>` it is given and refuses to fall back to
  simulation on real hardware; it is tested on the host with simulated edges only.

## One clock for every column (still open)

The E906 columns measure edge -> consumer on the E906's own clock. Comparing them with Linux
needs one clock for all: either jumper DRDY to a second GPIO in a bank Linux timestamps, or put a
logic analyzer on DRDY and a "sample consumed" GPIO. Both are listed in `TODO.md` section 2a.

## Reproduce (bare-metal column)

```bash
cmake --preset e906 && cmake --build build_e906 --target e906_bench_app -j
scp -O build_e906/apps/e906_bench_app/e906_bench_app root@<board>:/lib/firmware/e906_bench_app.elf
ssh root@<board> 'cd /sys/class/remoteproc/remoteproc0 && echo stop > state; \
                  echo e906_bench_app.elf > firmware && echo start > state'
ssh root@<board> 'python3 /root/abstractx_board_bench/bench_e906_runtime.py --imu 10000 --json imu.json'
```

For the FreeRTOS column load `e906_freertos_app` instead; for MicroPython load `e906_repl_app`
and run `imu_drdy_bench.py` with `run_e906_script.py`.
