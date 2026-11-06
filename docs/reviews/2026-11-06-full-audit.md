# Full audit, 2026-11-06 (processor side)

The audit ran at commit `ecb9be9`, with `tools/full_audit.py`, every automatic stage. The full report is in `build_audit/audit-2026-11-06.md`, which is not tracked. One finding was fixed after the run, in `3599c1d`.

No hardware was used. Every board claim below is either "not run" or quotes an older recorded run with its date.

## 1. Automatic stages

| Stage | Status | Summary |
| --- | --- | --- |
| Gate (plain, ASan/UBSan, TSan, boundary review, static analysis) | PASS | plain 121/121, ASan 110/110, TSan 110/110 |
| Traceability and contract audits | **FAIL** | 8/9 pass; `audit_specs.py` fails (see below) |
| Static analysis | PASS | clang-tidy finds 0 new issues; cppcheck reports 5 (advisory) |
| Coverage, host suite | REPORT | lines 85.3%, functions 85.0%, branches 66.2% |
| Mutation testing | SKIPPED | Mull is not installed |
| Fuzzing | SKIPPED | no fuzz targets yet |
| E906 cross-build, layout, memory | PASS | 5 images |
| CI status | SKIPPED | `gh` is not logged in |
| OS hosts | REPORT | 2 of 5 hosts exist; missing: Zephyr, ChibiOS, Linux (Boost.Asio) |
| Blocking vendor and OS calls | REPORT | 30 calls: E906 17, Pico 9, Linux 1, Zynq 1, apps 2 |

**Why `audit_specs.py` fails.** It now scans every specification (P0-07, `5547e0a`). 26 requirements have no `@impl` tag and are not declared "Not implemented yet". All 26 are FPGA board specs:

- Tang Nano 20K link: `A5E-LINK-01` and `-03`, `TN20K-01` and `-04`
- Zynq platform: `ZYNQ-PLATFORM-*`, 10 requirements
- Alinx AC7020C board: `AC7020C-*`, 9 requirements
- the retired IMU proof-of-concept: `ZYNQ-IMU-POC-05`, `-06` and `-08`

They were not triaged because FPGA work is paused.

**Completion figure.** `tools/spectrace_matrix.py --matrix` reports 59 of 244 requirements verified (24.2%). Host evidence was recorded at `01a4070` in `ecb9be9`. The other verdicts:

| Verdict | Count | Meaning |
| --- | ---: | --- |
| STALE | 71 | mostly need board or RTL records |
| IMPLEMENTED | 78 | no `@verifies` test |
| SPEC-ONLY | 27 | no code yet |
| PARTIAL | 6 | partly implemented |
| FAILING | 3 | board records from 2026-10: `E906-REPL-14`, `GPS-01`, `STUDIO-08` |

## 2. Manual checklists

### review: independent review

An independent read-only reviewer answered the "buses" checklist. It was given the code and the requirements, not my conclusions. Its findings are in section 3.

The previous review's open items are tracked in `engineering_log.md` (EL-1027 table):

- **Closed or partly closed today:** -16, -19, -21, -22, -24, -26, -27, -28, -33.
- **Still open:**
  - -25: needs the board
  - -31: heap, deferred by the owner
  - -34: INAV repo, the owner's decision
  - -47: legal
  - -48: FPGA fit

### concurrency

- **Boundary review** (now part of the gate) lists 30 shared variables. None lacks an ownership note, and no wake handshake is missing its fence.
- **TSan:** 110/110 under `setarch -R`. Scheduling is not randomised beyond what the tests do themselves.
- **Model checking** of the wake protocols (TLA+ or similar): not set up.
- **New shared state today:** `UartRxRingBase::claimed_` (atomic) and the E906 engine's `I2cJob::in_use` (atomic, claim/release). Both carry ownership notes.

### buses

See section 3.

### gui

Answered 2026-11-06 (after the run): `tests/test_studio_gui_clicks.py` clicks through the real widgets
with the Dear ImGui Test Engine (tabs, Pop to Canvas / Pop In, Bring to Front, View menu, Show TLP / Show
Log, layout presets, Connection menu). It found two product bugs (Show TLP / Show Log crashed Studio;
presets and jumps changed copies of the windows), fixed with [SPEC-STUDIO-16] "real windows".

### board

- No board runs this session.
- These changes need a board run before anyone can claim they work on hardware:
  - E906 UART `read_async` with a deadline
  - SPI1 with CS0 only
  - the E906 engine's async I2C
  - Zynq UIO lock and word order
  - the Pico port table (same pin values as before)
  - the little-endian SPI link to an FPGA
- The 3 FAILING board records (2026-10) are unchanged.

### platforms

Not answered this session. `hosts/` has FreeRTOS and bare metal; Zephyr, ChibiOS and Linux (Boost.Asio) hosts do not exist.

## 3. Buses review: findings and status

These are the reviewer's findings. They count as confirmed only once I checked them in code. Ranked as the reviewer ranked them:

| # | Finding | Status |
| --- | --- | --- |
| 1 | E906 engine ran I2C synchronously from the DRDY edge interrupt (20 ms wait, interrupts masked) | **Confirmed, fixed** in `3599c1d`; [SPEC-E906-HAL-04] rule 9 and audit check 6 (the old file gives 3 violations) |
| 2 | E906 `Spi1::AsyncTransferAwaiter` busy-polls up to 10 ms for ≤64 B (`spi.cpp:186-189`, `360-363`) | Accepted as is (owner, 2026-11-06) |
| 3 | E906 UART interrupt handler spins up to 1024×32+64 `nop`s waiting for RFL | Known as EL-1027-25; needs a board measurement |
| 4 | Pico I2C async ignores `timeout_us` ([SPEC-HAL-09]); a stuck bus never completes | **Confirmed, fixed** 2026-11-06 ([SPEC-PICO-HAL-01] I2C deadline); cross-build only |
| 5 | Pico UART `write_byte` ignores a full TX queue; async TX reports the full size anyway; no `rx_ring` | **Confirmed, send side fixed** 2026-11-06 ([SPEC-PICO-HAL-01] UART send queue); no `rx_ring` still open |
| 6 | Linux UART has no RX completion source (`read_async` is NotSupported) | **Confirmed, fixed** 2026-11-06 ([SPEC-LINUX-HAL-03], `sim/test_linux_uart_rx.cpp`) |
| 7 | Linux `IGpio` edge handlers are never dispatched (`dispatch_interrupt_events` has no caller) | **Confirmed, fixed** 2026-11-06 ([SPEC-LINUX-GPIO-02], `sim/test_linux_gpio_edges.cpp`) |
| 8 | Linux SPI worker / I2C driver invent data and report success when the device did not open (`spi_worker.hpp:159-200`, `linux_i2c_driver.hpp:73-77`) | **Confirmed, fixed** in the commit after `afe0d4f`: `BusError` unless `ABSTRACTX_SIM_BUS=1` ([SPEC-LINUX-HAL-01]); `sim/test_linux_bus_no_sim.cpp` |
| 9 | E906 `serial_xfer` re-inits the UART on every baud TLP and can wait up to 10 ms on a full TX ring | **Confirmed, fixed** 2026-11-06 ([SPEC-E906-HAL-04] rule 10; `queue_tx`) |
| 10 | Pico `start_dma_transfer_raw` returns without completing when no DMA channel is claimed | **Confirmed, fixed** in `d87e100` |

The reviewer also noted:

- **Zynq and ESP32-P4 have no SPI, I2C, UART or GPIO HAL**, so a portable app does not link there.
- **The same call behaves differently per target.** UART `read_async` works only on the E906. `IGpio::configure_interrupt` is refused on the E906, works on the Pico, and is silent on Linux.
- **CYW43 on the Pico stays on Core 0.** Its bus DMA wait was not inspected.

## 4. Not checked

- No hardware: E906, Pico 2 W, Zynq, Tang Nano 20K.
- No ESP32-P4 build: ESP-IDF is not installed.
- No Zynq cross-build with the ARM Linux toolchain; the native host build only.
- No synthesis or FPGA fit.
- No mutation testing, no fuzzing, no CI status.
- Not answered: the GUI and platforms checklists.
- Not re-checked: candidates 2, 4-7, 9 and 10.
