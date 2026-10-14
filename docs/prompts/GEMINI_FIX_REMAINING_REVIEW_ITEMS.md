# Prompt: Fix Remaining 2026-10-13 / 2026-10-14 Review Items

Paste everything below the line into the coding agent, with the AbstractX repository checked out as its working directory.

---

You are working in the AbstractX repository (C++20 coroutine embedded framework, SystemVerilog RTL, Python tools). Code reviews on 2026-10-13 and 2026-10-14 fixed several runtime, bare-metal and RTL bugs and left a list of open items. Your job is to fix the open items **one at a time**, proving each fix with commands you actually ran.

## Read first (do not skip)
1. `AGENTS.md`, especially **Section 10 "Behavioural Verification Gates"**. It exists because earlier agent work claimed "100% verified" while tests were failing. You must follow it.
2. `engineering_log.md`, the last two entries (2026-10-13, 2026-10-14), for what was fixed and why.
3. `TODO.md`, section **"Code Review Findings" → Remaining**. The work queue below is ordered from that list.
4. `docs/DESIGN_SPECIFICATION.md` `[SPEC-ARCH-01]` and `[SPEC-ARCH-02]` for the awaiter, dispatcher and frame-pool contracts.

## Setup
```bash
git submodule update --init third_party/etl
cmake -S . -B build_host -DABSTRACTX_TARGET=host && cmake --build build_host -j && ctest --test-dir build_host
cmake -S . -B build_asan -DABSTRACTX_TARGET=host \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build_asan -j && ctest --test-dir build_asan
cmake -S . -B build_tsan -DABSTRACTX_TARGET=host -DCMAKE_CXX_FLAGS="-fsanitize=thread -O1 -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build_tsan -j && ctest --test-dir build_tsan
# E906 cross-build (xPack riscv-none-elf-gcc in ~/.tools)
cmake --preset e906 && cmake --build build_e906 -j
# RTL: lint + synthesis driver check + cocotb (oss-cad-suite in ~/.tools)
source ~/.tools/oss-cad-suite/environment
for f in rtl/*.sv rtl/*/*.sv; do verilator --lint-only -Wall -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM -Wno-PINCONNECTEMPTY $f --top-module $(basename $f .sv); done
```
Expected baseline: Debug and ASan 27/27; TSan 24/27 (three known race failures, item 1 below); E906 builds clean. If the baseline differs, stop and report that first. Do not "fix" tests to make them pass.

## Work queue (in this order)
1. **Linux-target data races (TSan).** `InterruptLock` is a no-op on the host and emits Cortex-M `mrs primask`/`cpsid i` under `__aarch64__`, which is invalid on AArch64 (the Cubie A5E's A55 cores). Give Linux builds a queue access policy that is actually thread-safe (e.g. `std::mutex`-backed `TAccess` for `etl::queue_spsc_isr`, or lock-free atomics), use it for `SpiWorker`/`I2cWorker` request queues and the `Dispatcher` queue on Linux, and make the `__aarch64__` branch of `interrupt_lock.hpp` not emit M-profile instructions. Fix the two test-side races (`sim/sitl_coro_sim.cpp` has two producers on one SPSC ring; `sim/test_tlp_msg.cpp` reads callback results without synchronization). Done when the TSan build is 27/27 with zero reports.
2. **CI.** Add a GitHub Actions workflow running the Debug, ASan/UBSan and TSan host builds (with submodules), the Verilator lint loop, a Yosys driver check per RTL module (`yosys -p "read_verilog -sv <file>; hierarchy -top <module>; proc; check -assert"`), and the cocotb targets registered in `sim/CMakeLists.txt`. A cocotb run that prints no `TESTS=` line is a failure.
3. **ETL submodule guard.** In `third_party/CMakeLists.txt`, if `etl/include/etl/memory_model.h` does not exist, stop with `message(FATAL_ERROR ...)` naming `git submodule update --init third_party/etl`. Mention it in the README build section.
4. **`Task<T>` hardening** (`include/asp_coro.hpp`): `co_await` on a Task whose frame allocation failed dereferences a null handle; make it an explicit, testable outcome and test it with a small `-DABSTRACTX_CORO_POOL_SIZE` in a dedicated test target. Replace `promise_type::on_complete_` (`std::function`, may heap-allocate) with a non-allocating callable; keep `when_all`/`when_any` tests passing.
5. **Linux bridge SPI correlation** (`targets/linux/src/io_processor.cpp`): single `pending_spi_tag_` / bridge buffers mean a second request before the first completes gets the wrong tag. Reject with a busy status or keep fixed in-flight slots by tag; add a back-to-back test.
6. **E906 FP context in traps** (`targets/allwinner_e906/bsp/startup.S`): the trap entry saves only integer registers while `mstatus.FS` is enabled. Either save/restore the FP caller-saved registers and `fcsr` around `riscv_trap_dispatcher`, or prove (from the disassembly of every ISR reachable from `riscv_trap_dispatcher`) that no FP instruction is used and add a build check that fails if one appears.
7. **E906 hygiene.** Add `src/rpmsg.cpp` to the E906 CMake sources if it is meant to be used (fix whatever fails to build), otherwise delete it. Replace hardcoded `/home/tcmichals/...` in `cmake/toolchains/*.cmake` with `$ENV{HOME}`.
8. **RTL.** Add a reset synchronizer in `asp_top` (async assert, sync de-assert) and use `rst_n` consistently. Add cocotb testbenches for `asp_axi_dma`, `asp_dshot_core`, `asp_pwm_decoder`, `asp_neopixel_core`, `asp_ila_trace`. Port `sim/cocotb/asp_e2e_vip.py` / `test_asp_top_cocotb.py` to the dual-SPI interface (see `test_asp_tlp_64b_cocotb.py`), or delete them if `test_asp_tlp_64b_cocotb.py` already covers the same path.
9. **Audit tooling honesty.** Make `tools/audit_specs.py` count per-target `SPECIFICATION.md` requirements, not only `docs/DESIGN_SPECIFICATION.md`. Make `run_adversarial_audit.py` Stage 5 run (or require evidence of) the real tests instead of checking that directories exist. Do not lower any check to make it pass.
10. **Synchronous HAL fallbacks.** Find callers of `transfer_sync`, `transfer_byte`, `read_reg_sync_impl`, `Icm42688p::init()`. Remove if unused outside pre-scheduler bring-up; otherwise list callers. Update AGENTS.md Section 4 to match.
11. **Warnings.** Make the host build clean under `-Wall -Wextra` without pragmas.
12. **Repo hygiene.** `git mv` `docs/tier1_vision/GEMINI_*_PROMPT.md` to `docs/prompts/`; rename `hw/zynq7000/buildroot_external/REAMME`; consolidate `cmake/toolchain-*.cmake` with `cmake/toolchains/`.
13. **README and status claims.** Cite a measurement in the repo or reword as a goal: "formally verified", "15-cycle context switch", "<100 bytes" per frame, "sub-16 KB" binaries (the E906 `gps_imu_app` is 66 KB text), "120 FPS", "100% pass rate", "100% architectural compliance" (SpecTrace currently reports 99 violations). Mark the ESP32-P4 target as a stub and the Pico dual-core split as not yet implemented. Label hardcoded demo metrics in the Studio GUI as simulated.

### Do NOT do these without the human (hardware/address decisions)
- The E906 shared-memory design: the Linux ingestion side (`linux_trace_receiver_task`) does not exist, `platform_send_telemetry` overwrites a fixed address, and the address map is inconsistent across linker scripts, `memory_map.h`, TODO.md and the specs. Do not pick addresses. Write up the inconsistencies and a proposed single map as a Markdown note, and stop.
- Launching Pico Core 1 (`[SPEC-ARCH-05]`): needs a cross-core queue design first. Propose it in Markdown; do not implement.

## Rules for every item
- Spec first. If the item changes a contract, update the relevant `SPECIFICATION.md` / `DESIGN_SPECIFICATION.md` requirement before the code. Tag code with `// @impl [SPEC-*] <file>`.
- After each item, run **both** test builds and paste the final `ctest` summary lines.
- One commit per item, with the commands and results in the commit message. Do not combine items.
- Add an `engineering_log.md` entry only for real bugs (items 1, 4, 5, 6, 8), in the existing format.
- Do not change behaviour outside the item you are working on. Do not reformat files you are not otherwise editing.
- Do not claim anything you did not run. If you could not build or test something (cross-compiled targets, RTL, hardware), say so explicitly.
- For bare-metal changes, verify from the disassembly (`riscv-none-elf-objdump -d`), not from the source or its comments. A comment in `startup.S` said "If mcause < 0" while the code tested `mstatus`.
- For RTL changes, simulation passing is not enough: multi-driver and reset bugs only show up in synthesis checks.

## Final report
At the end, give a table: item, status (done / partial / skipped), commit hash, and the test evidence. Then list what you did **not** verify.
