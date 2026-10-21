# Bare Metal + AbstractX vs FreeRTOS + AbstractX on the XuanTie E906

**Status: draft. Method and firmware exist; no board numbers yet.** Every cell marked
*not measured yet* stays that way until a quoted run of `tests/board/bench_e906_runtime.py`
fills it. Requirements: [`apps/e906_bench/SPECIFICATION.md`](../../apps/e906_bench/SPECIFICATION.md),
[`apps/e906_freertos_app/SPECIFICATION.md`](../../apps/e906_freertos_app/SPECIFICATION.md).

## The question

AbstractX gives you coroutines, a lock-free ISR-to-coroutine dispatcher and zero-copy
transports. It does not need an RTOS. So when does adding FreeRTOS underneath help, and
what does it cost? On the Allwinner T527's XuanTie E906 (RV32, 200 MHz, 192 KB of fast
SRAM) we answer that by running **the same benchmark code** under both runtimes.

| | Bare metal + AbstractX | FreeRTOS + AbstractX |
| :--- | :--- | :--- |
| Firmware | `apps/e906_bench_app` | `apps/e906_freertos_app` |
| Who runs coroutines | `abstractx::run()` loop, `wfi` when idle | one FreeRTOS task, blocks on a task notification |
| ISR → coroutine | ISR posts to `IsrDispatcher` | ISR posts to `IsrDispatcher` and notifies the task |
| Timer | none needed unless coroutines sleep | 1 kHz kernel tick (CLINT `mtime`, CLIC line 7) |
| Other work | only coroutines | can add preemptive tasks beside AbstractX |

## Method

Both firmwares link `apps/e906_bench/bench.cpp` unchanged and serve RPMsg endpoint 1030
(`abstractx-bench`). The Linux side (`tests/board/bench_e906_runtime.py`, stdlib only) runs:

1. **Echo round trip**: 1000 messages each of 16, 256 and 496 bytes; min / median / p99 / max
   µs, and throughput. This includes the Linux rpmsg char driver, the message box interrupt
   (CLIC line 17), the dispatcher and one copy into a bench slot.
2. **On-target suite** (`R` request), timed with `mcycle` at 200 MHz:
   * *yield round trip*: one `co_await yield_to_dispatcher()`, i.e. one full pass of the
     runtime loop (10000 runs);
   * *soft interrupt wake*: CLINT `msip` (CLIC line 3) → ISR entry → coroutine resumed
     (1000 runs);
   * *idle*: idle-task wake-ups and cycles (FreeRTOS only for now);
   * health: `dropped_resumes`, coroutine pool high-water, dropped RPMsg messages.
3. **Footprint**: `size` and `tools/analyze_elf_size.sh` on both ELFs.

## Results

| Metric | Bare metal + AbstractX | FreeRTOS + AbstractX |
| :--- | :--- | :--- |
| Code (`.text`), bytes | 61,058 (build, see below) | 66,256 (build, see below) |
| Static RAM (`.data` + `.bss`), bytes | 27,796 | 39,492 |
| Yield round trip (cycles / ns) | not measured yet | not measured yet |
| Soft IRQ → ISR entry | not measured yet | not measured yet |
| Soft IRQ → coroutine resumed | not measured yet | not measured yet |
| RPMsg echo 16 B, median / p99 | not measured yet | not measured yet |
| RPMsg echo 496 B, median / p99 | not measured yet | not measured yet |
| RPMsg throughput 496 B | not measured yet | not measured yet |
| Idle wake-ups per second | not measured yet | not measured yet |

The footprint row comes from `riscv-none-elf-size` on the 2026-10-21 cross-build
(xPack GCC 15.2.0, Release). FreeRTOS's static RAM includes an 8 KB task stack, a 2 KB ISR
stack and the idle task; bare metal uses the startup stack. Both ELFs currently also link
newlib's `free`/`_malloc_r` through a sized `operator delete` reference in
`libabstractx_core` (see `engineering_log.md`, 2026-10-21); nothing calls them.

## When to pick which (expectations to confirm or refute)

These are the hypotheses the numbers will test, not conclusions:

* **Bare metal + AbstractX** when everything is event driven and cooperative: sensor
  fusion, RPMsg/TLP bridging, the MicroPython REPL. Expect the shortest ISR-to-coroutine
  path (no task notification, no scheduler), no periodic tick, and the smallest RAM.
* **FreeRTOS + AbstractX** when some work must preempt or be preempted: a long blocking
  vendor library, a CPU-heavy job that must not delay coroutine I/O, or code that already
  uses FreeRTOS APIs. Expect an extra notify-and-switch step on each wake and a 1 kHz tick
  even when idle.
* Either way the application code is identical: the coroutines do not know which runtime
  is underneath. That is the property this benchmark protects.

## Reproduce

```bash
cmake -S . -B build_e906 -DABSTRACTX_TARGET=e906 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-none-elf.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build_e906 --target e906_bench_app e906_freertos_app
scp -O build_e906/apps/e906_freertos_app/e906_freertos_app root@<board>:/lib/firmware/e906_freertos_app.elf
# on the board
echo stop > /sys/class/remoteproc/remoteproc0/state
echo e906_freertos_app.elf > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
python3 tests/board/bench_e906_runtime.py --json freertos.json
# repeat with e906_bench_app.elf, then restore riscv-firmware.elf
```

The bare-metal firmware needs the CLIC interrupt path (`mtvec` CLIC mode, line 17
dispatch, and line 3 routed to `abx_bench_soft_isr()`) before it can answer at all;
until then it reports `softirq=unsupported`, and without a message box interrupt it
never wakes.
