# Prompt: Continue AbstractX (from 2026-10-16, commit 52507cb)

Paste everything below the line into the coding agent, with the AbstractX repository checked out as its working directory.

---

You are continuing work on AbstractX. It is three projects in one repository:

- **SpecTrace:** requirements (what/why), skills (how each is checked), an evidence ledger in git, and a feedback loop.
- **Framework:** C++20 coroutines, zero-copy transports, targets E906 / Pico 2 W / ESP32-P4 / Linux, and the `asp_*` RTL.
- **Studio:** the observability GUI.

The top-level goals are **fast, low jitter, and no memory copies**. How each board avoids copies is written in that target's `SPECIFICATION.md`.

Work through the queue below **one item at a time**. Prove every item with commands you actually ran, and record the results.

## Read first
1. `AGENTS.md` sections 0 and 10–12:
   - Section 0 is the operating loop: retrieve the requirement, spec first, check with skills, record everything including misses, never claim done without evidence.
   - Section 10 holds the verification gates.
   - Sections 11–12 hold the RTL conventions and git identity.
2. `docs/TOOLING.md` (the Trac families and the tool map) and `docs/SPECTRACE_GUIDE.md` §9 (the plan).
3. `TODO.md`, the "Code Review Findings → Remaining" section.
4. The `engineering_log.md` entries from 2026-10-13 onward.
5. `targets/allwinner_e906/SPECIFICATION.md`, for the memory map, RPMsg and zero-copy design.

## Rules you must follow
- **Spec first.** New behaviour needs a requirement (`[SPEC-…]` with **Goal**, **Requirement**, **Verified by**, **Implementation Target**) before code. Code carries `@impl [SPEC-…]` and tests carry `@verifies [SPEC-…]`.
- **No untraced code.** Run `python3 tools/spectrace_matrix.py --changes --staged --strict` before every commit; it must report no UNTRACED and no BROKEN lines. Enable the hook with `git config core.hooksPath tools/git-hooks`.
- **Record evidence:**
  - Record sizes and timing with `python3 tools/trac.py size|perf --record`. Use build variants such as `--elf e906/debug=…` and `--elf e906/lto=…`.
  - Write lessons and misses to `engineering_log.md`, each with a category: product-bug, tool-bug, false-alarm, process-gap or hardware-fact.
- **Hardware facts** come from cubie-a5e and the linux-cubie `sunxi_rproc` driver, not from the AbstractX docs alone. Don't edit the cubie-a5e repo.
- **E906 constraints:**
  - The firmware runs from SRAM, inside a 192 KB `SRAM_FAST` window.
  - The D-cache stays off; `mhcr=0x1031`.
  - remoteproc `trace0` is the low-speed channel for processor faults and exceptions. RPMsg (`abstractx-tlp` 1024, `abstractx-trace` 1025) is the high-speed data path.
  - Firmware is built with `-fno-exceptions`. Don't reintroduce `std::exception_ptr`, `std::function` or heap use in the runtime (`[SPEC-ARCH-02]`, `[SPEC-ARCH-09]`).
- **Git:**
  - Commits are authored by `Tim Michals <tcmichals@gmail.com>`.
  - **No `Co-Authored-By` or other AI trailers, and no `Signed-off-by`.**
  - Commit and push when an item is done. Never force-push.
- **Be honest.** If a test fails or a step was skipped, say so with the output. Don't write "100% verified".

## Setup
```bash
git submodule update --init third_party/etl
cmake -S . -B build_host -DABSTRACTX_TARGET=host -DCMAKE_BUILD_TYPE=Debug && cmake --build build_host -j && ctest --test-dir build_host
cmake -S . -B build_asan -DABSTRACTX_TARGET=host -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build_asan -j && ctest --test-dir build_asan
cmake -S . -B build_tsan -DABSTRACTX_TARGET=host -DCMAKE_CXX_FLAGS="-fsanitize=thread -O1 -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build_tsan -j && ctest --test-dir build_tsan
cmake --preset e906 && cmake --build build_e906 -j               # Release
cmake --preset e906-debug && cmake --build build_e906_debug -j   # -Og -g3
cmake --preset e906-lto && cmake --build build_e906_lto -j       # Release + LTO
source ~/.tools/oss-cad-suite/environment && python3 tools/run_cocotb.py
```
If ETL headers are not found, pass `-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES=<etl>/include`.

**Baseline at 52507cb:**
- Host: 33/33 tests in Debug, ASan/UBSan and TSan.
- cocotb: 16/16 targets.
- Traceability: 21/131 requirements complete.
- `rtl:lint` fails with two known warnings: SYNCASYNCNET on `asp_top` `rst_n`, and CASEINCOMPLETE in `asp_ila_trace`.
- E906 Release: text 52,992 B, RAM 163,532 B of 196,608 B.

## Work queue (in order)
1. **Right-size the E906 coroutine pool.** `ABSTRACTX_CORO_POOL_SIZE` is 60 KB.
   - Add a host or sim test that runs the gps_imu_app task set and reports `coro_pool_high_water()`.
   - Set a per-target pool size of high water plus 25% headroom, through the target CMake.
   - Record the new size with `trac size --record`.
2. **Don't allocate the static trace pool when a zero-copy transport is used.** On the E906, `g_tracer` keeps a ~2 KB `StaticPoolTraceTransport` it never uses. Make the pool exist only when the platform has no transport of its own. Check that the trace tests still pass.
3. **Right-size the three E906 TLP rings** (4,224 B each) against the RPMsg message size of 7 TLPs. Spec first, in `targets/allwinner_e906/SPECIFICATION.md`.
4. **Pico 2 W zero-copy trace and UDP transport** (`[SPEC-PICO-TRACE-01]`): lwIP `PBUF_POOL` pbufs, acquire → fill in place → `pbuf` send. No `PBUF_RAM`.
   - Add a host test that checks pointer identity, like `sim/test_trace_zero_copy.cpp`.
   - Also find out why the Pico ELF still contains about 55 `_Unwind_*` symbols. It may be ARM EHABI from the SDK. Remove them if that's safe, and record what you found.
5. **E906 trap handler:** save the FP registers (the core is RV32IMAFDC) before calling C. Spec item `[SPEC-E906-CRASH-01]`. Check it with the disassembly skill.
6. **RTL lint failures:**
   - Add a 2-flop reset synchroniser for `rst_n` in `asp_top`.
   - Add a `default:` to the case in `asp_ila_trace`.
   - `trac test --rtl` must pass, and cocotb must stay at 16/16.
7. **Linux SPI bridge:** correlate the `pending_spi_tag_` request and response in `targets/linux/src/io_processor.cpp`, and add a test.
8. **Remove the synchronous HAL fallbacks** listed in `TODO.md`, if no target uses them any more.
9. **SpecTrace tooling** (`SPECTRACE_GUIDE.md` §9):
   - STALE/FAILING verdicts from the evidence ledger.
   - `trac report` with charts.
   - A CI workflow that runs the real checks and publishes github-action-benchmark graphs from `trac export-benchmark`.
   - Fix the `.gitignore` `*.json` rule that hides results.
10. **Raise traceability.** Add `Verified by` evidence to IMPLEMENTED requirements that have real tests. Never add tags without a test that actually checks the requirement.

**Waiting on the owner; don't start these:**
- Real Tang Nano 20K pins.
- QMTech Zynq builds (they need Vivado and an ARM toolchain).
- Porting the newer cubie-a5e IPC fixes: single writer per field, coalesced doorbell, `SHM_ERROR_MAGIC`. Wait until those are committed in cubie-a5e.

## For each item, report
- the requirement tag(s) you added or changed;
- the files you changed;
- the commands you ran and their pass/fail counts, plus any size or timing change from `trac`;
- what you recorded (ledger records, engineering-log entry);
- the commit hash, and anything left open.
