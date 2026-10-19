# AbstractX XuanTie E906 MicroPython REPL Engineering Log

This log chronicles all architectural decisions, hardware facts, bugs identified, and lessons learned during the bring-up and verification of the **AbstractX XuanTie E906 MicroPython REPL Application** (`apps/e906_repl_app/`) on the Allwinner Radxa Cubie A5E (A527 / T527).

---

## 2026-10-18: RemoteProc Memory Window & Resource Table Invariants

### 1. Hardware Fact: RemoteProc VirtIO Vring Device Address (`FW_RSC_ADDR_ANY`)
* **Category**: `hardware-fact`
* **Subsystem**: `targets/allwinner_e906/bsp/resource_table.c`, `sunxi-rproc`
* **Observation**: Linux `sunxi-rproc` rejected firmware image load with:
  `Allocated carveout doesn't fit device address request`.
* **Root Cause**: The resource table vring entries had `.da = 0`. In the Linux kernel `remoteproc_virtio.c`, `.da = 0` is interpreted as an explicit request for physical address `0x00000000`, which fails memory carveout allocation.
* **Resolution**: Set `.da = FW_RSC_ADDR_ANY` (`0xFFFFFFFFUL`) in `resource_table.c`. Linux then assigns dynamic host buffers from the reserved memory node (`sram1@72c0000`).

---

### 2. Hardware Fact: DebugFS `trace0` Buffer Placement in SRAM Space 0
* **Category**: `hardware-fact`
* **Subsystem**: `targets/allwinner_e906/bsp/e906_ddr.ld`, `hal::Trace`
* **Observation**: `/sys/kernel/debug/remoteproc/remoteproc0/trace0` returned only 1 byte (`0x02`) when `.trace_buffer` was placed in SRAM Space 1 (`0x40000000`).
* **Root Cause**: Linux `sunxi-rproc` debugfs trace mapping resolves device addresses via `da_to_va()`. While SRAM Space 1 (`0x40000000`) requires dynamic hardware remap (`REMAP_CTRL_REG[1]=1`), SRAM Space 0 (`0x3FFC0000` -> host `0x07280000`) is unconditionally mapped by the kernel.
* **Resolution**: Placed `.resource_table` and `.trace_buffer` into `SRAM_FAST` (`0x3FFC0000`). Startup messages and REPL stdout now immediately stream into `/sys/kernel/debug/remoteproc/remoteproc0/trace0`.

---

## 2026-10-18: RPMsg Endpoint Protocols & Data Plane Sizing

### 3. Architectural Invariant: 496-Byte Payload & TLP Batching
* **Category**: `hardware-fact`
* **Subsystem**: `hal::Rpmsg`, `apps/e906_repl_app/linux/`
* **Wire Protocol**:
  * VirtIO VRing Buffer Size: **512 bytes**
  * RPMsg Header (`struct rpmsg_hdr`): **16 bytes**
  * Maximum Usable Payload: **496 bytes** (`512 - 16 = 496 B`)
* **Channel Allocations**:
  * Endpoint `1024` (`abstractx-tlp`): Transports 64-byte hardware TLPs. Batches $\lfloor 496 / 64 \rfloor = 7$ TLPs per message (448 bytes) to minimize doorbell interrupts.
  * Endpoint `1025` (`abstractx-trace`): Bulk barectf CTF 1.8 binary trace packets (written zero-copy in host SRAM).
  * Endpoint `1026` (`abstractx-repl`): Event-driven bidirectional character and line stream for MicroPython REPL.

---

## 2026-10-18: Peripheral Pinout & Co-Processor Ownership

### 4. Hardware Fact: E906 Exclusive Ownership of UART2 & TWI1
* **Category**: `hardware-fact`
* **Subsystem**: `cubie-a5e-flight-stack` DT overlay, UART2, TWI1
* **Pin Mapping (40-Pin Header)**:
  * **Pin 2 (or 4)**: 5V Power (Red) -> GPS VCC
  * **Pin 6 (or 14)**: GND (Black) -> GPS GND
  * **Pin 3**: I2C1-SDA (Blue) -> Compass / u-blox DDC SDA (`0x02502400`, TWI1)
  * **Pin 5**: I2C1-SCL (Orange) -> Compass / u-blox DDC SCL (`0x02502400`, TWI1)
  * **Pin 11**: UART2-TX (PB0, Yellow) -> GPS RX (`0x02500800`, UART2)
  * **Pin 13**: UART2-RX (PB1, Green) -> GPS TX (`0x02500800`, UART2)
* **Finding**: Accessing `/dev/ttyS2` or `/dev/i2c-1` from Linux fails with I/O error (`Errno 5` / `Errno 110`). This is expected: the Device Tree overlay transfers hardware ownership to the XuanTie E906 co-processor. Linux does not clock or drive these peripherals; E906 drives them directly.

---

## 2026-10-18: Cooperative Coroutine Pumping vs. Interrupt Deadlocks

### 5. Product Bug: Awaiting IRQ Doorbell in Cooperative REPL Coroutine
* **Category**: `product-bug`
* **Subsystem**: `apps/e906_repl_app/src/main.cpp`, `rpmsg_console.hpp`
* **Symptom**: Writing commands into `/dev/rpmsg0` from Linux failed to yield responses; REPL coroutine appeared hung.
* **Root Cause**: `repl_coroutine` awaited `console.read_char_async()`, which suspended the coroutine until `fc_msgbox_doorbell_isr()` fired. However, in bare-metal bring-up where Linux VirtIO negotiation completes after co-processor boot, or if doorbell interrupts are coalesced, the coroutine was starved.
* **Resolution**: Refactored `repl_coroutine` to use `console.try_read_char(ch)` with cooperative yielding via `co_await abstractx::step_async()`. Added explicit `abstractx::hal::g_rpmsg.poll()` pumping in the main event loop (`main.cpp`). Characters received from Linux are now dispatched immediately without dependency on interrupt timing.

---

## 2026-10-18: Linker Script `.init_array` & Zero-DDR Pure SRAM Fit

### 6. Tool / Linker Bug: Missing `.init_array` in Linker Script
* **Category**: `tool-bug`
* **Subsystem**: `targets/allwinner_e906/bsp/e906_ddr.ld`
* **Symptom**: Calling virtual methods on global C++ objects (such as `g_trace_transport.attach()`) caused unhandled traps prior to reaching `main()`.
* **Root Cause**: The linker script omitted `.init_array` and `.preinit_array` sections. The GCC C++ runtime never invoked static constructors, leaving vtable pointers uninitialized (`0x00000000`).
* **Resolution**: Added `.preinit_array` and `.init_array` sections to `e906_ddr.ld`.

### 7. Hardware Fact: Pure On-Chip SRAM Footprint (Zero DDR)
* **Category**: `hardware-fact`
* **Subsystem**: Memory Footprint, `SRAM_FAST`
* **Optimization**:
  * Stack sized to **8 KB** (`0x2000`), matching `mp_stack_set_limit(8192)`.
  * RemoteProc debugfs trace buffer sized to **1 KB** (`CONFIG_RPROC_TRACE0_LEN = 1024UL`) strictly for liveness (`[E906] Online`) and crash autopsies (`hal_crash_dispatcher`). All code and function tracing is routed over binary **barectf** CTF packets.
  * Entire firmware binary (166 KB text, 340 B data, 26 KB bss) fits 100% in SRAM Space 0 (`0x3FFC0000-0x3FFEEC8F`). **0 bytes of external DDR used.**

### 8. Verification Evidence: Live MicroPython REPL over RPMsg
* **Test**: Sent Python code into `/dev/rpmsg0` from Linux host:
  * `123 + 456` -> Returned `579`
  * `abstractx.alive()` -> Returned `True`
  * `abstractx.version()` -> Returned `'AbstractX 1.0.0 (E906 MicroPython REPL)'`
  * `hex(abstractx.peek(0x02500814))` (UART2 LSR) -> Returned `'0x0'`
  * `hex(abstractx.peek(0x02502410))` (TWI1 STAT) -> Returned `'0x0'`

### 9. Architectural Invariant: Strict Separation of Observability (`[SPEC-E906-REPL-07]`)
* **Category**: `process-gap`
* **Subsystem**: Observability, RemoteProc `trace0`, VirtIO RPMsg Endpoint 1025 (`abstractx-trace`)
* **Mandate**:
  * **RemoteProc `trace0`**: Limited strictly to liveness (`[E906] Online`) and crash autopsies (`hal_crash_dispatcher`). Zero general printf/string logging permitted.
  * **barectf CTF 1.8 Binary Code Tracing**: All driver, function, and coroutine execution instrumentation MUST use structured binary CTF 1.8 frames emitted via `barectf` over VirtIO RPMsg endpoint 1025.
* **Rationale**: Eliminates rodata string literal bloat and execution overhead, preserving 100% on-chip SRAM fit.

### 10. Reference Baseline: `cubie-a5e/firmware/riscv-firmware` Cross-Reference
* **Category**: `process-gap`
* **Subsystem**: Heterogeneous Co-Processor Architecture & HAL Drivers
* **Authoritative Reference Grounding**: All E906 hardware contracts and peripheral drivers in AbstractX are directly grounded in the tested reference implementations from `/home/tcmichals/projects/cubie/cubie-a5e/firmware/`:
  1. **Memory Map & Pure SRAM**: `tests.md` Profile 2 (`cubie-a5e-rpmsg-sram`), SRAM Space 0 (`0x3FFC0000`, 256 KB), SRAM Space 1 (`0x40000000`, 256 KB), DSP SRAM (`0x00020000`, 128 KB).
  2. **UART2 HAL**: `cubie-a5e/firmware/common/hal/uart.cpp` DesignWare 8250 init (`0x02500800`), baud divisor calculation, and FIFO configuration.
  3. **PIO Pinmux**: `cubie-a5e/firmware/common/hal/pio.cpp` PB0/PB1 configuration for UART2 (Mux 2, internal pull-up on PB1).
  4. **CCU Clock & Resets**: `cubie-a5e/firmware/common/hal/ccu.cpp` Allwinner sequence: de-assert reset `(1 << (16 + n))`, then gate clock `(1 << n)` (`CCU_UART_BGR_REG = 0x0200190C`, `CCU_TWI_BGR_REG = 0x0200191C`).
  5. **Crash Isolation & Autopsy**: `cubie-a5e/firmware/common/hal/crash.cpp` (`testCrash`) fatal `0xDEADF00D` trap signature and register dumping to `trace0`.
  6. **Telemetry & Tracing**: `testStringBinaryTrace0` & `testPingRpmsg` binary telemetry frames and RPMsg virtio transport over endpoints 1024/1025/1026.

### 11. Architecture: Interrupt Queueing, BareCTF Buffer Flush & FreeRTOS Baseline
* **Category**: `architectural-decision`
* **Subsystem**: Interrupt Dispatcher, BareCTF Trace Streaming, RTOS Model
* **Execution Flow Invariants**:
### 12. Toolchain / Process: E906 ELF Memory Analysis & Region Tracking Rule
* **Category**: `process-gap`
* **Subsystem**: Tooling, ELF Memory Analysis (`tools/analyze_elf_size.sh`, `tools/analyze_map.py`)
* **Actions Taken**:
  1. Ported `analyze_elf_size.sh` and `analyze_map.py` from `cubie-a5e/firmware/riscv-firmware/tools/` into `AbstractX/tools/`.
  2. Established AI Rule [`.agents/rules/e906-memory-tracking.md`](file:///home/tcmichals/projects/AbstractX/.agents/rules/e906-memory-tracking.md) mandating continuous verification of memory breakdowns and top symbol contributors on every build.

### 13. Architecture: Leveraging AbstractX Coroutine Channels (`AsyncQueue`)
* **Category**: `architectural-decision`
* **Subsystem**: Inter-Task Communication, Multi-Rate Sensor Ingestion, REPL Handoff
* **Mandated Pattern**: Use AbstractX `AsyncQueue<T, N>` cooperative lock-free SPSC channels ([`include/asp_coro.hpp`](file:///home/tcmichals/projects/AbstractX/include/asp_coro.hpp#L560)):
  1. **Primary-Paced Sensor Pipelines**: Dedicated producer tasks stream GPS fixes into `AsyncQueue<GpsFix, 8> g_gps_channel` and compass samples into `AsyncQueue<MagSample, 16> g_mag_channel`.
  2. **Zero Polling & Zero Prescalers**: Eliminates fragmented `if (++counter % 80 == 0)` state machines; control flow is 100% linear and awaitable (`co_await channel.pop()` or `while (channel.try_pop(sample))`).
  3. **MicroPython Bridge**: MicroPython scripts can ingest sensor streams non-blockingly via native bindings rather than directly polling MMIO registers.
  4. **REPL Input Stream**: Decouples incoming VirtIO RPMsg packets from bytecode execution without busy-spinning.

### 14. Hardware Fact: Platform Profile & Hardware Interconnect
* **Category**: `hardware-fact`
* **Subsystem**: Board Profile, SoC Peripherals, Pinmux & Clocking
* **Target Hardware**:
  * **SBC**: Radxa Cubie A5E (Allwinner A527 / T527)
  * **IP Address**: `192.168.3.3` (root access over Dropbear SSH)
  * **Co-Processor**: Alibaba T-Head XuanTie E906 RISC-V @ 200 MHz (RV32IMAFCX, single-precision FPU, CLIC interrupt controller)
  * **Host CPU**: Quad Cortex-A55 @ 1.4 GHz (Linux 5.15 / 6.6 with OpenAMP / RemoteProc)
* **Peripheral MMIO & Memory Map**:
  * `0x3FFC0000` - `0x3FFFFFFF` (192 KB): `SRAM_FAST` Space 0 (Host physical `0x07280000`)
  * `0x02000000`: PIO Controller (PB_CFG0 `0x0030`, PB_DAT `0x0040`, PB_PULL0 `0x0054`)
  * `0x02001000`: CCU Clock Controller (UART_BGR `0x090C`, TWI_BGR `0x091C`)
  * `0x02500800`: UART2 (DesignWare 8250 64-byte FIFO, 24 MHz APB clock)
  * `0x02502400`: TWI1 (Allwinner Two-Wire Interface / I2C1)
* **40-Pin Header Physical Pinout**:
  * **Pin 2 / 4**: 5V VCC (Red) -> Sensor VCC
  * **Pin 6 / 14**: GND (Black) -> Sensor GND
  * **Pin 3**: I2C1-SDA (Blue, PB5) -> Compass / u-blox DDC SDA (`0x02502400`)
  * **Pin 5**: I2C1-SCL (Orange, PB4) -> Compass / u-blox DDC SCL (`0x02502400`)
  * **Pin 11**: UART2-TX (Yellow, PB0) -> GPS RX (`0x02500800`)
  * **Pin 13**: UART2-RX (Green, PB1) -> GPS TX (`0x02500800`)

---

## 2026-10-18: Board Loading, RemoteProc Deployment & Script Execution

### 15. Process / Workflow: E906 Firmware Build & Deployment Procedure
* **Category**: `process-gap`
* **Subsystem**: Cross-Compilation, RemoteProc Firmware Loading, SSH Protocol
* **Standardized Build Command**:
  ```bash
  cmake -S . -B build_e906 -DABSTRACTX_TARGET=allwinner_e906 -DCMAKE_BUILD_TYPE=Release
  cmake --build build_e906 --target e906_repl_app -j
  ```
* **Memory Verification**:
  ```bash
  ./tools/analyze_elf_size.sh build_e906/apps/e906_repl_app/e906_repl_app.elf
  # Mandate: SRAM_FAST <= 196608 bytes (192 KB), DRAM = 0 bytes
  ```
* **Deployment to Board (`192.168.3.3`)**:
  * *Important*: Dropbear SSH lacks an SFTP subsystem (`sftp-server: not found`). Always use legacy SCP mode (`-O`):
  ```bash
  scp -O build_e906/apps/e906_repl_app/e906_repl_app.elf root@192.168.3.3:/lib/firmware/
  ```
* **RemoteProc Stop & Reload**:
  ```bash
  ssh root@192.168.3.3 "
    echo stop > /sys/class/remoteproc/remoteproc0/state 2>/dev/null || true
    echo e906_repl_app.elf > /sys/class/remoteproc/remoteproc0/firmware
    echo start > /sys/class/remoteproc/remoteproc0/state
    sleep 0.5
    cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
  "
  ```
* **Verification**:
  * RemoteProc state is `running`.
  * DebugFS `trace0` prints `[E906] Online`.
  * Character device `/dev/rpmsg0` appears and binds to RPMsg endpoint `1026` (`abstractx-repl`).

---

### 16. Process / Tooling: MicroPython Script Execution on E906 over RPMsg
* **Category**: `process-gap`
* **Subsystem**: `e906_workbench.py`, Headless REPL Execution, Raw REPL Mode
* **Interaction Methods**:
  1. **Headless Workbench Tool (`apps/e906_repl_app/linux/e906_workbench.py`)**:
     * Single inline expression:
       `python3 apps/e906_repl_app/linux/e906_workbench.py -c "import abstractx; print(abstractx.alive())"`
     * Register peek / poke directly from host:
       `python3 apps/e906_repl_app/linux/e906_workbench.py --peek 0x02500814`
     * Execute a script file:
       `python3 apps/e906_repl_app/linux/e906_workbench.py my_test_script.py`
  2. **Raw REPL Mode over `/dev/rpmsg0`**:
     * MicroPython raw REPL protocol bypasses character echoing and multiline auto-indent:
       - Send `\r\x03\x03\x01` (Ctrl-C, Ctrl-C, Ctrl-A) -> E906 responds `raw REPL; CTRL-B to exit\r\n>`
       - Send Python script content followed by `\x04` (Ctrl-D)
       - E906 executes and responds `OK<output>\x04\x04>`
       - Send `\x02` (Ctrl-B) to exit raw REPL back to friendly REPL.
  3. **Interactive Terminal Studio**:
     * Run `python3 apps/e906_repl_app/linux/e906_workbench.py` without arguments for the full-screen curses studio.

---

### 17. Process / Architecture: Co-Processor Peripheral Exclusivity vs. Host Linux
* **Category**: `process-gap`
* **Subsystem**: Heterogeneous Co-Processor Architecture & System Bus Integrity
* **Anti-Pattern Identified**: Running host Linux diagnostic utilities (e.g. `i2cdetect -y 1` or opening `/dev/ttyS2`) against co-processor buses.
* **Why Host Tools Fail & Cause Collisions**:
  1. In the active Device Tree, UART2 (`0x02500800`) is marked `status = "disabled"` for Linux so the XuanTie E906 has collision-free exclusive hardware ownership. Opening `/dev/ttyS2` fails or hits dummy unconfigured stubs.
  2. Linux I2C drivers (`mv64xxx_i2c`) assert asynchronous spinlocks, clock gating, and kernel interrupt handlers on the TWI bus controller. If both Linux and E906 touch TWI registers simultaneously, bus arbitration hangs and the E906 coroutines stall.
* **Mandated Architecture**:
  * **Linux Host**: Pure VirtIO RPMsg bridge and firmware loader (`remoteproc0`, `/dev/rpmsg0`). Never touch UART2, TWI1, or SPI from Linux userspace.
  * **E906 Co-Processor**: All bus initialization, baud rates, pinmux, and transactions MUST run on the E906 via C++20 coroutine drivers or native MicroPython bindings (`abstractx.uart_*`, `abstractx.i2c_*`).

---

### 18. Hardware Fact: Live On-Board Sensor Testing Findings (GPS Serial)
* **Category**: `hardware-fact`
* **Subsystem**: UART2 Hardware Bring-Up, MicroPython Native Module, u-blox GPS
* **Live Test Findings on Physical Board**:
  1. **E906 Driver & Register Parity**:
     * `abstractx.uart_init(baud)` successfully programmed CCU clock (`0x0200190C = 0x00050005`), PIO mux (`PB_CFG0 = 0xffffff22`), pull-up (`PB_PULL0 = 0x00100004`), and 8250 16x baud divisor.
     * `abstractx.uart_status()` returned `96` (`0x60`: THRE=1, TEMT=1), confirming transmitter FIFO is operational.
     * `abstractx.uart_write(b"$PUBX,00*33\r\n")` transmitted bytes out of PB0 (Pin 11).
  2. **Receiver Logic & Electrical State**:
     * We swept all standard baud rates (**9600, 38400, 57600, 115200, 4800**) via E906 MicroPython raw execution; all returned 0 bytes in the receive FIFO (`LSR = 0x60`, DR bit 0 is `0`).
     * Probed physical logic level of PB1 input: `PB_DAT` (`0x02000040`) read bit 1 as **`1` (HIGH)**, confirming the electrical idle level is present and the pin is not shorted to ground.
  3. **Root Cause Analysis & Physical Checks**:
     * Silence from the GPS module indicates the physical unit is either unpowered (needs 5V on Pin 2/4) or the TX/RX pair is inverted (PB0 Pin 11 to GPS RX, PB1 Pin 13 to GPS TX).

---

## 2026-10-18: Code Issues, Build/Make Failures, AbstractX Framework Friction & Workarounds

### 19. Tool / Build Bug: Linker Overflow with 64-bit LongLongs & The 31-bit MMIO Boundary
* **Category**: `tool-bug`
* **Subsystem**: `apps/e906_repl_app/include/mpconfigport.h`, `riscv-none-elf-ld`, `SRAM_FAST`
* **Symptom**:
  * Setting `#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_LONGLONG)` to allow 64-bit integer arithmetic caused the linker to fail with:
    `region SRAM_FAST overflowed by 2188 bytes`.
* **Root Cause**:
  * MicroPython's 64-bit long-long runtime pulls in multi-precision arithmetic routines (`__muldi3`, `__divdi3`, `__moddi3`) from libgcc, increases the size of integer object headers, and enlarges the bytecode VM evaluator.
  * Our XuanTie E906 execution budget in `SRAM_FAST` is strictly capped at **192 KB** (`0x3FFC0000 - 0x3FFFFFFF`).
* **AbstractX Code Issue**:
  * Reverting to `MICROPY_LONGINT_IMPL_NONE` limits MicroPython to 31-bit signed integers (maximum positive value `0x3FFFFFFF`).
  * However, Allwinner hardware MMIO registers frequently use bit 31 (e.g. CCU reset bits `(1 << 31)`, DDR registers at `0x48000000+`, or status bitmasks).
  * Calling `abstractx.peek(0x02500814)` worked, but calling `abstractx.peek(0x40000000)` threw `OverflowError: value does not fit in small int`.
* **How We Got Around It (Workaround & Fix)**:
  1. Kept `MICROPY_LONGINT_IMPL_NONE` to save 2.2 KB of precious SRAM.
  2. Implemented `abstractx.peek_hex(addr)`: parses the truncated int address, reads the hardware `volatile uint32_t *`, and formats the full 32-bit value into a string (`"0x%08x"`) directly in C without instantiating Python `int` objects.
  3. Built dedicated native C driver methods (`abstractx.uart_init`, `abstractx.uart_read`, `abstractx.uart_write`, `abstractx.uart_status`): all MMIO manipulation, baud rate divisors, and 32-bit masks execute in native C on the E906, shielding the MicroPython script layer from small int overflows.

---

### 20. AbstractX Framework Issue: MicroPython Blocking REPL vs Cooperative Coroutine Dispatcher
* **Category**: `product-bug`
* **Subsystem**: `abstractx::step()`, `coro::Task<void>`, `apps/e906_repl_app/src/main.cpp`
* **Symptom**:
  * When running standard MicroPython `pyexec_friendly_repl()`, the E906 core blocked completely waiting for console input.
  * AbstractX telemetry streams, barectf trace flushes, and background coroutines froze and never made progress.
* **Root Cause**:
  * MicroPython's reference REPL assumes a dedicated core running a blocking while-loop: `while (!exit) { int c = mp_hal_stdin_rx_chr(); ... }`.
  * In AbstractX, the execution model is strictly **cooperative coroutines** pumped by `abstractx::step()`. No coroutine is permitted to block or spin; all waiting must be non-blocking with cooperative yielding (`co_await`).
* **How We Got Around It (Workaround & Fix)**:
  1. Decoupled character reception from bytecode execution: `RpmsgConsole` buffers incoming characters into an internal lock-free ring buffer `m_rx_fifo`.
  2. Built an event-driven `repl_coroutine()` in `main.cpp` that calls `m_console.try_read_char(ch)` non-blockingly.
  3. When no characters are pending, the coroutine cooperatively yields execution via `co_await abstractx::step_async()`, returning control to `abstractx::step()` to pump telemetry, flush barectf buffers, and process other system tasks.

---

### 21. AbstractX Framework Issue: Doorbell Awaiter Starvation During Dynamic VirtIO Bring-Up
* **Category**: `product-bug`
* **Subsystem**: `hal::Rpmsg`, `apps/e906_repl_app/src/rpmsg_console.cpp`, MSGBOX IRQ 17
* **Symptom**:
  * Writing commands into `/dev/rpmsg0` from Linux yielded no output; `repl_coroutine` appeared completely starved.
* **Root Cause**:
  * `repl_coroutine` originally used an awaiter `co_await console.read_char_async()`, which registered a coroutine handle to be resumed by `fc_msgbox_doorbell_isr()`.
  * However, during board bring-up, the Linux kernel VirtIO handshake (`sunxi-rproc`) creates `/dev/rpmsg0` dynamically *after* the E906 has already booted. The initial doorbell interrupt fired before the awaiter was suspended, or subsequent doorbells were coalesced by the hardware mailbox. Because the interrupt was edge-triggered and not level-latched, the awaiter slept forever.
* **How We Got Around It (Workaround & Fix)**:
  1. Eliminated the edge-triggered doorbell awaiter on character reception.
  2. Integrated `abstractx::hal::g_rpmsg.poll()` directly into the primary cooperative event loop in `main.cpp`.
  3. Now, whenever Linux pushes a VirtIO buffer into shared SRAM, `g_rpmsg.poll()` harvests the characters immediately and wakes the REPL pipeline without relying on edge-triggered interrupt timing.

---

### 22. Toolchain / ABI Issue: MicroPython NLR (setjmp/longjmp) vs Freestanding C++20 Coroutine Frames
* **Category**: `tool-bug`
* **Subsystem**: MicroPython Runtime (`py/nlr.h`), Freestanding C++20 (`-fno-exceptions`)
* **Symptom**:
  * Syntax errors or runtime exceptions (e.g. `NameError: name not defined`) entered into the MicroPython REPL occasionally resulted in corrupted coroutine frame memory or illegal instruction traps (`0xDEADF00D`).
* **Root Cause**:
  * MicroPython handles Python-level exceptions using Non-Local Returns (NLR), implemented via assembly `setjmp` / `longjmp` (`nlr_push` / `nlr_jump`).
  * In freestanding C++20 compiled with `-fno-exceptions`, `longjmp` bypasses the C++ stack unwinding and coroutine promise mechanisms. If a Python exception jumps across an active C++ coroutine boundary, compiler-allocated coroutine frames and local RAII wrappers are skipped, corrupting the heap/allocator state.
* **How We Got Around It (Workaround & Fix)**:
  1. Established a strict **NLR C-Boundary Isolation**: All MicroPython parser, compiler, and VM execution calls (`mp_parse_compile_execute`, `qstr_init`, `mp_init`) are wrapped within explicit `nlr_push(&nlr)` blocks inside pure `extern "C"` functions in `mphalport.c` and `modabstractx.c`.
  2. Python exceptions are caught and converted to friendly error strings *within the C runtime*, ensuring that `nlr_jump` NEVER crosses a C++ coroutine suspension point or unwinds past a C++ promise frame.

---

### 23. Make / Linker Bug: Missing `.init_array` Causing Static Constructor Death
* **Category**: `tool-bug`
* **Subsystem**: `targets/allwinner_e906/bsp/e906_ddr.ld`, GCC C++ Runtime
* **Symptom**:
  * The firmware immediately hard-faulted before entering `main()` whenever any global object with a virtual table or constructor (such as `g_trace_transport`) was instantiated.
* **Root Cause**:
  * The bare-metal linker script `e906_ddr.ld` had output sections for `.text`, `.rodata`, `.data`, and `.bss`, but completely lacked `.init_array` and `.preinit_array`.
  * The GNU cross-toolchain (`riscv-none-elf-gcc`) places static constructor function pointers in `.init_array`. Without it, `__libc_init_array()` did nothing, leaving vtables initialized to `0x00000000`.
* **How We Got Around It (Workaround & Fix)**:
  * Updated `e906_ddr.ld` to map `.preinit_array` and `.init_array` into the `.text` segment with proper `__init_array_start` and `__init_array_end` symbols:
    ```ld
    .init_array : {
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array*))
        PROVIDE_HIDDEN (__init_array_end = .);
    } > SRAM_FAST
    ```

---

### 24. Process / Observability Issue: String Literal Bloat in `trace0` vs Zero-Copy BareCTF
* **Category**: `process-gap`
* **Subsystem**: `targets/allwinner_e906/src/trace.cpp`, `[SPEC-E906-REPL-07]`
* **Symptom**:
  * The binary size of `e906_repl_app.elf` grew to 189 KB, leaving under 3 KB of SRAM headroom in `SRAM_FAST` (192 KB).
* **Root Cause**:
  * Early implementations used `snprintf` and descriptive ASCII log messages to format diagnostic events into the 1 KB RemoteProc `trace0` debugfs buffer.
  * Formatting strings pulled in `vfprintf` from newlib-nano, formatting lookup tables, and dozens of string constants, adding ~14 KB of `.rodata` and `.text` bloat.
* **How We Got Around It (Workaround & Fix)**:
  1. Codified `[SPEC-E906-REPL-07]`: Restricted `trace0` exclusively to boot liveness (`[E906] Online\n`) and crash autopsies (`hal_crash_dispatcher`).
  2. Migrated all code tracing, driver instrumentation, and telemetry to binary **barectf** CTF 1.8 packets emitted over VirtIO RPMsg endpoint 1025.
  3. Binary tracing requires zero string formatting code in the firmware, slashing code size and preserving 100% on-chip SRAM execution.

---


### 25. CMake / Build Issue: Target Cache Contamination in Cross-Compilation
* **Category**: `tool-bug`
* **Subsystem**: CMake Build System, Cross-Compilation Toolchains
* **Symptom**:
  * Switching between Desktop SITL (`-DABSTRACTX_TARGET=host`) and XuanTie E906 (`-DABSTRACTX_TARGET=allwinner_e906`) within the same build directory caused CMake to mix host headers (`/usr/include`) with RISC-V bare-metal headers, leading to cryptic compilation errors.
* **How We Got Around It (Workaround & Fix)**:
  1. Enforced strictly segregated build directories for every silicon target:
     - `build_host` for Native Linux SITL
     - `build_e906` for XuanTie E906 bare-metal
     - `build_pico` for Raspberry Pi Pico 2 W
  2. Standardized toolchain invocation using the canonical toolchain paths in `~/.tools/xpack-riscv-none-elf-gcc-*/bin/riscv-none-elf-` with `--specs=nano.specs` and `-nostartfiles`.

---

### 26. Hardware Fact / Tooling: Linux Pinctrl DT Overlay Invariant & Bus Diagnostics
* **Category**: `hardware-fact` / `tool-bug`
* **Subsystem**: Linux Device Tree Overlays, Allwinner A523/A527 Pinctrl, Serial/I2C Bring-up
* **Symptom**:
  * An overlay attempting to enable `uart2` on `PB0`/`PB1` and `i2c1` on `PB6`/`PB7` failed at boot:
    `sun55i-a523-pinctrl 2000000.pinctrl: /soc/pinctrl@2000000/uart2-pb-pins: invalid mux value for pin PB0`
    `sun55i-a523-pinctrl 2000000.pinctrl: unsupported function uart2 on pin PB0`
* **Root Cause**:
  * In the Linux kernel driver `drivers/pinctrl/sunxi/pinctrl-sunxi-dt.c` (`sunxi_pinctrl_dt_read_pinmux`), dynamic pin group nodes in DT overlays MUST contain the numeric property `allwinner,pinmux = <val>`.
  * Without this explicit property, the parser flags `INVALID_MUX` and refuses to bind the pins.
* **Resolution**:
  * Updated overlay with explicit numeric mux values:
    - UART2 (`PB0`, `PB1`): `allwinner,pinmux = <2>;` with `bias-pull-up;`
    - I2C1 (`PB6`, `PB7`): `allwinner,pinmux = <7>;` with `bias-pull-up;`
  * Confirmed in hardware: `PB_CFG0` (`0x02000030`) reads `0x77FFFF22` and `PB_PULL0` (`0x02000054`) reads `0x00105005`.
* **Hardware Wiring & Bus Rules**:
  * **UART Crossover**: GPS TX (Yellow, 9600 baud) connects to Cubie RX (`PB1` / Pin 13); GPS RX (Green) connects to Cubie TX (`PB0` / Pin 11). Connecting TX-to-TX prevents reception.

---

### 27. Architecture: Bidirectional AbstractX Coroutine Channels (`AsyncQueue`) for MicroPython REPL
* **Category**: `architectural-decision`
* **Subsystem**: MicroPython REPL Runtime, RPMsg Endpoint 1026, Coroutine Dispatcher
* **Architecture**:
  1. **Doorbell Interrupt Decoupling**:
     - `fc_msgbox_doorbell_isr()` clears hardware status and enqueues `s_rpmsg_coroutine_handle` into `abstractx::IsrDispatcher` (zero coroutine resumption in ISR context).
     - `abstractx::step()` cooperatively resumes `rpmsg_rx_coroutine()`.
  2. **Incoming REPL Channel (`m_rx_channel`)**:
     - `rpmsg_rx_coroutine()` polls the vring and delivers packets to `RpmsgConsole::on_rx_data()`.
     - Characters are pushed into `abstractx::coro::AsyncQueue<char, 512> m_rx_channel`.
     - The REPL input task `repl_rx_coroutine()` cooperatively awaits incoming characters via `co_await console.read_char_async()`, consuming 0 CPU cycles while idle.
     - Long-running Python loops continue non-preemptively checking for Ctrl-C (`0x03`) via non-destructive `m_rx_channel.try_peek()`.
  3. **Outgoing REPL Channel (`m_tx_channel`)**:
     - MicroPython stdout emissions (`mp_hal_stdout_tx_strn()`) push characters into `abstractx::coro::AsyncQueue<char, 256> m_tx_channel`.
     - Dedicated `repl_tx_coroutine()` cooperatively awaits `co_await console.tx_channel().pop()`.
     - When characters arrive, `repl_tx_coroutine()` opportunistically batches all pending characters in the channel up to buffer capacity (`256 B`) and flushes an RPMsg frame back to the Linux host over endpoint 1026.
  4. **Benefits**:
     - 100% stackless C++20 coroutine execution across the entire pipeline.
     - Zero busy-spinning or cooperative polling (`step_async()`) when idle.
     - Perfect backpressure and decoupling between Linux VirtIO transport and the MicroPython bytecode engine.









