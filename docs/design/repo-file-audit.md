# Repository File Audit (2026-10-23)

Research only: no file was moved, deleted or edited. Scope is `git ls-files` (595 tracked
paths at `f0360c5`, plus the working-tree changes present at audit time). "User" means a
file that includes, imports, builds, links, tests or links to (in docs) the path.
Method: `#include` / import grep, CMake `add_executable`/`add_library`/`add_test` lists,
`sim/cocotb/Makefile`, `hw/Makefile`, `hw/common/gowin_synth.tcl`, basename grep across all
tracked text. Anything not proven either way is marked **unsure**.

## Summary

* The generic core (`include/abstractx/`) carries **device drivers and flight fusion**
  (`drivers/gps/ublox_gps.hpp`, `drivers/imu/icm42688p.hpp`, `drivers/mag/qmc5883l.hpp`,
  `fusion/attitude_filter.hpp`) and **flight-specific trace payloads**
  (`ImuSamplePayload`, `GpsFixPayload` in `trace/tracer.hpp`). These are what the "gps.h in
  the core" observation is about. There is no file literally named `gps.h`.
* Dead headers no translation unit includes: `include/asp_spi_protocol.h`,
  `include/mpsc_isr_queue.hpp`, `include/abstractx/hal/remoteproc.hpp`,
  `include/abstractx/trace/coro_trace.hpp`, `targets/allwinner_e906/include/{ipc_protocol.hpp,
  hal/spsc_queue.hpp, shm_ping_protocol.h, dram_spsc_protocol.h}`, and two committed
  `generated_io_config.hpp` files.
* Unbuilt or unrun: `rtl/asp_axi_dma.sv`, `rtl/asp_ila_trace.sv` (no top instantiates them,
  no testbench), `sim/test_coro_combinators.cpp` (never in CMake; the only test of
  `when_all`/`when_any`), `sim/test_asp_top_tb.sv`, `sim/cocotb/test_asp_top_cocotb.py`
  (known failing, superseded), `targets/allwinner_e906/bsp/e906_sram.ld`,
  `cmake/toolchain-e906.cmake`, `cmake/toolchain-pico2w.cmake`,
  `cmake/toolchains/xtensa-esp-elf.cmake`.
* Exact duplicates / stale copies: `apps/e906_repl_app/testresults.md` is byte-identical to
  `TEST_RESULTS.md`; `docs/verification/ENGINEERING_LOG.md` is a stale copy of the first
  ~230 lines of the root `engineering_log.md`.
* Generated output committed: `tools/visualizer/pr_comment.md` (written by
  `tools/track_quality_trends.py:577`), `targets/{linux,pico2w_rp2350}/include/generated_io_config.hpp`,
  `docs/media/slide_*.png` + `.webp` (outputs of `tools/render_end_to_end_walkthrough.py`).
  `docs/verification/TRACEABILITY_MATRIX.md` and `*/GUIDE.md` are generated on purpose; keep.
* AI prompt leftovers: 5 Gemini prompt files (`docs/prompts/*`, `docs/tier1_vision/GEMINI_PRO_*`)
  are linked from nowhere; `TODO.md:116` already plans to move them.
* Local disk only (not tracked, all ignored): 9 `build*/` dirs (~580 MB) and
  `.claude/worktrees/` (~940 MB, 4 worktrees whose branches are all merged into `main`
  with no local changes). Stray `*.ini` Studio/ImGui layout files in the root are covered
  by `.gitignore:73 *.ini`.
* There is **no** `GEMINI_PRO_A7A_USB_AUDIT_BUNDLE.md` (or any audit bundle) at the top level
  of this repository. The only bundle generator is `tools/build_gemini_context.py`, whose
  output `abstractx_context_for_gemini.txt` is gitignored.

### Traceability impact of removals

`tools/audit_specs.py` counts `@impl` tags. Removing these files drops the **only** `@impl`
for a requirement, so the spec must be changed in the same commit:

| File | Sole implementer of |
| --- | --- |
| `rtl/asp_ila_trace.sv` | `[SPEC-ILA-01]` (docs/DESIGN_SPECIFICATION.md:674) |
| `targets/allwinner_e906/include/dram_spsc_protocol.h` | `[SPEC-E906-MEM-02]` (targets/allwinner_e906/SPECIFICATION.md:125) |
| `include/abstractx/fusion/attitude_filter.hpp` | `[SPEC-APP-04..06]` (move, do not delete; update the path in the tag) |

Files with `@impl` tags that are *not* sole implementers: `rtl/asp_axi_dma.sv`
(`SPEC-ZYNQ-01/02`), `targets/allwinner_e906/bsp/e906_sram.ld` (`SPEC-E906-MEM-01`),
`include/abstractx/trace/coro_trace.hpp` (`SPEC-STUDIO-02`),
`tools/check_cubie_a5e_peripherals.sh` and `apps/e906_repl_app/test_sensors.py`
(`SPEC-GPS-01`, `SPEC-E906-ARCH-02`). Moving any tagged file also changes the path that
`docs/verification/TRACEABILITY_MATRIX.md` lists (regenerate with
`python3 tools/spectrace_matrix.py --matrix --write`). `docs/DESIGN_SPECIFICATION.md:35,108`
names `include/mpsc_isr_queue.hpp` as an Implementation Target although no code uses it
(spec/code drift; fix the spec when the header goes).

---

## Top-level files

| Path | Verdict | Evidence |
| --- | --- | --- |
| `AGENTS.md`, `CLAUDE.md`, `GEMINI.md`, `.github/copilot-instructions.md` | keep | agent entry points; `CLAUDE.md` includes `@AGENTS.md`; `AGENTS.md` lists the others |
| `README.md`, `LICENSE`, `TODO.md`, `engineering_log.md` | keep | `engineering_log.md` written by `tools/log_mistake.py:34`, read by `tools/trac.py:242` |
| `CMakeLists.txt`, `CMakePresets.json` | keep | root build. Note: preset `e907` is a legacy alias (`CMakeLists.txt` prints a deprecation); no preset for `esp32p4` or `zynq7000` |
| `pytest.ini` | keep | pytest config (`testpaths = tests`) |
| `.gitignore`, `.gitmodules` | keep | see `.gitignore` notes below |
| `.claude/skills` (symlink) | keep | `AGENTS.md` "`.claude/skills` -> `.agents/skills`" |
| `.vscode/{launch,settings,tasks}.json` | keep | `targets/pico2w_rp2350/HOWTO.md` refers to the tasks; personal paths hard-coded (`/home/tcmichals/.tools/...`) |
| `.github/workflows/spectrace_dashboard.yml` | keep | CI |
| `.github/prompts/spectrace-review.prompt.md` | keep | Copilot `/spectrace-review` slash command (`.agents/rules/spectrace-review.md` §2) |
| `.agents/rules/*.md` (6), `.agents/skills/*/SKILL.md` (16) | keep | included by `CLAUDE.md`; skills named in `Verified by` lines |
| `trace/barectf_config.yaml` | keep | `tools/visualizer/ctf_schema_loader.py:257` |
| `src/runtime.cpp` | keep | `CMakeLists.txt` `add_library(abstractx_core STATIC src/runtime.cpp)` |

Untracked / ignored root items seen at audit time: `AbstractX_Studio___User_Domain_Workbench.ini`,
`Pytest_Studio___*.ini` (5), `imgui.ini`, `studio_capture.ini` (all ignored by `.gitignore:73`),
`.pytest_cache/` (ignored), `docs/design/micropython-trace-and-debug.md` and the new
`include/abstractx/hal/bus_*.hpp` + `sim/test_bus_protocols.cpp` (untracked work in
progress, out of scope).

### `.gitignore` coverage

| Item | Covered | By |
| --- | --- | --- |
| `build/`, `build_asan`, `build_e906`, `build_e906_debug`, `build_e906_irq`, `build_e906_lto`, `build_host`, `build_pico2w`, `build_tsan` | yes | `build/`, `build*/` (lines 2-3) |
| `.claude/worktrees/`, `.claude/scheduled_tasks.lock` | yes, **locally only** | `.git/info/exclude:8,11` (not in the shared `.gitignore`; another clone would see them) |
| Studio `*.ini` layouts | yes | `*.ini` / `!pytest.ini` |
| `*.dtbo` built from overlays | yes | `.gitignore:58` |
| `tools/visualizer/pr_comment.md` | **no** | generated each run by `tools/track_quality_trends.py:577`; tracked |
| Stale lines `sim/test_*` and `examples/*` binary names (lines 25-50) | harmless | in-source build leftovers; out-of-source builds make them unnecessary |

---

## `include/` (core API)

| Path | Verdict | Evidence |
| --- | --- | --- |
| `include/abstractx/abstractx.hpp` | keep | `apps/e906_bench_app/src/main.cpp:8` |
| `include/abstractx/coro.hpp` | keep | `examples/dual_processor_io_and_processing.cpp:24`; it is only a wrapper that includes `../asp_coro.hpp` (`coro.hpp:24`) |
| `include/abstractx/domain_dispatcher.hpp` | keep | `apps/e906_bench/bench.cpp:12` |
| `include/abstractx/isr_dispatcher.hpp` | keep (shim) | `include/abstractx/coro.hpp:25`; 4-line alias for `domain_dispatcher.hpp`, candidate to fold in |
| `include/abstractx/interrupt_lock.hpp` | keep | `apps/e906_repl_app/src/e906_periph.cpp:14` |
| `include/abstractx/platform_topology.hpp` | keep | `include/abstractx/abstractx.hpp:27` |
| `include/abstractx/timer.hpp` | keep | `include/abstractx/coro.hpp:26` |
| `include/abstractx/hal/async_driver.hpp` | keep | `include/abstractx/hal/i2c.hpp:19` |
| `include/abstractx/hal/gpio.hpp` | keep | `include/abstractx/hal/platform.hpp:23` |
| `include/abstractx/hal/i2c.hpp`, `spi.hpp`, `uart.hpp`, `uart_rx.hpp`, `uart_status.hpp`, `io_status.hpp`, `timer.hpp`, `platform.hpp`, `io_processor.hpp` | keep | e.g. `include/abstractx/drivers/imu/icm42688p.hpp:22` (spi), `include/abstractx/hal/uart.hpp:20-21`, `include/abstractx/abstractx.hpp:22,26` |
| `include/abstractx/hal/mailbox.hpp` | keep | only `targets/esp32p4/src/hal_mailbox.cpp:8` (ESP-IDF build only, never built here) |
| `include/abstractx/hal/remoteproc.hpp` | **remove** | no `#include` anywhere; no symbol used; no `@impl` |
| `include/abstractx/trace/tracer.hpp` | keep, **split** | `include/abstractx/abstractx.hpp:23`. Contains flight payloads `ImuSamplePayload` (line 91), `GpsFixPayload` (105) and their emitters (371, 406): move these to the app / CTF schema layer |
| `include/abstractx/trace/sink.hpp`, `trace_dispatcher.hpp`, `runtime_stats.hpp`, `event_trace.hpp` | keep | `abstractx.hpp:24-25`, `domain_dispatcher.hpp:30`, `sim/test_event_trace.cpp:16` |
| `include/abstractx/trace/coro_trace.hpp` | remove or wire up (unsure) | no `#include`; only named in comments in `tools/visualizer/abstractx_studio.py:335`; carries `@impl [SPEC-STUDIO-02]` (also implemented elsewhere) |
| `include/abstractx/drivers/gps/ublox_gps.hpp` | **move** to a driver library (e.g. `drivers/gps/` with its own CMake INTERFACE target) | users: `apps/gps_imu_app/include/gps_imu_tasks.hpp:16`, `examples/gps_imu_flight_node.cpp:16`, `tests/test_sitl_gps.cpp:14`, `sim/test_tlp_msg.cpp:5` |
| `include/abstractx/drivers/imu/icm42688p.hpp` | **move** (same) | `apps/gps_imu_app/include/gps_imu_tasks.hpp:15`, `tests/test_sitl_imu.cpp:14`, `sim/test_tlp_msg.cpp:4` |
| `include/abstractx/drivers/mag/qmc5883l.hpp` | **move** (same) | `apps/gps_imu_app/include/gps_imu_tasks.hpp:17`; no test of its own |
| `include/abstractx/fusion/attitude_filter.hpp` | **move to `apps/gps_imu_app/include/`** | only users `apps/gps_imu_app/include/gps_imu_tasks.hpp:18`, `tests/test_sitl_fusion.cpp:14`; it includes all three driver headers (lines 9-11), so the "generic" core depends on specific sensors |
| `include/asp_coro.hpp` | keep (relocate later) | 23 includers, e.g. `apps/e906_bench/bench.hpp:11`. The real coroutine core lives outside `include/abstractx/`; `abstractx/coro.hpp` wraps it |
| `include/asp_tlp64.h`, `include/asp_tlp64.hpp` | keep | `asp_tlp64.hpp:11` includes the `.h`; `.hpp` has 26 includers; `.h` also read by `tests/test_board_suite_local.py:42` |
| `include/asp_tlp_msg.hpp` | keep | `sim/test_baremetal_isr_spsc.cpp:18` |
| `include/spsc_tlp_ring.hpp` | keep | `apps/e906_repl_app/src/e906_periph.cpp:19` |
| `include/pcie_bar_map.hpp`, `include/pcie_reg_api.h` | keep (unsure naming) | `sim/sitl_coro_sim.cpp:19`, `targets/allwinner_e906/src/io_processor.cpp:25`; `pcie_bar_map.hpp:11` includes `pcie_reg_api.h`. "PCIe" naming is legacy for the TLP BAR map |
| `include/asp_spi_protocol.h` | **remove** | no includer, no `@impl`, no doc reference |
| `include/mpsc_isr_queue.hpp` | remove (after spec fix) | no includer; `MpscIsrQueue` is used nowhere in code; still named as Implementation Target in `docs/DESIGN_SPECIFICATION.md:35,108` |

## `src/`

| Path | Verdict | Evidence |
| --- | --- | --- |
| `src/runtime.cpp` | keep | `CMakeLists.txt` `abstractx_core` |

## `targets/`

| Path | Verdict | Evidence |
| --- | --- | --- |
| `targets/CMakeLists.txt`, `targets/SPECIFICATION.md` | keep | root `CMakeLists.txt` `add_subdirectory(targets)` |
| `targets/allwinner_e906/CMakeLists.txt`, `SPECIFICATION.md`, `GUIDE.md` | keep | `GUIDE.md` generated from the spec by `tools/spec_to_doc.py` |
| `targets/allwinner_e906/bsp/startup.S`, `resource_table.c`, `e906_ddr.ld` | keep | `targets/allwinner_e906/CMakeLists.txt` lines 1-3, `E906_LDSCRIPT` (line 37) |
| `targets/allwinner_e906/bsp/e906_sram.ld` | unsure (likely remove) | no CMake user (`E906_LDSCRIPT` defaults to `e906_ddr.ld`); has `@impl [SPEC-E906-MEM-01]` (shared). `apps/gps_imu_app/platforms/allwinner_e906/HOWTO.md:87` names a non-existent `bsp/e906.ld` |
| `targets/allwinner_e906/src/*.cpp` (17 files) | keep | all listed in `targets/allwinner_e906/CMakeLists.txt` `add_library(abstractx_target_e906 ...)`; `rpmsg.cpp`/`rpmsg_endpoints.cpp` also in `sim/CMakeLists.txt:29-41` |
| `targets/allwinner_e906/include/hal/{ccu,clic,crash,dma,i2c,msgbox,pio,pmp,rpmsg,rpmsg_endpoints,spi,timer,trace,uart}.hpp` | keep | e.g. `apps/e906_repl_app/src/e906_periph.cpp:15-18`, `targets/allwinner_e906/src/io_processor.cpp:19`, `sim/test_e906_rpmsg_endpoints.cpp:16` |
| `targets/allwinner_e906/include/memory_map.h`, `resource_table.h` | keep | `apps/e906_repl_app/src/main.cpp:12`, `sim/test_e906_rpmsg_endpoints.cpp:17` |
| `targets/allwinner_e906/include/hal/spsc_queue.hpp` | **remove** | no includer; `hal::` SPSC queue superseded by `include/spsc_tlp_ring.hpp` |
| `targets/allwinner_e906/include/ipc_protocol.hpp` | **remove** | no includer; `fc::ipc` namespace from the earlier flight-controller design |
| `targets/allwinner_e906/include/shm_ping_protocol.h` | remove or move to the cubie-a5e repo (unsure) | no includer in this repo; `TODO.md:142` says it must be resynced with the cubie-a5e protocol, so it is a copy of a contract owned elsewhere |
| `targets/allwinner_e906/include/dram_spsc_protocol.h` | unsure | no includer; sole `@impl [SPEC-E906-MEM-02]`; `TODO.md:105` already flags these headers as stale |
| `targets/esp32p4/CMakeLists.txt`, `SPECIFICATION.md`, `idf_component.yml`, `src/hal_{timer,spi,mailbox}.cpp` | keep (unbuilt) | built only under ESP-IDF (`if(DEFINED IDF_TARGET)`); no preset, no IDF project, no `hal_platform`/`io_processor`: the target is incomplete and never compiled here |
| `targets/esp32p4/io_processor.yaml`, `targets/linux/io_processor.yaml`, `targets/pico2w_rp2350/io_processor.yaml` | unsure | input to `tools/generate_io_config.py`, which no build step runs (only `README.md:878-898` describes it) |
| `targets/linux/include/generated_io_config.hpp`, `targets/pico2w_rp2350/include/generated_io_config.hpp` | **remove** or wire generation into CMake | generated output (`tools/generate_io_config.py:76`), committed, included by nothing |
| `targets/linux/CMakeLists.txt`, `SPECIFICATION.md`, `src/*.cpp` (7) | keep | `targets/linux/CMakeLists.txt` `abstractx_target_linux`; host tests link it (`sim/CMakeLists.txt:5`) |
| `targets/linux/include/{epoll_reactor,eventfd,gpio_gpiod,i2c_worker,spi_worker,linux_i2c_driver,linux_spi_driver}.hpp` | keep | `sim/test_tlp_msg.cpp:8-12`, `sim/test_linux_hal_completion.cpp:18-19` |
| `targets/pico2w_rp2350/CMakeLists.txt`, `SPECIFICATION.md`, `HOWTO.md`, `pico_sdk_import.cmake`, `src/*.cpp` (7) | keep | root `CMakeLists.txt` includes `pico_sdk_import.cmake`; sources in target CMake |
| `targets/pico2w_rp2350/include/abstractx_pico.hpp`, `hal/pico_trace_transport.hpp` | keep | `targets/pico2w_rp2350/src/hal_gpio.cpp:10`, `sim/test_pico_zero_copy_trace.cpp:9` |
| `targets/pico2w_rp2350/include/lwipopts.h` | keep | included implicitly by the Pico SDK lwIP (`pico_cyw43_arch_lwip_poll`) |
| `targets/zynq7000/CMakeLists.txt`, `src/{hal_platform,io_processor}.cpp`, `include/abstractx/target/zynq_uio.hpp` | keep (unbuilt) | `targets/CMakeLists.txt:12`; `zynq_uio.hpp` used by `targets/zynq7000/src/io_processor.cpp:17`; no preset, not in AGENTS.md §6 target list |

## `apps/`

| Path | Verdict | Evidence |
| --- | --- | --- |
| `apps/CMakeLists.txt`, `apps/README.md` | keep | root `CMakeLists.txt` `add_subdirectory(apps)` |
| `apps/e906_bench/{SPECIFICATION.md,bench.cpp,bench.hpp}` | keep | shared source of `apps/e906_bench_app/CMakeLists.txt:7` and `apps/e906_freertos_app/CMakeLists.txt` |
| `apps/e906_bench_app/{CMakeLists.txt,src/main.cpp}` | keep | `apps/CMakeLists.txt:9` |
| `apps/e906_freertos_app/*` (5 files) | keep | `apps/CMakeLists.txt:10`; needs the `third_party/FreeRTOS-Kernel` submodule, which is **not initialised** in this checkout, so it is skipped with a warning; `FreeRTOSConfig.h` is included implicitly by FreeRTOS |
| `apps/e906_repl_app/{CMakeLists.txt,SPECIFICATION.md,README.md,GUIDE.md}` | keep | `apps/CMakeLists.txt:8`; `GUIDE.md` generated by `tools/spec_to_doc.py:16` |
| `apps/e906_repl_app/include/{mpconfigport.h,mphalport.h,qstrdefsport.h,rpmsg_console.hpp}`, `src/*` (5) | keep | `apps/e906_repl_app/CMakeLists.txt` `REPL_APP_SOURCES`; port headers are pulled in by MicroPython `py.cmake` |
| `apps/e906_repl_app/{flight_sensors.py,raw_sensor_streamer.py,run_e906_script.py,sensor_contract.py,compass_gps_vector.py}` | keep | `.agents/skills/e906-board-selftest/SKILL.md:24-26`, `apps/e906_repl_app/GUIDE.md:80-84` |
| `apps/e906_repl_app/linux/e906_workbench.py` | keep | `apps/e906_repl_app/README.md:656` |
| `apps/e906_repl_app/cubie-a5e-flight-stack-{e906,linux}.dtso` | keep | `apps/e906_repl_app/README.md`, skill `e906-board-selftest` |
| `apps/e906_repl_app/TEST_RESULTS.md` | keep | written by `tools/test_e906_sensors_board.py:89` |
| `apps/e906_repl_app/testresults.md` | **remove** | byte-identical to `TEST_RESULTS.md` (`diff` empty); linked from nowhere |
| `apps/e906_repl_app/README_TESTS.md` | keep or merge into `README.md` (unsure) | linked from nowhere; content overlaps `README.md:668-690` |
| `apps/e906_repl_app/test_sensors.py` | move to `tools/board/` or remove (unsure) | Linux-side `/dev/ttyAS3` + `/dev/i2c-1` probe; only reference is its own `@impl` in `TRACEABILITY_MATRIX.md:52`; `tools/test_e906_sensors_board.py` is the maintained board test |
| `apps/e906_repl_app/engineering_log.md` | unsure (merge) | a third engineering log beside the root one and `docs/verification/ENGINEERING_LOG.md`; AGENTS.md §0 names only the root `engineering_log.md` |
| `apps/gps_imu_app/{CMakeLists.txt,README.md,SPECIFICATION.md,src/main.cpp,include/gps_imu_tasks.hpp,trace_schema.json}` | keep | `apps/CMakeLists.txt:4`; `trace_schema.json` whitelisted in `.gitignore:61` |
| `apps/gps_imu_app/platforms/allwinner_e906/{CMakeLists.txt,main.cpp,*.dtso,HOWTO.md}` | keep | `apps/gps_imu_app/CMakeLists.txt:50-51`. HOWTO has a broken `e906.ld` path (line 87) and `file:///` link |
| `apps/gps_imu_app/platforms/{esp32p4,linux,pico2w}/HOWTO.md` | keep | docs only |
| `apps/gps_imu_app/tools/flight_display.py` | keep | `.vscode/tasks.json:77` |
| `apps/serial_loopback_app/*` (11 files) | keep | `apps/CMakeLists.txt:7`; platform subdirs via `apps/serial_loopback_app/CMakeLists.txt:43-49` |

## `examples/`

| Path | Verdict | Evidence |
| --- | --- | --- |
| `examples/CMakeLists.txt` + all 16 `.cpp` | keep | each is `add_abstractx_example(...)` in `examples/CMakeLists.txt:9-24` and runs as a ctest. Eight are protothread teaching variants (`protothreads_*`, `pt_example_*`, `generic_protothread_replacements`): consolidation candidate, not dead |

## `rtl/`

| Path | Verdict | Evidence |
| --- | --- | --- |
| `rtl/asp_top.sv`, `asp_axis_fifo.sv`, `asp_router.sv`, `asp_sys_regs.sv`, `asp_wishbone_master.sv`, `spi/asp_spi_frontend.sv`, `spi/asp_spi_reg_bank.sv`, `imu/asp_imu_auto_dma.sv`, `motor/asp_dshot_core.sv`, `pwm/asp_pwm_decoder.sv`, `led/asp_neopixel_core.sv` | keep | read by `hw/common/gowin_synth.tcl:10-20`; testbenches in `sim/CMakeLists.txt:73-90` (`asp_pwm_decoder` has **no** cocotb test, which breaks AGENTS.md §11) |
| `rtl/asp_axi_lite_bridge.sv` | keep | `hw/zynq7000/qmtech_zynq7020/rtl/top_qmtech_zynq7020.sv`; `sim/CMakeLists.txt:83` |
| `rtl/asp_axi_dma.sv` | unsure (remove or add top + testbench) | instantiated by no top, not in `gowin_synth.tcl`, no testbench; `@impl SPEC-ZYNQ-01/02` (also implemented elsewhere) |
| `rtl/asp_ila_trace.sv` | unsure (remove or add top + testbench) | instantiated by no top, no testbench; sole `@impl [SPEC-ILA-01]` |

## `sim/`

| Path | Verdict | Evidence |
| --- | --- | --- |
| `sim/CMakeLists.txt` | keep | root `CMakeLists.txt` `add_subdirectory(sim)` |
| `sim/sitl_coro_sim.cpp` and the other 21 tracked `sim/test_*.cpp`, all registered with `add_abstractx_sim` / `add_executable` | keep | `sim/CMakeLists.txt:9-23,29-56,100-105` |
| `sim/fakes/e906_msgbox_fake.cpp` | keep | `sim/CMakeLists.txt:31` |
| `sim/test_coro_combinators.cpp` | **register in CMake** (or remove) | never in `sim/CMakeLists.txt` (`git log -S` finds no commit); last touched 2026-08-19 "pre-refactor snapshot". It is the only test of `when_all`/`when_any`, which `apps/gps_imu_app/include/gps_imu_tasks.hpp` uses |
| `sim/test_asp_top_tb.sv` | **remove** | no Makefile, CMake or script references it; cocotb replaced it |
| `sim/cocotb/Makefile`, `README.md`, `wb_util.py` | keep | `wb_util.py` imported by `test_asp_dshot_core_cocotb.py` |
| `sim/cocotb/asp_axis_tester.py`, `icm42688p_cocotb_vip.py` | keep | `test_asp_axis_fifo_cocotb.py:7`, `test_asp_tlp_64b_cocotb.py:25` |
| `sim/cocotb/test_*_cocotb.py` (13 registered) | keep | `sim/CMakeLists.txt:73-90` |
| `sim/cocotb/test_asp_top_cocotb.py` | remove | `sim/CMakeLists.txt:82` comment: fails, drives `spi_mosi` lost with Dual-SPI, superseded by `test_asp_tlp_64b_cocotb`. Still cited as "Verified by" in `hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md:216` (fix that line too) |
| `sim/cocotb/asp_e2e_vip.py` | remove with the above | only importer is `test_asp_top_cocotb.py:8`; documented in `docs/verification/{AXIS_TESTING_FRAMEWORK,E2E_VERIFICATION}.md` (update those) |

## `tests/`

| Path | Verdict | Evidence |
| --- | --- | --- |
| `tests/CMakeLists.txt`, `test_main.cpp`, `test_sitl_{imu,gps,fusion,serial_loopback}.cpp` | keep | `tests/CMakeLists.txt:35-50` |
| `tests/test_{spectrace_matrix,trac,e906_rpmsg_bridge,board_suite_local,runtime_stats_decode,event_streams_decode,live_profile,e906_bench_local,adversarial_invariants,zynq_buildroot_config}.py` | keep | `tests/CMakeLists.txt:58-116` |
| `tests/test_{e906_sensor_protocol,membrowse_tracking,spec_traceability,tlp_and_ctf_schema,visualizer_sdk}.py` | keep | `tests/CMakeLists.txt:87-106` (pytest-gated) |
| `tests/test_board_checkout.py` | keep | opt-in, `tests/CMakeLists.txt:97` |
| `tests/__init__.py`, `tests/conftest.py` | keep | pytest package/fixtures |
| `tests/board/{run_board_tests,board_common,e906_link,test_e906_*}.py`, `bench_e906_runtime.py`, `measure_{dma,isr}.py` | keep | `.agents/skills/e906-board-selftest/SKILL.md:24`; `apps/e906_bench/SPECIFICATION.md:54`; `engineering_log.md:1870` (`measure_dma.py` is cited by nothing; unsure) |

## `tools/`

| Path | Verdict | Evidence |
| --- | --- | --- |
| `tools/audit_specs.py`, `spectrace.py`, `spectrace_matrix.py`, `spectrace_drift.py`, `spectrace_dedupe.py`, `run_adversarial_audit.py`, `audit_baseline.txt`, `log_mistake.py`, `create_app_spec.py`, `git-hooks/pre-commit` | keep | `AGENTS.md` §1, §9; `docs/SPECTRACE.md:305,312`; `.agents/skills/spec-change-review/SKILL.md:15-17` |
| `tools/trac.py`, `trac_lib/*` (4) | keep | `.github/workflows/spectrace_dashboard.yml:49-55`; `tests/test_trac.py` |
| `tools/track_memory_footprint.py`, `track_memory_membrowse.py`, `track_quality_trends.py`, `membrowse-targets.json`, `analyze_elf_size.sh`, `analyze_map.py`, `check_e906_layout.py` | keep | `.agents/rules/e906-memory-tracking.md:27-40`; workflow lines 89, 93 |
| `tools/run_cocotb.py`, `setup_venv.sh` | keep | `.agents/skills/cocotb-regression/SKILL.md:16`; `README.md:899` |
| `tools/abstractx_wire.py`, `e906_rpmsg_bridge.py`, `board_studio_bridge.py`, `test_board_system_checkout.py`, `test_e906_repl_system.py`, `test_e906_sensors_board.py`, `seed_cubie_a5e_gps.py` | keep | `.agents/skills/e906-board-selftest/SKILL.md:24-26`; `apps/e906_repl_app/README.md:668-690`; GPS guide line 121. Note: three `test_*.py` board scripts live in `tools/` while the rest are in `tests/board/` (move candidate) |
| `tools/spec_to_doc.py`, `sync_code_to_spec.py`, `generate_io_config.py` | keep (unsure for `generate_io_config.py`) | `spec_to_doc.py` produces the `GUIDE.md` files; `sync_code_to_spec.py` in `README.md:895`; `generate_io_config.py` is not run by any build |
| `tools/run_sitl_gui_demo.py` | unsure | only `docs/tier2_contracts/observability/README.md:173`; drives `flight_display.py` |
| `tools/check_cubie_a5e_peripherals.sh` | keep, move to `tests/board/` (unsure) | referenced by nothing; on-board health check with `@impl [SPEC-GPS-01]` |
| `tools/build_gemini_context.py` | **remove** | no reference; builds the Gemini context bundle (`abstractx_context_for_gemini.txt`, gitignored); the file lists it bundles are partly stale |
| `tools/example_imu_whoami.py` | remove or move to `examples/` (unsure) | no reference; last change 2026-08-11; "PCIe-like TLP" mock demo |
| `tools/test_asp_pcie.py` | **remove** | no reference; last change 2026-08-11; tests the old "PCIe register" flow (NeoPixel/DShot sweeps) not wired to any board path |
| `tools/run_vivado_check.py` | unsure | no reference; needs Vivado 2025.2; superseded in practice by the open Gowin flow and `rtl-lint` skill |
| `tools/render_end_to_end_walkthrough.py` | unsure (keep only if the README GIF must be regenerable) | no reference; produced `docs/media/*` |
| `tools/visualizer/abstractx_studio.py`, `ctf_schema_loader.py`, `event_streams.py`, `flight_plugin.py`, `live_profile.py`, `runtime_stats.py`, `sdk/*`, `profiles/*.json`, `requirements.txt`, `SPECIFICATION.md`, `USER_PLUGIN_SPECIFICATION.md` | keep | `.vscode/tasks.json:70`; `tests/test_visualizer_sdk.py`; `tests/CMakeLists.txt:78` |
| `tools/visualizer/capture_studio.py` | keep | produced the micropython-article screenshots (its output names match `docs/articles/micropython-on-e906/assets/studio-*`) |
| `tools/visualizer/pr_comment.md` | **remove + gitignore** | generated by `tools/track_quality_trends.py:577-598`; stale content (commit `4a25c32`, 2026-10-09) |

## `cmake/`

| Path | Verdict | Evidence |
| --- | --- | --- |
| `cmake/toolchains/riscv-none-elf.cmake`, `arm-none-eabi.cmake` | keep | `CMakePresets.json:24,53,64,77` |
| `cmake/toolchains/xtensa-esp-elf.cmake` | unsure (remove) | no preset or CMake user; ESP32-P4 is RISC-V, not Xtensa; only `.agents/rules/toolchain-standardization.md:20` mentions it |
| `cmake/toolchain-e906.cmake`, `cmake/toolchain-pico2w.cmake` | **remove** | superseded by `cmake/toolchains/*`; only users are `TODO.md:174` (old command) and an `engineering_log.md` entry |

## `third_party/`

| Path | Verdict | Evidence |
| --- | --- | --- |
| `third_party/CMakeLists.txt` | keep | root `add_subdirectory(third_party)` |
| `third_party/etl` (submodule, 20.49.0) | keep | `third_party/CMakeLists.txt:6`; linked by `abstractx_core` |
| `third_party/micropython` (submodule, v1.29.0-119) | keep | `apps/e906_repl_app/CMakeLists.txt` `MICROPY_DIR` |
| `third_party/FreeRTOS-Kernel` (submodule) | keep | `apps/e906_freertos_app/CMakeLists.txt`; **not initialised** here (`git submodule status` shows `-`) |

## `docs/`

| Path | Verdict | Evidence |
| --- | --- | --- |
| `docs/README.md`, `DESIGN_SPECIFICATION.md`, `SPECTRACE.md`, `SPECTRACE_GUIDE.md`, `SPEC_TEMPLATE.md`, `TOOLING.md`, `PROS_CONS.md` | keep | linked from `README.md` / `AGENTS.md` |
| `docs/design/bus-transaction-model.md` | keep | `docs/DESIGN_SPECIFICATION.md` |
| `docs/articles/README.md` and articles `AGENTIC_AI_AND_RAG_COMPARISON`, `BAREMETAL_VS_FREERTOS_E906`, `CASE_STUDIES_AND_BENCHMARKS`, `GITHUB_NATIVE_EMBEDDED_OBSERVABILITY`, `INTRO_ABSTRACTX_HETEROGENEOUS_DEVELOPMENT`, `PCIE_AXI_STREAMING_ARCHITECTURAL_LESSONS` | keep | `docs/articles/README.md` |
| `docs/articles/SCALABLE_X_FABRIC_4K_TO_BIG.md` | orphan: link it from `docs/articles/README.md` or remove (unsure) | linked from nowhere |
| `docs/articles/gps-compass-straight-line-async/` (README + 4 SVG) | keep | `docs/README.md`; SVGs linked from the article |
| `docs/articles/micropython-on-e906/README.md` + PNG/GIF linked from it | keep | `docs/articles/README.md:15` |
| `.../assets/{call-path,memory-layout,rpmsg-ground-up,what-runs-where}.svg` | keep | sources of the linked PNGs |
| `.../assets/why-this-way.{png,svg}` | unsure (orphan) | not linked from the article after the Part 5 rewrite |
| `.../assets/studio-timeline.png`, `studio-live.mp4` (600 KB) | unsure (orphan) | not linked from the article; only written by `capture_studio.py` |
| `docs/media/end_to_end_walkthrough.gif` | keep | `README.md:158` |
| `docs/media/end_to_end_walkthrough.webp`, `slide_{1..6}_*.png` | remove (unsure) | generated by `tools/render_end_to_end_walkthrough.py`; linked from no doc |
| `docs/prompts/GEMINI_CONTINUE_ABSTRACTX.md`, `GEMINI_FIX_REMAINING_REVIEW_ITEMS.md` | remove or archive | linked from nowhere; one-off Gemini session prompts |
| `docs/tier1_vision/GEMINI_PRO_{ARCHITECTURE_AUDIT,GUI_OBSERVABILITY_REVIEW,STRATEGY}_PROMPT.md` | move to `docs/prompts/` or remove | linked from nowhere; `TODO.md:116` plans the move |
| `docs/tier1_vision/*` (other 5) and `historical/*` (5) | keep | `docs/README.md:52-67` |
| `docs/tier2_contracts/*` (10) and `observability/*` (6) | keep | `docs/README.md`, `docs/DESIGN_SPECIFICATION.md`, `docs/tier2_contracts/TLP_BUS_SPECIFICATION.md` |
| `docs/tier3_targets/bsp/*` (3), `hardware/*` (8) | keep | `docs/README.md`, `README.md`, `hw/tang_nano20k/SPECIFICATION.md` |
| `docs/tier3_targets/hardware/schematics/README.md` + 2 PDFs (2.0 MB + 1.5 MB) | keep (largest non-vendored files) | `schematics/README.md`; vendor schematics are evidence for pin facts. Consider Git LFS if more are added |
| `docs/verification/TRACEABILITY_MATRIX.md` | keep | generated on purpose (`tools/spectrace_matrix.py --matrix --write`), read by `docs/SPECTRACE.md` |
| `docs/verification/ENGINEERING_LOG.md` | **remove** (point `docs/README.md` at `../engineering_log.md`) | its first 229 lines equal the root `engineering_log.md` (diff of 2 lines); last update 2026-09-28; the root log has 67 entries vs 12 |
| `docs/verification/{ASP_RELEASE_PROCESS,ASP_REQUIREMENTS,ASP_VALIDATION_MATRIX,AXIS_TESTING_FRAMEWORK,BUILD,E2E_VERIFICATION,RUNNING_PROFILE,SASHIKO_ADVERSARIAL_REVIEW_AND_CPPUTEST_GUIDE}.md` | keep | `docs/README.md`. `AXIS_TESTING_FRAMEWORK.md` / `E2E_VERIFICATION.md` describe the removed-candidate `asp_e2e_vip.py` flow |
| `docs/verification/evidence/2026-10/{cubie-a5e,hp}.jsonl`, `e906_selftest.json`, `validation/*` | keep | evidence ledger (`tools/trac_lib/ledger.py:16`); `.gitignore:59` whitelists it |

Broken absolute links: 63 `file:///home/tcmichals/...` links in 15 tracked files (most in
`docs/verification/evidence/validation/*`, `engineering_log.md`, `apps/gps_imu_app/README.md`,
`targets/SPECIFICATION.md`, `targets/pico2w_rp2350/HOWTO.md`). They work on one PC only.

## `hw/`

| Path | Verdict | Evidence |
| --- | --- | --- |
| `hw/Makefile`, `hw/common/gowin_synth.tcl` | keep | `.agents/skills/fpga-build/SKILL.md` |
| `hw/tang9k/*`, `hw/primer20k/*`, `hw/tang_nano20k/*` (incl. `gen_fitonly_cst.py`, `*_fitonly.cst`) | keep | `hw/Makefile` targets; `hw/Makefile:91-97` |
| `hw/cyc1000/{cyc1000.qsf,top_cyc1000.sv}` | unsure | no Makefile target (Quartus), no script uses the `.qsf`; described in `docs/tier3_targets/hardware/CUBIE_A5E_CYC1000_INTEGRATION.md` |
| `hw/zynq7000/{README,SPECIFICATION,DEVICE_TREE_CONFIGURATION}.md`, `tools/setup_workspace.sh` | keep | `README.md`; `hw/zynq7000/README.md:30-32` |
| `hw/zynq7000/alinx_ac7020c/*` (10) | keep | `hw/zynq7000/alinx_ac7020c/Makefile` drives `vivado/*.tcl`, `tools/install_ps7_init.sh` |
| `hw/zynq7000/qmtech_zynq7020/{README,SPECIFICATION}.md`, `constraints/*.xdc`, `rtl/*`, `tools/{setup_workspace,sync_kernel}.sh`, `tools/zynq_diagnostics.py` | keep | traceability matrix rows SPEC-ZYNQ-04/05; README line 53 |
| `hw/zynq7000/qmtech_zynq7020/tools/load_bitstream.py` | unsure | referenced by nothing |
| `hw/zynq7000/buildroot_external/{Config.in,external.desc,external.mk,Makefile,LICENSE.vendor-source}` | keep | standard Buildroot BR2_EXTERNAL files (found by name, not by reference) |
| `hw/zynq7000/buildroot_external/board/zynq/*` (incl. `overlays/xilinx/*.dtso`, `patches/linux/0001-*.patch`) | keep | `BR2_LINUX_KERNEL_CUSTOM_DTS_DIR` / `BR2_LINUX_KERNEL_PATCH` directories in `configs/abstractx_alinx_ac7020c_defconfig:12,18` |
| `hw/zynq7000/buildroot_external/board/zynq/readme.txt` | keep | Buildroot board readme |
| `hw/zynq7000/buildroot_external/configs/abstractx_*_defconfig` (3) | keep | `hw/zynq7000/tools/setup_workspace.sh:67`; `tests/test_zynq_buildroot_config.py:24` |
| `hw/zynq7000/buildroot_external/configs/zynq_qmtech_xc720_defconfig` | **remove** | superseded by `abstractx_qmtech_zynq7020_defconfig` (older binutils, relative script paths); only user is `notes.txt`. `tests/test_zynq_buildroot_config.py:69` globs `*_defconfig`, so it is still linted until removed |
| `hw/zynq7000/buildroot_external/notes.txt` | **remove** | one stale command with a `/media/tcmichals/...` path to the old `zynq_qmtech_xc720` tree |
| `hw/zynq7000/buildroot_external/REAMME` | **remove** | empty (0 bytes), misspelled; `TODO.md:116` already lists it |
| `hw/zynq7000/buildroot_external/package/{cpputest,libmetal}/*` | keep | `hw/zynq7000/buildroot_external/Config.in`; `tests/test_zynq_buildroot_config.py` |
| `hw/zynq7000/testApps/imu_backend_validation_poc/*` (6) | keep (POC) | traceability rows SPEC-ZYNQ-IMU-POC-01..07; note `test_imu_validation.py` never runs (`pytest.ini testpaths = tests`, not in ctest) |

## What this audit did not check

* Nothing was built, run or tested (per instructions; ctest suites were running in
  `build_host`/`build_asan`/`build_tsan`). Whether `sim/test_coro_combinators.cpp` still
  compiles is unknown.
* `include/` usage was found by `#include` grep; symbol-level use (e.g. a header that is
  included but whose types are unused) was only checked for the files listed as removable.
* ESP-IDF, Zynq, Vivado and Quartus flows were not exercised, so files only those flows read
  are judged from their configs.
* `third_party/` submodule contents were not audited; `.claude/worktrees/` contents were
  only checked for merged branches and uncommitted changes.
* Link checks for docs are by basename match; relative-path correctness was not verified.
