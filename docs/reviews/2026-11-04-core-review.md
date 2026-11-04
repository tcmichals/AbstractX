# AbstractX Core Review: 2026-11-04 (step 1)

**Scope:** the core runtime (timers, dispatcher, `step()`, hosts), the data paths between producers
and coroutines (queues, awaiters, combinators, driver base, I/O engine, Linux bus drivers), and the
tests and gates that are supposed to catch problems. Commit reviewed: `db032f8`; audit tools from
the `full-audit` skill ([SPEC-AUDIT-17]) run on the working tree on top of it.

**Not in scope yet (next steps):** comparison with FreeRTOS / Zephyr / ChibiOS / Linux and the bus
API efficiency review (`platforms` and `buses` checklists of the `full-audit` skill), GUI click tests,
RTL, board runs.

**How it was done:** three read-only review passes in parallel (time and scheduling; data paths;
tests and gates), each told to prove findings with a program, plus the automatic stages of
`tools/full_audit.py`. Reviewers were seeded with some suspicions (T-1, T-7), so those two are
confirmed by measurement, not by the reviewer agreeing.

**Evidence legend:**
- **[V]** reproduced by a program. The programs are in [`2026-11-04-probes/`](2026-11-04-probes/)
  (see "Re-running the probes"). Every [V] in sections 1 and 2 was re-run a second time on
  2026-11-04 after the review passes, with the same result.
- **[R]** read in the code, with exact lines. Not yet shown by a program.
- **[I]** inferred. Not confirmed in code or on hardware.

A finding counts as **confirmed** only once a failing test or lint rule exists for it
([SPEC-AUDIT-17]). None does yet: every item below is open until its fix lands with a test that
fails on the old code and passes on the new.

---

## 0. Summary, ranked

| # | Sev | Finding | Ev. |
| --- | --- | --- | --- |
| T-1 | High | **Fixed in `583637b`.** Concurrent `ITimer` sleeps run one after another: three 10 ms sleeps wake at 10, 20 and 30 ms (E906, Linux and Pico share the path) | [V] |
| T-2 | High | **Fixed in `583637b`.** `with_timeout` leaves its timeout sleep holding the timer. Later sleeps are delayed by the whole timeout, and after 16 of them every `with_timeout` fails at once | [V] |
| T-3 / Q-7 | High | Destroying a suspended task leaves its handle and result pointer in driver requests and wait slots. The completion writes into the freed frame and resumes whatever reused it | [V] |
| Q-1 | High | **Fixed 2026-11-04.** Linux I2C reports `Ok` for transfers it never sends: 2-write `write_regs`, 3 or more messages, zero-length probe | [V] |
| Q-2 | High | **Fixed 2026-11-04.** A TLP read/write/transaction whose submit fails returns an earlier response as if it were fresh | [V] |
| Q-3 | High | **Fixed 2026-11-04.** `IoEngine` auto-channel data race: a new edge overwrites the snapshot, timestamp and length while the previous completion still reads them | [V] TSan |
| G-1 | High | ctest runs whatever binary exists; a source that no longer compiles still "passes" | [V] |
| G-2 / G-3 | High | VERIFIED can be permanent (ledger record with commit `HEAD`), and VERIFIED needs no `@verifies` test | [V] |
| G-17 | High | `936d023` log entry claims "100% pass" while the same commit broke two host tests; its new I2C retry blocks with `delay_us(200)` | [V] |
| T-4 | Med | Dispatcher holds 32 handles; the 33rd wake is dropped (counted only) and that coroutine hangs forever | [V] |
| T-5 | Med | **Fixed 2026-11-04** (hosted: every enqueue wakes; Linux timer Polled). Driver completions post the handle but never call `wake()`, so a host that waits on `step()`'s deadline sleeps through them (Linux) | [R] |
| T-6 | Med | **Fixed 2026-11-04 (not run on a board).** Pico idle is a bare `wfe` with no masked re-check; the SDK alarm can fire on core 0 while AbstractX sleeps on core 1 | [I] |
| Q-4 | Med | **Fixed 2026-11-04.** `AsyncQueue`: one waiter slot per side (a second consumer is never woken), push result ignored, no drop counter, not thread-safe despite the "lock-free" comment | [V] |
| Q-5 | Med | Linux SPI/I2C: in-flight requests cannot be cancelled, so `when_any` / `with_timeout` cannot cancel the loser's I/O | [V] |
| Q-6 | Med | A `when_any` loser that never completes keeps its frame: 200 races leak 25,600 B of pool (E906 pool is 4 KB) | [V] |
| Q-8 | Med | **Fixed 2026-11-04.** Linux UART `write_async` reports `Ok` on a short write or `EAGAIN` | [V] |
| G-4 … G-11, G-14 | Med | Test-claim and test-strength gaps (section 3) | [V]/[R] |
| T-7, T-8, Q-9 … Q-12, G-12, G-13, G-15, G-16, G-18 | Low | Section 1 to 3 | [R] |

---

## 1. Time and scheduling

| id | Sev | Where | Defect | Evidence | Breaks |
| --- | --- | --- | --- | --- | --- |
| T-1 | High | `include/abstractx/hal/async_driver.hpp:77-91,197-205`; `targets/allwinner_e906/src/platform.cpp:113-125`; `targets/linux/src/hal_timer.cpp:53-77`; `targets/pico2w_rp2350/include/abstractx_pico.hpp:129-147` | Every `ITimer` sleep goes through `AsyncDriverBase`, which runs one request at a time. The next sleep starts counting when the previous one ends; `req.target_time_us` is computed and never used. | [V] `t_timer A`: `three concurrent sleep_ms_async(10) woke at 10423, 20594, 30599 us` | [SPEC-HAL-05], [SPEC-ARCH-10] |
| T-2 | High | `include/asp_coro.hpp` `with_timeout` / `sleep_us_task` (~1229-1243) + T-1 | When the task wins, the timeout's sleep stays queued for its full length, delaying all later sleeps; with 16 queued, `submit_request` fails and every later `with_timeout` reports a timeout immediately. | [V] `t_timer B`: `sleep_ms_async(1) took 101609 us`; `t_wt`: `task won 17, timeout won 23 (Error 23), elapsed 186 us` | [SPEC-ARCH-11], [SPEC-HAL-05] |
| T-3 | High | `include/asp_coro.hpp:281,445` (`~Task`); `timer.hpp` `SleepAwaiter` (no destructor); `domain_dispatcher.hpp:103` | Destroying a suspended task frees its frame while a driver request still holds its handle and `result_slot`; the completion writes into freed memory and resumes whatever coroutine reused the frame. | [V] `t_timer C`: `after 30 ms (event never set): b.done=1 ran_past=1` (B ran past an Event nobody set) | AGENTS §10.3, [SPEC-ARCH-10] |
| T-4 | Med | `include/abstractx/domain_dispatcher.hpp:57,73-78`; `Event::set` | Dispatcher capacity 32; a dropped resume is counted and the coroutine hangs forever (frame and spawn slot never freed). | [V] `t_timer E`: `40 waiters ... woken=32 done=32 dropped_resumes=8` | [SPEC-ARCH-10] every suspension has a waker |
| T-5 | Med | `async_driver.hpp:49-54`; `targets/linux/src/hal_timer.cpp:72` | Completions post but never call `wake()` / `wake_from_isr()`; [SPEC-ARCH-15] requires publish-then-wake. FreeRTOS E906 works only because its ISR handler wakes. | [R] | [SPEC-ARCH-15] |
| T-6 | Med | `targets/pico2w_rp2350/src/hal_platform.cpp:124-128` | Bare `wfe` with no masked re-check; `SmpInterruptLock::unlock` sends no `__sev()`; an alarm callback on core 0 can be slept through on core 1. | [I] (alarm-pool core not confirmed on hardware) | [SPEC-ARCH-06] idle policy |
| T-7 | Low | `targets/linux/src/hal_timer.cpp:55`; `targets/linux/src/hal_platform.cpp:42-44` | One detached `std::thread` per sleep (heap, can throw in `noexcept`); idle polls every 100 µs. Known: [SPEC-ARCH-15] "not covered yet" (Linux Asio host). | [R] | [SPEC-ARCH-15] |
| T-9 | Med | `hosts/freertos/src/freertos_host.cpp` (`if (next == kNow) continue;`) | While `step()` returns `kNow` the FreeRTOS host never blocks or yields: a coroutine that never waits starves lower-priority tasks and delays equal-priority ones to the next tick, with nothing to show it. Found by a dataAcq integration review after this review. **Fixed 2026-11-04**: `taskYIELD()` on `kNow`, and `longest_ready_run()` counts it ([SPEC-ARCH-15]); `sim/test_host_drive.cpp`. | [V] | [SPEC-ARCH-15] |
| T-8 | Low | `include/abstractx/interrupt_lock.hpp:31-36`; `targets/esp32p4` | ESP32-P4 is dual-core but builds with `ABSTRACTX_SMP_CORES=1`; latent while every P4 driver is a stub. | [R] | [SPEC-ARCH-01] |

Refuted: the dispatcher queue is named SPSC but every producer and the consumer go through
`InterruptLock`, so it is a locked MPSC queue and safe on every target checked (bare metal masks
interrupts, Pico uses the SMP lock, hosted uses a process-wide spinlock). The E906 64-bit timer
globals are only touched with interrupts masked.

## 2. Data paths: queues, awaiters, combinators, drivers

| id | Sev | Where | Defect | Evidence | Breaks |
| --- | --- | --- | --- | --- | --- |
| Q-1 | High | `targets/linux/include/i2c_worker.hpp:120-141,143-200` | Driver advertises `no_start` and 16 messages, but the worker handles only 1 message or write+read; other shapes issue no ioctl and still return `Ok`. A zero-length probe always "finds" the device. | [V] `i2c_nostart` (driver on `/dev/null`): `write_regs_async: done=1 status=0 transferred=0 msgs_done=2` | [SPEC-HAL-09], [SPEC-ARCH-09] |
| Q-2 | High | `include/asp_coro.hpp:868-876,898-906,992-1000` | On TX-ring-full or no free tag, `await_suspend` returns false but `await_resume` returns the stored response of an earlier transaction on that tag, with no error. | [V] `coro_bugs`: `A2 read with TX ring FULL: done=1 returned type=3 tag=3 payload=a5 00 00 03` | AGENTS §10.4, [SPEC-ARCH-09] |
| Q-3 | High | `include/abstractx/hal/io_engine.hpp:186-202` vs `347-363` | `on_trigger` checks `in_flight`/`pending` but not `busy`; a new edge overwrites `snap`, `ts_ns`, `cmd_len` while the completion still reads them, so a sample can carry the next edge's timestamp. | [V] TSan `ioengine_race`: `data race io_engine.hpp:199` | [SPEC-ARCH-13], AGENTS §10.3 |
| Q-4 | Med | `include/asp_coro.hpp:674-756` | `AsyncQueue`: single push/pop waiter slot, `PushAwaiter::await_resume` ignores `try_push`, no drop counter, plain `size_t` indices, "lock-free" comment. `apps/gps_imu_app` ignores `try_push` results (`gps_imu_tasks.hpp:203,218,233,246`). | [V] `coro_bugs`: `B consumers woken=1 got0=-1 got1=7 c0.done=0` | [SPEC-ARCH-10], [SPEC-TLP-03] |
| Q-5 | Med | `targets/linux/include/linux_spi_driver.hpp:52-58`; `linux_i2c_driver.hpp:146-153` | Requests go straight to the worker and `hardware_busy_` clears at once, so `cancel_pending` / `cancel_active` see nothing. | [V] `spi_cancel`: `cancel_pending()=0 cancel_active()=0 ... completed anyway: done=1 status=0` | [SPEC-HAL-07], [SPEC-ARCH-11] |
| Q-6 | Med | `include/asp_coro.hpp:1193,1197` | `when_any` detaches the loser (safe against use-after-free), but a loser that never completes keeps its frame forever. | [V] `coro_bugs`: `pool_used before=0 after 200 races=25600` | AGENTS §10.5 |
| Q-7 | Med | `asp_coro.hpp:850-1001`; `spi.hpp:313-381`; `i2c.hpp:283-389`; `uart_rx.hpp`; `asp_coro.hpp:679-692` | No awaiter except `Event`, `Semaphore`, `BusLockAwaiter`, `StreamBatchAwaiter` unregisters in its destructor; this is the general form of T-3. | [R] (T-3 shows the consequence) | AGENTS §10.3, [SPEC-HAL-07] |
| Q-8 | Med | `targets/linux/src/hal_uart.cpp:121-136` | `write_async` writes on the coroutine thread under the process-wide lock; a short write or `EAGAIN` is reported as `Ok`. | [R] | [SPEC-HAL-07], AGENTS §4 |
| Q-9 | Low | `async_driver.hpp:140-150` | After a successful abort, `cancel_active()` does not start the next queued request; the queue stalls until the next submit. | [R] | [SPEC-HAL-07] |
| Q-10 | Low | `async_driver.hpp:53`; `asp_coro.hpp:553,613,710,823`; `bus_lock.hpp` | Every waker ignores the `Dispatcher::push/post` result (see T-4). | [R] | [SPEC-ARCH-01] |
| Q-11 | Low | `targets/linux/src/io_processor.cpp:100` | The SPI worker opens one `/dev/spidev<bus>.<cs>` on first use; later transfers to another device go to the first chip select. | [R] | [SPEC-ARCH-13] |
| Q-12 | Low | `asp_coro.hpp:779` | Tag generation `% 254 + 1` never uses 255 and reuses tags sooner (feeds Q-2). | [R] | |

Checked and fine: no awaiter pops "the next" result from a shared queue; every failed submit returns
`false` from `await_suspend` (except the stale value in Q-2); no span points at a loop-local or a
temporary; no heap or forbidden headers in `include/` or `src/`.

## 3. Tests, claims and gates

| id | Sev | Where | Defect | Evidence | Suggested fix |
| --- | --- | --- | --- | --- | --- |
| G-1 | High | `tests/CMakeLists.txt:35`, `sim/CMakeLists.txt:10`, `tools/trac.py:111-115`, `host-tests` skill | ctest runs the binary already in the build dir; `trac.py --ctest` records a pass without building. | [V] syntax error added to a test: build `rc=2`, then `ctest -R sim_test_host_drive`: `100% tests passed` | `tools/gate.sh` (done today); also a ctest build fixture and `trac.py` builds first |
| G-2 | High | `docs/verification/evidence/2026-10/hp.jsonl:75`; `tools/spectrace_matrix.py:463-470,501-503` | A ledger record with commit `HEAD` (dirty) never goes stale; SPEC-STUDIO-01, SPEC-USER-PLUGIN-01/06 stay VERIFIED after 27 commits to their code. | [V] `--matrix` | reject non-hex or dirty commits |
| G-3 | High | `tools/spectrace_matrix.py:476-516` | VERIFIED needs a passing skill run but no `@verifies` test (SPEC-USER-PLUGIN-01/06, SPEC-ZYNQ-IMU-POC-07 have none). Only 8 of 238 requirements are VERIFIED (163 STALE). | [V] | require at least one ctest-registered `@verifies` test |
| G-4 | Med | `tests/test_zynq_buildroot_config.py:21,67,119,134` | `@impl` tags for ZYNQ-PLATFORM-07/08/10/11 are in the test, so editing the real defconfigs never makes them stale. | [V] | move tags to the defconfigs |
| G-5 | Med | `spectrace_matrix.py --check-verifies` | 68 unbacked `@verifies` claims, enforced by no test. | [V] `Unbacked @verifies claims: 68` | shrink-only baseline in `test_repo_is_honest` |
| G-6 | Med | `tests/test_ring_wraparound.cpp:1` | Claims SPEC-TLP-02 (virtual BAR addressing); tests only ring wraparound. | [R] | drop the claim |
| G-7 | Med | `tests/test_visualizer_async_bridge.py:5` | Claims SPEC-STUDIO-08/04 but tests an async engine Studio does not use. | [V] | test `udp_receiver_thread` or wire the engine in |
| G-8 | Med | `sim/sitl_coro_sim.cpp`; 9 `examples/*.cpp` | No asserts, always exit 0, yet `sim_sitl_coro_sim` claims SPEC-ARCH-01. | [V] | add asserts or call them smoke tests |
| G-9 | Med | `tests/test_live_profile.py:51`, `test_event_streams_decode.py:39`, `test_toolchain_files.py:86` | Skips print and return, so ctest shows Passed. | [V] `75: skip: E906 ELF ... not present`, Passed | exit 77 + `SKIP_RETURN_CODE 77` |
| G-10 | Med | `tests/CMakeLists.txt:123,151-160`; `.github/workflows/spectrace_dashboard.yml:28` | GUI tests registered only when `imgui_bundle` is importable; CI does not install it, so CI runs no Studio test; CI runs no cocotb. | [R] | install it in CI; missing dependency fatal under a CI option |
| G-11 | Med | `sim/CMakeLists.txt:139-163`; `spectrace_matrix.py:176-187` | None of the 17 cocotb suites is in ctest; `check_tests` counts a name anywhere in the CMake text as "run". | [R] | `add_test` per suite; `check_tests` requires `add_test` |
| G-12 | Low | `tests/test_e906_rpmsg_bridge.py:74` | One test function is never called from `__main__`. | [V] | auto-discover tests |
| G-13 | Low | `tests/test_visualizer_sdk.py:307-330` | Floating-canvas test asserts flags it just set. | [R] | drive Pop to Canvas / Pop In |
| G-14 | Med | `docs/DESIGN_SPECIFICATION.md:374`; `targets/SPECIFICATION.md:440` | Spec register map disagrees with code (`0x300` = GPS in spec, BARO/DSHOT in code; `GpsBase = 0x5000u`; `PCIE_BAR_SPI_BASE` does not exist). | [V] grep | one map + parity test (`hw-sw-register-parity`) |
| G-15 | Low | `targets/allwinner_e906/SPECIFICATION.md:209,214,218,263,266,268,438,649` | Spec names that are not in code (`s_ring_errors`, `s_rx_vq`, `s_rpmsg_coroutine_handle`, `kDmaCopyMinBytes`, ...). | [V] | rename or mark not implemented |
| G-16 | Low | `docs/DESIGN_SPECIFICATION.md:836,841,862,867`; `tools/visualizer/SPECIFICATION.md:344` | Implementation / Verified-by paths that do not exist (moved to `applib/`, `timeline_stream.py`). Two requirements name unknown skills (SPEC-ARCH-14, SPEC-TLP-05). | [V] | `--check-drift` checks paths |
| G-17 | High | `engineering_log.md` entry of `936d023` | "100% pass ... zero failures" while that commit broke two host tests (fixed in `db032f8`); "100%" includes a run with 35 of 39 skipped; the new I2C retry calls blocking `Timer::delay_us(200)`. | [V] | no log or commit claim without quoted output from the same commit |
| G-18 | Low | `engineering_log.md` entry of `33055cf` | Claims Pico fixes that landed in `167b03b`; "zero warnings" is false at HEAD (`-Wmismatched-new-delete` in two sim tests). | [V] | each entry cites its commit; `-DABSTRACTX_WERROR=ON` in CI |
| G-19 | Med | `tests/test_visualizer_sdk.py` | No GUI test clicks anything: tabs, Pop to Canvas / Pop In, View menu, connection menu, CLI Send, Restore Layout are never exercised. Dear ImGui Test Engine is importable (`imgui_bundle` 1.92.9). | [V] | click tests (next task) |

## 4. Automatic audit stages on this tree (`tools/full_audit.py`)

| Stage | Status | Result |
| --- | --- | --- |
| gate | FAIL | 89/90 in each build; the one failure is `py_test_spectrace_matrix`, caused only by the untracked `sim/cocotb/test_asp_pio_rtfc_cocotb.py` (passes with it moved aside) |
| trace | FAIL | 7/9: `--check-verifies` (68 unbacked, G-5) and `boundary_review.py --check` (`g_stop` in `apps/gps_imu_app/platforms/linux/e906_rpmsg_studio_bridge.cpp:73`, open since `af609a3`) |
| e906 | PASS | 5 images, layout ok; `e906_repl_app` SRAM_FAST 190.6 / 192.0 KB (99.3%) |
| hosts | REPORT | 2/5: FreeRTOS, bare metal; missing Zephyr, ChibiOS, Linux (Boost.Asio) |
| drivers | REPORT | 32 blocking vendor / OS calls in first-party code: `targets/allwinner_e906` 17, `targets/pico2w_rp2350` 9 (`i2c_*_blocking`, `uart_getc`, `uart_putc_raw`, `sleep_*`), `targets/linux` 4, apps 2. Each needs a verdict: boot-only (mark it) or replace |
| mutation | SKIPPED | Mull not installed |
| fuzz | SKIPPED | no fuzz targets yet |
| ci | SKIPPED | `gh` not logged in on this PC; CI has likely been red since `936d023` and nobody saw it |
| coverage, static | not run in this pass | |

## 5. Status of the 2026-10-22 review (§3 buses, §5 coroutines)

- **Fixed:** `YieldAwaiter` posts; hand-clocked delay helpers and `TimerService` removed from core;
  `when_all` / `when_any` post their wakes and detach the loser; frame pool exhaustion traps;
  `Event` / `Semaphore` wait lists and permit hand-off; `CoroutineIoEngine` inline resume and tag
  leak; bare `uint8_t` register reads replaced by status-returning reads; `step()` /
  `service_task()` duplication documented.
- **Partly fixed:** five completion paths (callback, handle, result slot, output queue remain);
  cancellation exists but cannot reach in-flight Linux requests (Q-5) or destroyed frames (Q-7);
  E906 timer list exists but the `ITimer` adapter still runs one sleep at a time (T-1).
- **Still open:** base class does not own the in-flight request (Q-5); transfers started under
  `InterruptGuard`; synchronous methods still pure virtual in `i2c.hpp:511-519` and UART;
  `AsyncQueue` (Q-4); dispatcher drops at 32 (T-4); destroying a suspended task (T-3); stale TLP
  response (Q-2); tag 255 (Q-12); Pico bare `wfe` (T-6).

## 6. Proposed fix order

1. **Timers (T-1, T-2):** `ITimer` sleeps use the deadline list directly (absolute `target_time_us`,
   any number in flight), and a cancelled sleep leaves the list; this also removes T-2.
2. **Lifetime (T-3, Q-7, Q-6, Q-5):** every awaiter that registers a handle or result slot unlinks
   or cancels in its destructor; drivers own their in-flight request so cancel reaches it.
3. **Silent success (Q-1, Q-2, Q-8):** every unsupported shape or failed submit returns an error.
4. **Races and capacity (Q-3, T-4, Q-4, T-5):** busy check in `IoEngine`; dispatcher capacity
   policy (sized from tasks + waiters, or a never-drop path); `AsyncQueue` waiter lists and
   counters; completions call `wake()`.
5. **Gates (G-1 … G-11, G-17):** build fixture for ctest, ledger and VERIFIED rules, skip exit codes,
   CI installs and runs GUI and cocotb tests, `--check-verifies` baseline.
6. **Spec drift (G-14 … G-16):** one register map with a parity test; spec names and paths checked.

Each fix: spec first, then a regression test that fails on the old code (the probes below are the
starting point), then the code.

## Re-running the probes

From the repository root, after `cmake --build build_host`:

```bash
P=docs/reviews/2026-11-04-probes
for f in t_timer t_wt coro_bugs i2c_nostart spi_cancel; do
  g++ -std=c++20 -O1 -Iinclude -Itargets/linux/include -Ithird_party/etl/include $P/$f.cpp \
      -Wl,--start-group build_host/libabstractx_core.a build_host/targets/linux/libabstractx_target_linux.a \
      -Wl,--end-group -lpthread -o /tmp/$f
done
/tmp/t_timer A; /tmp/t_timer B; /tmp/t_timer C; /tmp/t_timer E; /tmp/t_wt
/tmp/coro_bugs; /tmp/i2c_nostart; /tmp/spi_cancel
# ioengine_race needs TSan: same command with -fsanitize=thread against build_tsan, run under setarch -R
```

## Not checked

- E906, Pico, ESP32-P4 and Zynq driver awaiters (only Linux drivers were exercised); no board runs,
  so T-1 is measured on Linux only and T-6 is inferred.
- `applib/` drivers; RTL and cocotb; the FreeRTOS kernel and Pico SDK alarm-pool core placement.
- Coverage and static analysis in this pass (stages exist; not run here).
- All 68 unbacked `@verifies` claims (about 10 sampled); engineering log entries before 2026-10-30.
- The OS comparison (FreeRTOS, Zephyr, ChibiOS, Linux) and the bus API efficiency review: next steps.
