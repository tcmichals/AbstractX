# Tracing, Profiling and Debugging MicroPython on the E906

Status: research note, 2026-10-23. No firmware was changed, built or run for this note.
Claims about this repo cite `file:line`. Anything marked **not verified** is an estimate
or comes from outside sources only.

Question (Tim Michals): is there some way to trace or measure the performance of the
MicroPython code, and is there a debugger?

Short answer: no Python-level profiler or debugger exists today. The cheapest useful step is
to make the 1 kHz profiler interrupt that already exists also record the **current Python
function and line**. This needs no `sys.settrace`. It does need two small changes to how
MicroPython is built. A `pdb`-style debugger is possible with `sys.settrace`, but it costs
flash, heap and speed, and it cannot show local variables in this MicroPython version.

---

## 1. What exists today

| Tool | What it shows | Where |
| :--- | :--- | :--- |
| PC-sampling profiler, CTF stream 4 | The interrupted C program counter plus the CPU owner (coroutine frame, idle, or loop), at `profile_hz` (default 1000) | `targets/allwinner_e906/src/timer.cpp:126-130`, `include/abstractx/trace/event_trace.hpp:118-123`, spec `docs/DESIGN_SPECIFICATION.md:621` |
| Timeline, CTF stream 5 | Resume, interrupt and idle spans | `event_trace.hpp:126-133`, spec `docs/DESIGN_SPECIFICATION.md:642` |
| Studio live profile | PC to function and line with `addr2line` | `tools/visualizer/live_profile.py:6,24` |
| `abstractx.trace(task_id, state, msg)` | Manual named marker into the coroutine trace, then a flush | `apps/e906_repl_app/src/modabstractx.c:262-269`, `apps/e906_repl_app/src/mphalport.cpp:176-182` |
| `abstractx.cycles()` | Cycle counter | `modabstractx.c:113-118` |
| Crash autopsy | Register dump to `trace0` and S_UART0 on a fault | `targets/allwinner_e906/src/crash.cpp:34-58`, `irq_dispatcher.cpp:13` |

The profiler can only see C. Python time shows up as `mp_execute_bytecode`, `gc_alloc` and
similar functions, all charged to the `repl_rx` coroutine. It cannot tell which Python line
is hot.

### Problems found while checking
1. **Tracebacks have no real line numbers.** The port uses `MICROPY_CONFIG_ROM_LEVEL_MINIMUM`
   (`apps/e906_repl_app/include/mpconfigport.h:29`). At that level
   `MICROPY_ENABLE_SOURCE_LINE` is off (`third_party/micropython/py/mpconfig.h:916-917`), so
   the compiler stores no line table. In the 2026-10-20 build (`build_e906`),
   `mp_emit_bc_set_source_line` is 2 bytes, an empty function. Every source line then decodes
   as line 1 (`py/bc.h:334-347`). This blocks any line-level profiler or debugger. Not
   verified on the board by running a failing script.
2. **`abstractx.cycles()` is 32-bit, not 64-bit as its comment says.** `mp_hal_ticks_cpu()`
   truncates `mcycle` to `mp_uint_t` (`mphalport.cpp:74-76`). At 200 MHz it wraps every
   ~21.5 s. Values at or above 2^30 do not fit a small int, so `mp_obj_new_int_from_ull`
   (`modabstractx.c:116`) allocates a long int on the GC heap on about half of all calls.
   Use differences modulo 2^32, or return the low 30 bits.
3. The MicroPython modules `micropython` (`mem_info`, `qstr_info`, `stack_use`), `gc` and `time`
   are not in the build (no `mp_module_micropython`, `mp_module_gc` or `mp_module_time` in
   `nm`). Their defaults need `CORE_FEATURES` or higher (`py/mpconfig.h:1599,1729,1905`).
   Manual timing is available only through `abstractx.cycles()` and `abstractx.ticks_ms()`.

---

## 2. Python-level profiling options

### 2a. Manual instrumentation (available now)
Use `abstractx.cycles()` before and after a block, or `abstractx.trace()` markers that appear
on the Studio timeline. Cost: one call each (`trace()` also flushes the tracer). Good for
"how long does this function take". It does not find hot spots you did not guess.

### 2b. `sys.settrace` (`MICROPY_PY_SYS_SETTRACE`)
MicroPython v1.30.0-preview (`third_party/micropython`, `git describe`
`v1.30.0-preview-118-g424cb8d7a`) supports it, and it is off by default (`py/mpconfig.h:1845`).
Turning it on brings in:
* `py/profile.c` (845 lines) and frame objects;
* `MICROPY_PERSISTENT_CODE_SAVE` (required, `py/profile.c:37`; enabled by `py/mpconfig.h:423`)
  and `MICROPY_PY_BUILTINS_CODE_FULL` (`py/mpconfig.h:1419`);
* an extra `prev_state` and `frame` field in every `mp_code_state_t` (`py/bc.h:248-251`);
* **a GC-heap frame object on every Python call, even with no trace function set**
  (`mp_prof_frame_enter`, `py/profile.c:189-196`, called through `FRAME_ENTER` in `py/vm.c:157-164`);
* a check on every opcode (`TRACE_TICK`, `py/vm.c:179-188`).

Estimated cost (**not verified**, nothing was built): 5-8 KB of code plus `SOURCE_LINE`, more
GC pressure on the 128 KB heap, a slowdown with no callback set, and 10x or more with a Python
callback on every line. Space 0 is ~89.6% of 192 KB (figure from the request, not re-measured
here), which leaves ~20 KB. Most of `profile.c` runs on every opcode or call, so it cannot be
moved to `.text_cold`. A profiler built on settrace also changes the timing it measures. For
profiling, settrace is the wrong tool here.

### 2c. Sampling the current Python line from the existing 1 kHz interrupt (recommended)
**Is a "current code state" pointer available without settrace?** No.
`MP_STATE_THREAD(current_code_state)` exists only under `MICROPY_PY_SYS_SETTRACE`
(`py/mpstate.h:323-326`, `py/vm.c:149-196`). Without it, the pointer is a local variable of
`mp_execute_bytecode`, and an interrupt cannot find it.

**It is cheap to add one, without patching MicroPython.** `vm.c` defines the interpreter
through `MICROPY_WRAP_MP_EXECUTE_BYTECODE(mp_execute_bytecode)` (`py/vm.c:220`,
default `py/mpconfig.h:2233-2234`). The port can rename it, for example
`#define MICROPY_WRAP_MP_EXECUTE_BYTECODE(f) f##_inner`, and provide its own
`mp_execute_bytecode` that pushes `{code_state, prev}` onto a linked list held in the
wrapper's C stack frame, calls `_inner`, then pops. Only `objfun.c:294` and
`objgenerator.c:210` call it, and the inner function catches all exceptions and returns
`MP_VM_RETURN_EXCEPTION`, so the pop always runs. `MICROPY_STACKLESS` is off, so no frames
are switched without a call. Cost: about 40 bytes of code and about 10 cycles per Python call
(**not verified**). The linked list also gives the ISR the call stack, not just the innermost
frame.

**Is `code_state->ip` current?** Yes. `SELECTIVE_EXC_IP` is 0 (`py/vm.c:222`), so
`MARK_EXC_IP_GLOBAL()` stores `code_state->ip` before every opcode dispatch
(`py/vm.c:240-242, 329-330`). A sample sees the opcode that is running, or the call opcode
when the interrupt lands in C code that Python called (`i2c_read`, `gc_alloc`). That second
case is useful: it charges C time to the Python line that caused it.

**What the ISR must read, and why it is safe.** On this single core, an interrupt either sees
the list head before or after a complete push or pop (one aligned word store). It reads:
1. the list head (volatile word); if NULL, no Python frame is active;
2. `cs->ip` and `cs->fun_bc->bytecode` (live: the wrapper is still running, so the frame and its
   function are on the C stack and reachable for the GC; the GC never moves objects);
3. the bytecode prelude: `MP_BC_PRELUDE_SIG_DECODE`, `MP_BC_PRELUDE_SIZE_DECODE`, then
   `block_name` and the line table, as the traceback code does (`py/vm.c:1437-1447`), then
   `mp_bytecode_get_source_line` (`py/bc.h:334`).

Decode **in the ISR**, not later. A raw `fun_bc` pointer can be freed and reused by the GC
before `status_service()` drains the ring. Limit the line-table walk to about 256 entries so the
ISR time stays bounded. Estimate: 0.5-3 µs per sample, 0.05-0.3% CPU at 1 kHz
(**not verified**). The prelude is read-only once compiled, so a sample that interrupts the GC
or the compiler is still safe.

The ISR records `{block_name qstr, line, depth, flags}` (8 bytes). The qstr ids are safe to
resolve later because qstrs are never freed. The cooperative drain (`status_service()`) sends a
name record (`qstr id -> text`) the first time it sees an id. Static qstrs can also be mapped
from the build's generated `qstrdefs`.

Two details:
* The VM hook runs other coroutines from inside the Python loop (`mphalport.cpp:80-103`), so a
  Python frame can be "active" while another coroutine owns the CPU. The ISR already knows the
  owner (`event_trace.hpp:120-121`). Set a flag bit when the owner is not `repl_rx`, and
  Studio can show "Python waiting at line N" separately from "Python running line N". Time spent
  in `sleep_ms` shows up the same way, as idle, with the line of the sleep call.
* Scripts arrive through raw REPL (`apps/e906_repl_app/run_e906_script.py:110-164`), so the file
  name is `<stdin>` and line numbers count from the script that was sent. Studio (or the
  sender) must keep the text of the last script to show the source lines.

---

## 3. Debugger

### Python
* **Upstream MicroPython has no `pdb`.** Community options, all built on `sys.settrace`:
  * [`micropython-lib` PR #1022](https://github.com/micropython/micropython-lib/pull/1022)
    `debugpy` (Andrew Leech): VS Code over the Debug Adapter Protocol, with breakpoints, stepping,
    stack and evaluate. **Open, not merged.** It depends on core PR
    [micropython#8767](https://github.com/micropython/micropython/pull/8767) (improved settrace).
    Its default transport is TCP; a stream transport exists. Real local variable names need
    `MICROPY_PY_SYS_SETTRACE_LOCALNAMES`, which is not in this tree.
  * [`mpdb`](https://github.com/bobveringa/mpdb): a proof-of-concept pdb with `break`, `next`,
    `step`, `until`, `continue` and `p expr`; conditional breakpoints only at global scope.
  * [`mp_debugpy`](https://github.com/josverl/mp_debugpy): experimental.
* **A minimal debugger here** would be a small Python module sent before the script (no
  firmware code beyond enabling settrace and `SOURCE_LINE`):
  `sys.settrace(cb)`; `cb` returns a local tracer only for frames whose
  `f_code.co_filename`/`f_lineno` match a breakpoint set, or when stepping. On a stop it prints
  `BREAK <stdin>:<line>` and reads commands with `abstractx.console_read()`
  (`modabstractx.c:225-235`) until it gets `c`, `n` or `s`. `p expr` uses `eval(expr, frame.f_globals)`;
  `eval` is in the build (`mp_builtin_eval` in `nm`). The other coroutines keep running while it
  waits, because the VM hook still pumps the dispatcher. Studio's CLI tab, or a variant of
  `run_e906_script.py`, would need to handle the `BREAK` lines instead of streaming raw output.
* **Limits on this target:** all the settrace costs in 2b; frames expose only `f_back`, `f_code`,
  `f_globals`, `f_lasti` and `f_lineno` (`py/profile.c:97-112`), so **local variables cannot be
  inspected**; no breakpoints inside C; Ctrl-C handling (`mpconfigport.h:41`) and raw-REPL
  framing must not clash with the command channel; the timing of interrupt-driven code changes
  while it is stopped.
* Without settrace, the cheaper "debugger" is better tracebacks (turn on `SOURCE_LINE`) plus
  `print`/`abstractx.trace()`.

### C, without JTAG
The T527 does not route the E906 debug module to the ARM bus
(`~/projects/cubie/cubie-a5e/docs/articles/part1_heterogeneous_riscv_intro_architecture.md`, section 7.1),
so OpenOCD on Linux cannot attach. Whether physical JTAG pins exist on the board was **not verified**.
Options, cheapest first:
1. **Crash autopsy (exists):** registers and `mepc` to `trace0` (`crash.cpp:34-58`).
   Studio or a script can symbolize `mepc`/`ra` with `addr2line`, as the profiler already does.
2. **Post-mortem core file:** after a crash, Linux reads SRAM Space 0/1 through `/dev/mem`
   (host PA `0x07280000`/`0x072C0000`) and writes an ELF core with the crash frame, so
   `riscv-none-elf-gdb e906_repl_app core` gives a backtrace and variables. Host-only Python,
   no firmware cost. The same `/dev/mem` path can read live memory without stopping the core.
3. **GDB stub over RPMsg:** `ebreak` traps to the existing handler, which runs a small RSP stub.
   While stopped it must poll the message box and vring itself, because interrupts and the event
   loop are frozen. Linux bridges a TCP port to the endpoint. Estimate 3-6 KB, most of it can go
   in `.text_cold` (**not verified**). It stops the whole E906, and RPMsg peers may time out.

---

## 4. Recommendation: smallest useful first step

1. **Turn on `MICROPY_ENABLE_SOURCE_LINE`** in `mpconfigport.h`. This gives real line numbers in
   tracebacks right away and is required for everything else. Cost: a few hundred bytes of code
   in `emitbc.c` (**not verified**), and roughly 1-2 bytes of GC heap per source line of
   compiled Python. No runtime cost.
2. **Python line sampling in the existing profiler interrupt** (section 2c): the
   `mp_execute_bytecode` wrapper, an ISR decode of about 200-400 bytes, and a ring of 128 x 8 B =
   1 KB (it can go in `.sram_c` in Space 1). CPU cost about 0.1-0.3% at 1 kHz plus ~10 cycles
   per Python call. All figures are estimates and must be measured with
   `tools/analyze_elf_size.sh` and on the board.
3. **Wire and Studio:** a new CTF stream 6 (`PySample`: u32 block-name qstr, u16 line, u8 depth,
   u8 flags; plus a name record), so stream 4 decoders stay unchanged. In Studio's live profile
   tab, add a "Python" table, "function : line -> % of samples (running / waiting)", with the
   source text of the last script sent, next to the C view it already has. Owner and flags let
   it split "Python busy" from "Python waiting in sleep or I/O".

Following the project loop, this starts with a new `[SPEC-*]` requirement (a sibling of
`[SPEC-TRACE-09]`) and a host test that runs the wrapper and decoder on the host MicroPython
build at least 1000 times. Leave settrace and the debugger until the sampler shows they are
needed; if they are, put a settrace build behind a separate debug build option, not in the
default firmware.

## Not checked
* No firmware was built, so every size and CPU figure is an estimate.
* Nothing was run on the board. The "line 1" traceback finding comes from the config and an
  existing ELF (`build_e906`, 2026-10-20), not from a board run.
* The `MICROPY_WRAP_MP_EXECUTE_BYTECODE` rename was not compiled, and the ISR-safety argument was
  not tested.
* The PR #1022 status is from 2026-10-23 and only partly read. Whether the Cubie A5E has
  physical JTAG pins was not checked.
