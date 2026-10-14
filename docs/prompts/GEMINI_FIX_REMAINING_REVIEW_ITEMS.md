# Prompt: Fix Remaining 2026-10-13 Review Items

Paste everything below the line into the coding agent, with the AbstractX repository checked out as its working directory.

---

You are working in the AbstractX repository (C++20 coroutine embedded framework, SystemVerilog RTL, Python tools). A code review on 2026-10-13 fixed four runtime bugs and left a list of open items. Your job is to fix the open items **one at a time**, proving each fix with commands you actually ran.

## Read first (do not skip)
1. `AGENTS.md`, especially **Section 10 "Behavioural Verification Gates"**. It exists because earlier agent work claimed "100% verified" while tests were failing. You must follow it.
2. `engineering_log.md`, the last entry (2026-10-13), for what was fixed and why.
3. `TODO.md`, section **"Code Review Findings (2026-10-13)" → Remaining**. That list is your work queue.
4. `docs/DESIGN_SPECIFICATION.md` `[SPEC-ARCH-01]` and `[SPEC-ARCH-02]` for the awaiter, dispatcher and frame-pool contracts.

## Setup
```bash
git submodule update --init third_party/etl
cmake -S . -B build_host -DABSTRACTX_TARGET=host && cmake --build build_host -j && ctest --test-dir build_host
cmake -S . -B build_asan -DABSTRACTX_TARGET=host \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build_asan -j && ctest --test-dir build_asan
```
Both must be 27/27 before you start. If they are not, stop and report that first; do not "fix" tests to make them pass.

## Work queue (in this order)
1. **ASan CI job.** Add a GitHub Actions workflow that runs both builds above on `ubuntu-latest` (with submodules) and fails on any test or sanitizer failure.
2. **ETL submodule guard.** In `third_party/CMakeLists.txt`, if `etl/include/etl/memory_model.h` does not exist, stop with `message(FATAL_ERROR ...)` that names the `git submodule update --init third_party/etl` command. Mention the command in the README build section.
3. **`Task<T>` hardening** (`include/asp_coro.hpp`):
   - `co_await` / `await_resume` on a Task whose frame allocation failed (null handle) currently dereferences null. Make allocation failure an explicit, testable outcome (e.g. a `valid()` check plus a trap hook), and add a test that exhausts a small pool (`-DABSTRACTX_CORO_POOL_SIZE`) in a dedicated test target.
   - `promise_type::on_complete_` is `std::function` (may heap-allocate). Replace it with a non-allocating callable (`etl::delegate` or function pointer + context) and keep `when_all` / `when_any` tests passing.
4. **Linux bridge SPI correlation** (`targets/linux/src/io_processor.cpp`): the single `pending_spi_tag_` / `bridge_tx_buf_` / `bridge_rx_buf_` means a second bridge request before the first completes gets the wrong tag and overwrites buffers. Either reject the second request with a busy status, or keep a small fixed array of in-flight slots indexed by tag. Add a test that submits two requests back to back and checks both completions' tags and data.
5. **Synchronous HAL fallbacks.** Find every caller of `transfer_sync`, `transfer_byte`, `read_reg_sync_impl` and `Icm42688p::init()`. If none are pre-scheduler bring-up code, remove them. Otherwise leave them and list the callers. Update AGENTS.md Section 4 to match the result.
6. **Warnings.** Make the host build clean under `-Wall -Wextra` (examples, `sim/sitl_coro_sim.cpp`, `targets/linux/include/i2c_worker.hpp:182`). Do not silence warnings with pragmas; fix the code.
7. **Repo hygiene.** `git mv` `docs/tier1_vision/GEMINI_*_PROMPT.md` to `docs/prompts/`. Rename `hw/zynq7000/buildroot_external/REAMME` to `README`. Consolidate `cmake/toolchain-*.cmake` with `cmake/toolchains/` and update every reference (`grep -rn toolchain-`).
8. **README claims.** For each of these, either cite a measurement already in the repo (file and line) or reword it as a goal: "formally verified", "15-cycle context switch", "<100 bytes" per frame, "sub-16 KB" binaries, "120 FPS", "100% pass rate". Do not invent numbers.

## Rules for every item
- Spec first. If the item changes a contract, update the relevant `SPECIFICATION.md` / `DESIGN_SPECIFICATION.md` requirement before the code. Tag code with `// @impl [SPEC-*] <file>`.
- After each item, run **both** test builds and paste the final `ctest` summary lines.
- One commit per item, with the commands and results in the commit message. Do not combine items.
- Add an `engineering_log.md` entry only for real bugs (items 3 and 4), in the existing format.
- Do not change behaviour outside the item you are working on. Do not reformat files you are not otherwise editing.
- Do not claim anything you did not run. If you could not build or test something (cross-compiled targets, RTL, hardware), say so explicitly.

## Final report
At the end, give a table: item, status (done / partial / skipped), commit hash, and the test evidence. Then list what you did **not** verify.
