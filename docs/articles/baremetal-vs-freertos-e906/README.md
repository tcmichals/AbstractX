# Bare Metal + AbstractX vs FreeRTOS + AbstractX on the XuanTie E906

## Part 4 of *Bringing Up Heterogeneous RISC-V on Allwinner SoCs* (Radxa Cubie A5E)

**Status: Verified on hardware.** Every performance metric and memory footprint reported below is taken directly from benchmark runs on the Radxa Cubie A5E (Allwinner T527 XuanTie E906 @ 200 MHz).
Evidence: [`docs/verification/evidence/e906_bench_baremetal.json`](../../verification/evidence/e906_bench_baremetal.json) and [`docs/verification/evidence/e906_bench_freertos.json`](../../verification/evidence/e906_bench_freertos.json).
Requirements: [`apps/e906_bench/SPECIFICATION.md`](../../../apps/e906_bench/SPECIFICATION.md), [`apps/e906_freertos_app/SPECIFICATION.md`](../../../apps/e906_freertos_app/SPECIFICATION.md).

---

## 1. The Core Question

AbstractX provides freestanding C++20 coroutines, a lock-free ISR-to-coroutine dispatcher, zero-copy VirtIO RPMsg transports, and cycle-accurate binary telemetry. It runs entirely freestanding without requiring an operating system.

When does adding FreeRTOS underneath help, and what does it cost?

On the Allwinner T527's XuanTie E906—a 32-bit RISC-V core (RV32IMAFC) running at 200 MHz with 192 KB of tightly coupled on-chip SRAM—we answered this by running **the exact same benchmark application** (`apps/e906_bench/bench.cpp`) compiled under both runtimes:

![Architecture Comparison: Bare Metal vs FreeRTOS](assets/table1-architecture-comparison.svg)

---

## 2. One Benchmark, Two Runtimes

To ensure an authentic like-for-like comparison, [`apps/e906_bench/bench.cpp`](../../../apps/e906_bench/bench.cpp) is compiled unchanged into both firmwares:
* **Bare Metal (`apps/e906_bench_app`)**: Initializes the AbstractX runtime and runs the cooperative loop `abstractx::run(bench_main())`. When no coroutines have pending resumptions, it executes low-power `wfi` sleep in `platform_idle_wait()`.
* **FreeRTOS (`apps/e906_freertos_app`)**: Spawns one high-priority FreeRTOS task (`abstractx_task`) running `abstractx::step()`. When the dispatcher has drained, the task blocks in `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)`.

Both serve VirtIO RPMsg endpoint 1030 (`abstractx-bench`). The Linux host on the Cortex-A55 application cores exercises:
1. **Echo Round-Trip Latency & Throughput**: 1,000 messages each of 16, 256, and 496 bytes, measuring min, median, p99, and max round-trip latency (µs).
2. **On-Target Suite (`R` command)**: Timed with RISC-V `mcycle` at 200 MHz:
   * *Yield Round Trip*: One `co_await yield_to_dispatcher()`, measuring loop dispatch overhead (10,000 iterations).
   * *Soft-Interrupt Wake*: CLINT machine software interrupt (`msip`, CLIC line 3) trigger $\rightarrow$ ISR entry $\rightarrow$ waiting coroutine resumed (1,000 iterations).
   * *Idle Accounting*: Sleep cycles and periodic wake-ups.

---

## 3. Execution Model & Wake Path Comparison

The key architectural distinction lies in how an interrupt propagates to a coroutine:

![Execution Path Comparison: Hardware IRQ to Coroutine Resume](assets/wake-path-comparison.svg)

### Bare Metal: Pure SPSC Dispatch
1. The hardware interrupt fires on the T-Head CLIC (e.g., CLIC line 17 for the RPMsg message box).
2. The ISR executes, clears the interrupt condition, and posts the waiting coroutine's handle into `IsrDispatcher`'s lock-free single-producer single-consumer (SPSC) ring.
3. The ISR executes `mret`. If the CPU was suspended in `wfi` inside `platform_idle_wait()`, execution resumes immediately at the next instruction.
4. `abstractx::run()` drains the dispatcher and directly invokes `coro.resume()`. Zero register stacking, zero context saving, and zero scheduler overhead.

### FreeRTOS: SPSC Dispatch + Task Notification
1. The hardware interrupt traps into `freertos_risc_v_trap_handler` in `portASM_e906.S`.
2. The ISR posts the handle to `IsrDispatcher`, then signals `vTaskNotifyGiveFromISR(s_ax_task)` followed by `portYIELD_FROM_ISR()`.
3. If the `abstractx` task was blocked in `ulTaskNotifyTake()`, the FreeRTOS scheduler performs a full context switch: saving 32 integer registers, updating the current TCB, and restoring the target task's stack frame.
4. The task unblocks and calls `abstractx::step()` to resume the coroutine.

---

## 4. Observability: Connecting FreeRTOS to barectf & AbstractX Studio

AbstractX incorporates a dynamic CTF 1.8 / barectf binary trace engine (`[SPEC-TRACE-09]`, `[SPEC-TRACE-10]`) streaming over VirtIO RPMsg endpoint 1025 (`abstractx-trace`).

By instrumenting FreeRTOS kernel hooks in `FreeRTOSConfig.h`:
```c
/* [SPEC-E906-RTOS-07] BareCTF / Studio Timeline Trace Integration */
#define traceTASK_SWITCHED_IN()  abx_trace_task_switched_in()
#define traceTASK_SWITCHED_OUT() abx_trace_task_switched_out()
```

Each FreeRTOS task switch captures cycle timestamps (`mcycle`) and pushes structured timeline spans directly into AbstractX's CTF Stream 5:

```cpp
// @impl [SPEC-E906-RTOS-07] apps/e906_freertos_app/SPECIFICATION.md
void abx_trace_task_switched_out(void) {
    const uint64_t now = abx_bench_mcycle();
    if (s_task_in_cycles != 0 && now >= s_task_in_cycles) {
        const uint64_t duration = now - s_task_in_cycles;
        TaskHandle_t cur = xTaskGetCurrentTaskHandle();
        const uint32_t taskId = cur ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cur)) : 0;
        abstractx::events::span(abstractx::events::kResume, taskId, s_task_in_cycles, duration);
    }
}
```

### What AbstractX Studio Visualizes:
* **Timeline Tab**: Displays FreeRTOS tasks (the `abstractx` task, background worker threads, and the idle task) as live, cycle-accurate timeline lanes alongside hardware ISRs.
* **Kernel Tick Observability**: Visually inspect the 1 kHz CLINT timer tick (CLIC line 7) and quantify exactly how many cycles each tick steals from application processing.
* **Nested Coroutine Visibility**: When the `abstractx` task runs, coroutines dispatched inside `abstractx::step()` render within the task's execution window.

---

## 5. Driver Architecture: Reusing AbstractX HAL with FreeRTOS Wrappers

AbstractX drivers (SPI, UART, I²C, DMA) are written as freestanding, event-driven C++20 HAL drivers (`[SPEC-HAL-*]`). They never block or spin.

Rather than re-implementing peripheral drivers for FreeRTOS, FreeRTOS tasks use synchronous adapters via `TaskNotifier` (`[SPEC-E906-RTOS-08]`):

![Zero-Duplication Driver Architecture](assets/freertos-driver-wrapper.svg)

```cpp
// @impl [SPEC-E906-RTOS-08] apps/e906_freertos_app/include/freertos_driver_wrapper.hpp
abstractx::freertos::TaskNotifier notifier;

notifier.prepare();  // Captures xTaskGetCurrentTaskHandle()
hal_driver.start_transfer(request);

// Calling task suspends cooperatively without busy-waiting:
if (notifier.wait(timeout_ticks)) {
    // Transfer complete (woken by ISR via vTaskNotifyGiveFromISR)
}
```

* **Zero Duplicated Silicon Logic**: Registers, FIFOs, DMA descriptors, and CLIC interrupt routing stay in one place (`targets/allwinner_e906/`).
* **Cooperative Blocking**: When a FreeRTOS task waits for slow I/O, it yields the CPU, allowing lower-priority tasks to execute or the CPU to sleep in `wfi`.

---

## 6. Memory Footprint Analysis

Both ELFs were compiled with xPack GNU RISC-V GCC 15.2.0 (Release, `-O2 -g -DNDEBUG` since 2026-10-28; earlier Release builds were really `-O3`), freestanding C++20, `-fno-exceptions -fno-rtti`, and linked with `newlib-nano`. Memory layouts are verified against Allwinner T527 remoteproc windows via `tools/check_e906_layout.py`:

![Memory Budget: Static RAM and Stack Allocation Breakdown](assets/table2-memory-footprint.svg)

* **Code Size**: FreeRTOS adds only **4,986 bytes** of code (`.text`), encompassing the kernel scheduler (`tasks.c`, `list.c`, `queue.c`), the RISC-V port, and trap entry assembly.
* **Static RAM**: FreeRTOS requires **11,696 bytes** of additional static RAM. Because AbstractX enforces zero dynamic heap allocation (AGENTS §2, `configSUPPORT_STATIC_ALLOCATION = 1`), task control blocks and stacks are statically allocated (8 KB `abstractx` stack, 2 KB ISR trap stack, and 1 KB idle stack).
* **Heap Usage**: neither build compiles a FreeRTOS `heap_*.c`, and no AbstractX code calls `malloc()`. But newlib's `_malloc_r`, `_free_r` and `_sbrk` are still linked into both ELFs (`riscv-none-elf-nm`, 2026-10-30; EL-1027-31), so "zero heap" is a property of our code, not yet of the binary.

---

## 7. Empirical Hardware Benchmarks

The benchmark suite was executed on real Radxa Cubie A5E silicon over VirtIO RPMsg endpoint 1030. The evidence files (`docs/verification/evidence/e906_bench_baremetal.json`, `e906_bench_freertos.json`) carry 1970 timestamps because the board clock was not set; the runs date from late October 2026.

All figures below are taken directly from the verified evidence runs:

![Silicon Benchmark Measurements: Bare Metal vs FreeRTOS](assets/table3-measured-benchmarks.svg)

### Key Performance Findings:
1. **Yield Round-Trip Parity**: A full loop pass (`co_await yield_to_dispatcher()`) takes **1,961 cycles (9.80 µs)** on bare metal vs **1,997 cycles (9.98 µs)** under FreeRTOS—a difference of just **36 clock cycles (180 ns)**.
2. **Interrupt Latency Trade-Off** (soft interrupt, trigger -> coroutine resumed):
   * FreeRTOS achieves slightly faster soft-IRQ trigger-to-coroutine wake (**12.69 µs** vs **16.28 µs**) because `vTaskNotifyGiveFromISR` immediately queues the task for execution before returning, whereas bare metal polls pending flags at the top of the idle loop.
   * Both runtimes comfortably satisfy sub-20 µs real-time interrupt response requirements.
3. **RPMsg Round-Trip Latency**:
   * Median echo latencies are close (16 B: 100.8 µs bare metal, 111.2 µs FreeRTOS; 496 B: 328.7 µs vs ~330 µs), and p99 is under 400 µs on both.
   * **But the bare-metal run has ~1 s outliers**: the maximum echo time was 999 ms (16 B), 997 ms (256 B) and 968 ms (496 B), against 214 / 292 / 388 µs under FreeRTOS. That is why the bare-metal 496 B throughput is 0.20 MB/s against 2.98 MB/s for FreeRTOS. The cause is not known yet; a one-second stall matches the once-a-second status report deadline, which is the first thing to check.
4. **Idle and the Tick** (what was and was not measured):
   * **Bare Metal**: the bench firmware does not count idle time (`"idle": "not-instrumented"` in the evidence), so there is no measured idle load or wake-up count for it. By design it has no periodic tick: it sleeps in `wfi` until an interrupt or a timer deadline it armed.
   * **FreeRTOS**: the idle hook woke **4,514 times** in a window of 439.1 M cycles (2.2 s at 200 MHz), and the core was outside the idle hook for **5.5 %** of that window. That 5.5 % is the busy share of the whole benchmark run (yield loop, soft-IRQ test, RPMsg traffic) plus the 1 kHz tick, **not** the cost of the tick alone, and the wake-ups include every interrupt, not just the tick. An idle-only window is still to be measured.

---

## 8. Architectural Decision Guide

![When to Choose Bare Metal vs FreeRTOS + AbstractX](assets/table4-decision-matrix.svg)

### Choose Bare Metal + AbstractX when:
* **The system is 100% event-driven**: Sensor fusion (IMU, compass, GNSS), telemetry streaming, and state estimation where all I/O is non-blocking.
* **RAM is constrained**: You cannot afford to allocate dedicated 4–8 KB stacks per task and need maximum SRAM available for buffers and profiler rings.
* **Power is critical**: You cannot tolerate 1,000 periodic timer wake-ups per second waking the CPU from low-power `wfi`.

### Choose FreeRTOS + AbstractX when:
* **You have mixed workloads**: High-rate coroutine sensor ingestion running in the `abstractx` task, alongside long-running compute jobs (FFT, path planners, matrix inversions) that must be preempted by real-time I/O.
* **Integrating legacy third-party libraries**: Code that expects traditional synchronous APIs (`read()`, `write()`, `sleep()`) can run in a dedicated FreeRTOS task using AbstractX's `TaskNotifier` wrappers without blocking coroutines.
* **Multiple developers or RTOS legacy**: Teams familiar with FreeRTOS semantics can adopt AbstractX coroutines incrementally inside a single RTOS task while leaving existing tasks intact.

---

## 9. Reproduce

To reproduce these measurements on a Radxa Cubie A5E:

```bash
# 1. Cross-compile both firmwares on host
cmake -S . -B build_e906 -DABSTRACTX_TARGET=e906 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-none-elf.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build_e906 --target e906_bench_app e906_freertos_app -j

# 2. Deploy FreeRTOS firmware to board
scp -O build_e906/apps/e906_freertos_app/e906_freertos_app root@192.168.3.3:/lib/firmware/e906_freertos_app.elf
ssh root@192.168.3.3 "echo stop > /sys/class/remoteproc/remoteproc0/state && \
                      echo e906_freertos_app.elf > /sys/class/remoteproc/remoteproc0/firmware && \
                      echo start > /sys/class/remoteproc/remoteproc0/state"

# 3. Run benchmark suite from Linux
ssh root@192.168.3.3 "python3 /root/abstractx_board_bench/bench_e906_runtime.py --json freertos.json"

# 4. Deploy Bare Metal firmware to board
scp -O build_e906/apps/e906_bench_app/e906_bench_app root@192.168.3.3:/lib/firmware/e906_bench_app.elf
ssh root@192.168.3.3 "echo stop > /sys/class/remoteproc/remoteproc0/state && \
                      echo e906_bench_app.elf > /sys/class/remoteproc/remoteproc0/firmware && \
                      echo start > /sys/class/remoteproc/remoteproc0/state"

# 5. Run benchmark suite from Linux
ssh root@192.168.3.3 "python3 /root/abstractx_board_bench/bench_e906_runtime.py --json baremetal.json"
```
