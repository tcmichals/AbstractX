# AbstractX Resource Lifecycle: Open & Close Semantics (Python & C++20)

**Document:** `docs/tier2_contracts/LIFECYCLE_OPEN_CLOSE_CONTRACT.md`  
**Governing Requirements:** `[SPEC-ARCH-01]`, `[SPEC-ARCH-08]`, `[SPEC-E906-REPL-03]`, `[SPEC-E906-REPL-12]`  
**Status:** Authoritative Foundation (Tier 2 SSOT)

---

## 1. Architectural Overview & The Open/Close Duality

In AbstractX, resource acquisition and release follow a strict **Dual-Domain Execution Model**:

1. **Host Linux Domain (Python & POSIX)**:
   - Linux user-space utilities (`run_e906_script.py`, `e906_rpmsg_bridge.py`, `abstractx_studio.py`) operate on a rich POSIX operating system.
   - Resource access follows standard POSIX `open()` and `close()` system calls (`os.open()`, `open()`, `os.close()`) for dynamic character devices (`/dev/rpmsg_ctrl0`, `/dev/rpmsgN`), sockets, and file descriptors.
2. **Freestanding Target Domain (Bare-Metal C++20 & MicroPython)**:
   - Firmware running on dual-core microcontrollers (RP2350, ESP32-P4) and coprocessors (XuanTie E906) operates in **freestanding C++20 with 0 dynamic heap and zero filesystem**.
   - Traditional POSIX `open()` and `close()` **do not exist**: calling `open()` in MicroPython raises `NameError: name 'open' is not defined`.
   - Instead, resource lifecycle is managed via **Static RAII Initialization, Channel Lifetime Binding, and Soft-Reset VM Teardown**.

---

## 2. Resource Lifecycle Matrix

| Layer / Context | "Open" Primitive | "Close" Primitive | Memory & Resource Contract |
| :--- | :--- | :--- | :--- |
| **Linux Host (Python)** | `os.open("/dev/rpmsgN", O_RDWR)` | `os.close(fd)` | Allocates and frees kernel VFS file descriptors and RPMsg endpoint structures. |
| **Linux Host (Bridge)** | `ioctl(RPMSG_CREATE_EPT_IOCTL)` | `os.close(fd)` | Dynamically creates `/dev/rpmsgN` device nodes; closing frees kernel buffer pools. |
| **AbstractX C++ HAL** | `co_await dev.init_async()` | `dev.deinit()` / Static Lifetime | Zero heap; hardware clocks enabled and pinmuxed; buffers statically pooled in `.bss` / SRAM Space 0. |
| **AbstractX SPSC Channels** | `AsyncQueue<T, N>` Constructor | Never closed (Static Lifetime) | Fixed circular ring buffers in SRAM; never allocated with `malloc()` or released with `free()`. |
| **MicroPython Runtime** | `mp_init()` + `pyexec_event_repl_init()` | `mp_deinit()` (Ctrl-D Soft Reset) | Quarantined in HiFi4 DSP SRAM (`0x00020000`). Soft-reset sweeps all Python objects and resets heap. |
| **MicroPython Peripherals** | `abstractx.uart_init(baud)` | VM Soft Reset / Re-init | Directly programs MMIO clock dividers, resets FIFO, and enables bus controllers. |
| **MicroPython File I/O** | **Prohibited (NameError)** | **Prohibited** | **Zero Filesystem**: Code is streamed into memory over RPMsg (`/dev/rpmsg0`). No disk or VFS blocks. |

---

## 3. Host-Side Open & Close Flow (Linux Python)

```mermaid
sequenceDiagram
    autonumber
    participant PY as Python Script (run_e906_script.py)
    participant CTRL as Kernel /dev/rpmsg_ctrl0
    participant DEV as Kernel /dev/rpmsgN
    participant E906 as XuanTie E906 Coprocessor

    Note over PY,CTRL: 1. Endpoint Creation ("Open")
    PY->>CTRL: open("/dev/rpmsg_ctrl0", O_RDWR)
    PY->>CTRL: ioctl(RPMSG_CREATE_EPT_IOCTL, name="abstractx-repl", dst=1026)
    CTRL-->>DEV: Remoteproc creates /dev/rpmsg0 node
    PY->>CTRL: close(ctrl_fd)

    Note over PY,DEV: 2. Channel Binding & Handshake
    PY->>DEV: open("/dev/rpmsg0", O_RDWR | O_NONBLOCK)
    PY->>DEV: write(b"\x03\x03\x02\x01") (Ctrl-C reset + Ctrl-A Raw REPL)
    DEV->>E906: First packet arrives; E906 learns host_addr = msg.src

    Note over PY,E906: 3. Payload Streaming
    PY->>DEV: write(payload_256B_chunks) + write(b"\x04") (Ctrl-D Execute)
    E906->>DEV: MicroPython VM executes in SRAM and streams stdout
    PY->>DEV: read() until b"\x04" end-of-execution marker

    Note over PY,DEV: 4. Channel Teardown ("Close")
    PY->>DEV: write(b"\x02") (Ctrl-B Exit Raw REPL)
    PY->>DEV: close(rpmsg_fd)
    DEV-->>CTRL: Kernel remoteproc tears down endpoint and frees buffers
```

---

## 4. Target-Side Open & Close Semantics (Bare-Metal XuanTie E906)

### 4.1 The Zero-Filesystem Invariant
On bare-metal RISC-V targets running AbstractX:
* `MICROPY_CONFIG_ROM_LEVEL` is set to `MICROPY_CONFIG_ROM_LEVEL_MINIMUM`.
* `MICROPY_VFS` (Virtual File System) is disabled (`0`).
* MCU SRAM Space 0 (`0x3FFC0000`, 192 KB) is strictly reserved for C++ flight code, coroutine static frame pools, and SPSC rings.
* Python scripts are **never stored on disk or flash**. They are transferred directly over VirtIO RPMsg text buffers into RAM and compiled on-the-fly into bytecode.
* Attempting to call `open("file.txt", "r")` in MicroPython fails immediately with `NameError: name 'open' is not defined`.

### 4.2 Peripheral Acquisition ("Open")
Peripherals (such as UART3 for GPS or TWI1 for Magnetometer) are opened and configured through native `abstractx` functions:
```python
import abstractx

# "Open" UART3 @ 115200 baud:
# Enables clock gate in CCU, sets PI11/PI12 mux, configures baud divisor 13
abstractx.uart_init(115200)

# "Open" TWI1 @ 400 kHz:
# Deasserts reset, configures PB4/PB5 mux, sets CCR clock divisor 0x12, enables bus
abstractx.i2c_init(400000)
```

### 4.3 Peripheral Teardown & Soft-Reset ("Close")
In embedded flight loops, peripherals are long-lived and operate continuously. When a script completes or crashes:
1. **Interrupt Char (`Ctrl-C`, `0x03`)**:
   `MICROPY_VM_HOOK_LOOP` intercepts `0x03` in the input ring and executes `mp_sched_keyboard_interrupt()`, immediately unwinding execution back to the REPL prompt without leaking locks.
2. **Soft Reset (`Ctrl-D`, `0x04`)**:
   The host sends Ctrl-D. The C++ wrapper calls:
   ```cpp
   mp_deinit();                 // Sweeps and destroys all Python objects
   gc_init(heap_start, heap_end); // Resets quarantined DSP SRAM allocator
   mp_init();                  // Reinitializes clean Python VM
   pyexec_event_repl_init();   // Restores clean REPL state
   ```
   All Python-allocated memory is reclaimed in $O(1)$ without affecting the C++ flight loop or SPSC rings.

---

## 5. C++ Driver Lifecycle Contract (`init_async` / RAII)

In the AbstractX C++20 runtime:
* Public HAL interfaces (`ISpi`, `II2c`, `IUart`) do not use POSIX file descriptors.
* Drivers are instantiated statically with zero dynamic heap allocation:
  ```cpp
  // Static instantiation (Zero Heap)
  static hal::UbloxGps s_gps(s_uart);
  ```
* Acquisition follows the **Asynchronous Coroutine Contract**:
  ```cpp
  // "Open" driver asynchronously
  bool ok = co_await s_gps.init_async();
  if (!ok) {
      // Diagnostic fault reporting
  }
  ```
* Scoped critical sections use RAII locks that automatically restore interrupt states on destruction (`InterruptLock`):
  ```cpp
  {
      abstractx::InterruptLock lock;
      // Critical section: interrupts masked
  } // Automatically restored upon exiting scope ("closed")
  ```
