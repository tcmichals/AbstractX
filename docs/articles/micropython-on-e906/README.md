# Experiment First, Compile Later: Python on Both Processors of the Allwinner T527

## Part 5 of *Bringing Up Heterogeneous RISC-V on Allwinner SoCs* (Radxa Cubie A5E)

**One Cubie A5E, two kinds of processor, Python on both: check out new hardware and run experiments in seconds, with no compile in the loop.**

The Radxa Cubie A5E is a small board with a surprise inside. Its Allwinner
T527 runs Linux on eight ARM Cortex-A55 cores, and next to them sits a 200 MHz
XuanTie E906 RISC-V core that Linux never uses. In this part we put MicroPython
on that RISC-V core and drive it from Python on Linux. You type a line, the E906
reads a compass over I²C or a GPS over a UART, and the answer comes back over
the link between the two processors. No firmware rebuild, no reflash, no reboot
between experiments. While it runs, **AbstractX Studio**—our native desktop engineering workbench running on your PC—provides real-time visibility into the coprocessor: CPU utilization, per-interrupt microsecond costs, line-by-line source profiling, and an interactive prompt, all without stalling hard real-time execution.

![AbstractX Studio's CPU tab, live: busy percentage, the coroutine table and per-interrupt costs updating once a second from the E906](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/studio-cpu.gif)

*Live from the board: AbstractX Studio displays the E906's real-time heartbeat. Once a second it updates CPU load, per-task execution percentages, and exact per-interrupt microsecond costs using sub-microsecond non-intrusive binary telemetry.*

<!-- PHOTO: the Cubie A5E test bench (u-blox GPS on UART3, HMC5883L compass on I2C1, wiring to the
40-pin header). Upload to Hackster and place it here. -->

### In short

1. **One chip, two worlds.** Eight Cortex-A55 cores run Linux: networking,
   files, a full Python. One small RISC-V core, the E906, is free for
   real-time work: sensors, timing, control loops.
2. **The device tree decides who owns which pins.** A one-line setting in
   `/boot/config.txt` makes U-Boot hand the GPS UART, the compass I²C bus and
   some DMA channels to the E906 instead of Linux.
3. **Linux starts the E906** by loading a firmware file, and the two talk by
   passing messages through shared memory, with a hardware doorbell to say
   "look now". That message system is called **RPMsg**.
4. **AbstractX** is the firmware on the E906: a small event loop with
   interrupt-driven drivers. **MicroPython** runs inside it, so the E906 can run
   Python you send it.
5. **Python on Linux talks to Python on the E906.** A 20-line Linux script can
   run code on the E906 or read its CPU load. **AbstractX Studio**, a dedicated
   desktop engineering workbench running on your PC, gives you an interactive
   REPL and live silicon observability instruments (bare-metal `top`, `perf`,
   and a software logic analyzer timeline).
6. **The point is speed of learning:** try the hardware in Python, measure what
   it costs, and only then write the fast version in C++ on the same drivers.

### Where this fits in the series

![Allwinner T527 RISC-V Firmware & MicroPython Series](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/table1-series-roadmap.svg)

You do not need the earlier parts to follow this one: each section below
starts from what you need to know and points back to the part that goes deeper.

---

## 1. The board and what we hang on it

The Cubie A5E's Allwinner T527 combines:

![Hardware Execution Domains & Peripheral Partitioning](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/table2-architecture-blocks.svg)

On our test bench two sensors are wired to the 40-pin header:

- a **u-blox NEO-M8N GPS** on UART3 (pins PI11 / PI12), and
- an **HMC5883L compass** on I²C1, also called TWI1 (pins PB4 / PB5).

Part 1 explains why a core like the E906 is worth having next to Linux:
deterministic timing, offloading the A55s, and isolating real-time work from
everything Linux does. This part is about making that core quick to
experiment with.

## 2. Who owns which pins: the device tree and U-Boot

Linux does not probe the hardware to find out what is on the board; it reads a
**device tree**, a data file that lists every peripheral, its registers, its
interrupt, its clock and which driver should use it. The board's base device
tree gives everything to Linux.

To give UART3 and I²C1 to the E906 instead, we do not edit that base file. We
apply an **overlay**: a small patch file (`.dtbo`) that changes a few nodes. The
[device-tree overlay article](https://github.com/tcmichals/cubie-a5e/blob/main/docs/articles/devicetreeOverlay.md)
explains the mechanism in depth. In short:

1. The boot partition holds a plain text file, `/boot/config.txt`.
2. U-Boot runs a boot script (`boot.scr`, compiled from `boot.cmd`) that reads
   `config.txt`, loads the base device tree into RAM, and applies every overlay
   listed on the `dtoverlay=` line, one after another.
3. Linux boots with the merged tree and never knows overlays were involved.

On our board `config.txt` contains:

```text
dtoverlay=cubie-a5e-flight-stack-e906
```

That overlay
([`cubie-a5e-flight-stack-e906.dtso`](https://github.com/tcmichals/AbstractX/blob/main/apps/e906_repl_app/cubie-a5e-flight-stack-e906.dtso))
does four things:

- **UART3, I²C1 and SPI1 go to `generic-uio`.** Linux still sets the pin
  multiplexing, turns on the clocks and releases the resets, but it attaches no
  driver, so the E906 has the registers to itself.
- **The message box and `remoteproc` are enabled**, with the firmware name
  `riscv-firmware.elf` and the memory region for the shared rings.
- **The DMA controller is split:** `dma-channels = <8>` tells Linux it has only
  channels 0-7, so channels 8-15 are free for the E906 (section 10).
- **Pins are documented in the file**, header pin by header pin.

Changing the hardware split is therefore an edit, a compile and a reboot:

```bash
# PC: compile the overlay (dtc from a kernel build tree)
dtc -@ -I dts -O dtb -o cubie-a5e-flight-stack-e906.dtbo cubie-a5e-flight-stack-e906.dtso
# Board: install it and reboot
scp -O cubie-a5e-flight-stack-e906.dtbo root@192.168.3.3:/boot/
ssh root@192.168.3.3 reboot
```

This week's DMA change was exactly that: one line, `dma-channels = <8>`, and
after the reboot `/proc/device-tree/soc/dma-controller@3002000/dma-channels`
read 8 instead of 16. There is also a `cubie-a5e-flight-stack-linux` overlay
that gives the same pins back to Linux drivers (`/dev/ttyAS3`, `/dev/i2c-1`),
so you can switch between "Linux owns the sensors" and "the E906 owns the
sensors" with one line in `config.txt`.

## 3. Starting the E906 from Linux: `remoteproc`

The E906 has no flash of its own. Linux loads its firmware into the on-chip
SRAM and starts it, using the kernel's **remoteproc** framework (built and
explained in Part 2). The firmware is an ordinary ELF file:

```bash
ls -l /lib/firmware/riscv-firmware.elf          # a link to the firmware you want
echo stop  > /sys/class/remoteproc/remoteproc0/state
echo start > /sys/class/remoteproc/remoteproc0/state
cat /sys/class/remoteproc/remoteproc0/state     # running
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0   # the E906's boot log
```

Inside the ELF is a small **resource table** that tells Linux what the firmware
needs: where its boot log lives (`trace0`) and that it wants an RPMsg link.
Swapping firmware is a copy and a stop/start; Linux keeps running the whole
time. On our board an init script starts the E906 at boot.

## 4. How Linux and the E906 talk: RPMsg from the ground up

Two processors that share memory still need rules to exchange data. RPMsg is
built in layers, and each layer solves one problem:

1. **Shared memory.** Both processors can read and write the upper 128 KB of
   SRAM Space 1. That is where messages live. But shared memory alone cannot
   tell the other side that something new arrived.
2. **A doorbell.** The **message box** is a small hardware mailbox: writing to
   it raises an interrupt on the other processor (on the E906, interrupt line
   17). Now nobody has to poll memory; the doorbell says "look now". Part 3
   measures this layer on its own.
3. **Rings of buffers.** Shared memory is organised as two **VirtIO rings**
   (vrings), one per direction. Each ring holds a fixed number of 512-byte
   buffers. The sender takes a free buffer, fills it, puts it in the ring and
   rings the doorbell; the receiver takes it, uses it, and hands it back.
4. **Messages with addresses.** Each buffer starts with a 16-byte RPMsg header
   (source address, destination address, length) followed by up to 496 bytes
   of data. One buffer is one message.
5. **Endpoints with names.** The destination address picks an **endpoint** on
   the other side, like a port number. When the E906 firmware starts, it
   announces its endpoints by name, and Linux creates a channel for each.
6. **Files on Linux.** Linux's `rpmsg_char` driver gives each endpoint a
   device file, `/dev/rpmsgN`. To your program the E906 is now a file: `write()`
   sends one message, `read()` returns one message.

![How one RPMsg message travels: a Python write() on Linux fills a 512-byte buffer in shared SRAM, the message box doorbell interrupts the E906, and AbstractX delivers the message to endpoint 1026, 1024 or 1025](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/rpmsg-ground-up.png)

*Follow the numbers: (1) your write fills a buffer in ring 1, (2) the message box rings the E906's doorbell, (3) the E906 takes the buffer and hands it to the endpoint named in the header, (4) replies come back the same way in ring 2.*

### Three streams on top of messages

RPMsg carries **messages**, not a continuous byte stream. Each message is at
most 496 bytes and arrives whole. Our firmware builds three different
**streams** on top of that, one per endpoint, so the three kinds of traffic
never mix:

![Shared SRAM Virtual Endpoints & Wire Protocols](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/table3-rpmsg-endpoints.svg)

Linux creates the `/dev/rpmsgN` file for an endpoint when a program asks for
it, through `/dev/rpmsg_ctrl0`. The numbers depend on the order, so tools find
"their" endpoint by name rather than by number.

### Two rules that keep the link healthy

Buffers are a shared, limited resource, and Linux never tells the E906 that a
reader went away. Two rules came out of that (the story is in
[section 12](#12-what-went-wrong)):

- **The trace stream needs a lease.** The E906 sends trace packets only while
  a reader has said hello in the last 3 seconds. Readers send a 4-byte hello
  when they open the endpoint and then once a second.
- **Long-running scripts need a heartbeat.** A streaming script expects a
  `PING` line at least once a second and stops itself after 3 seconds without
  one, so a closed PC tool never leaves the E906 talking to nobody.

### Try it: read the E906's CPU load from Linux in Python

This script runs on the board, under Linux, next to two files from the repo:
[`e906_rpmsg_bridge.py`](https://github.com/tcmichals/AbstractX/blob/main/tools/e906_rpmsg_bridge.py)
(finds or creates an endpoint) and
[`runtime_stats.py`](https://github.com/tcmichals/AbstractX/blob/main/tools/visualizer/runtime_stats.py)
(decodes the status report). It opens the trace endpoint, keeps the lease alive
and prints the E906's once-a-second report:

```python
# e906_load.py: the E906's CPU load, read from Linux
import os, time
from e906_rpmsg_bridge import ENDPOINTS, HELLO, create_endpoint
import runtime_stats as rs

ep = ENDPOINTS["trace"]                                   # 'abstractx-trace', 1025
fd = os.open(create_endpoint("/dev/rpmsg_ctrl0", ep["name"], ep["addr"]), os.O_RDWR)
os.write(fd, HELLO)                      # "I'm listening": starts the 3 s lease
prev, last_hello = None, time.monotonic()
while True:
    msg = os.read(fd, 512)               # one RPMsg message per read
    if time.monotonic() - last_hello > 1:
        os.write(fd, HELLO); last_hello = time.monotonic()
    report = rs.decode(msg)              # None unless it is a status report
    if report:
        r = rs.rates(prev, report) if prev else None
        if r:
            print(f"busy {r['busy_pct']:.1f}%",
                  {t['name']: round(t['cpu_pct'], 1) for t in r['tasks']},
                  {d['name']: round(d['irq_per_s']) for d in r['drivers']})
        prev = report
```

On our board, with the E906 idle and the GPS sending:

```text
busy 0.3% {'rpmsg_rx': 0.0, 'repl_rx': 0.0, 'repl_tx': 0.0} {'timer': 1001, 'uart3': 30, 'msgbox': 1, 'twi1': 0, 'irq88': 0}
busy 0.4% {'rpmsg_rx': 0.0, 'repl_rx': 0.0, 'repl_tx': 0.0} {'timer': 1001, 'uart3': 30, 'msgbox': 1, 'twi1': 0, 'irq88': 0}
```

The E906 is 0.3 % busy, the timer interrupts 1,001 times a second (the
profiler's sample clock, section 7), and UART3 takes about 30 interrupts a
second from the GPS. No compile happened anywhere: a Python script on Linux is
reading the E906 firmware's own counters, live, from the other processor.

## 5. MicroPython on the E906, with AbstractX underneath

![What runs where: Studio on the PC, bridges on Linux, the AbstractX event loop with MicroPython as one coroutine on the E906](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/what-runs-where.png)

*The three columns are three separate machines as far as software is concerned: a PC running AbstractX Studio, Linux on the Cortex-A55 cores, and the E906 coprocessor.*

Notice how the three tiers divide the engineering labor:
1. **The Development PC**: Runs **AbstractX Studio**, rendering rich 60 FPS Dear ImGui observability instruments, decoding binary telemetry, and parsing DWARF ELF symbols so the embedded silicon never wastes compute or SRAM on GUI rendering or string formatting.
2. **The Cortex-A55 Linux Cores**: Act as a transparent high-bandwidth bridge, routing RPMsg message streams and binary trace buffers over SSH or network sockets to your PC.
3. **The XuanTie E906 RISC-V Core**: Runs the lightweight, freestanding AbstractX event loop and MicroPython, driving I²C and UART hardware peripherals in real time.

The firmware on the E906 is
[AbstractX](https://github.com/tcmichals/AbstractX): an event loop that runs
small cooperative tasks (C++20 coroutines, Part 4) and interrupt-driven
drivers, with no heap. This firmware
([`apps/e906_repl_app`](https://github.com/tcmichals/AbstractX/tree/main/apps/e906_repl_app))
runs three tasks:

- `rpmsg_rx`: receives RPMsg messages from Linux,
- `repl_rx`: **MicroPython**, which runs whatever you send it, and
- `repl_tx`: sends MicroPython's output back.

MicroPython is not the firmware; it is one task inside it. That has three
consequences you will see in the experiments:

- **Interrupts keep running under Python.** The GPS UART and the compass I²C
  bus are handled by AbstractX interrupt handlers. When Python waits for the
  bus, the E906 sleeps (`wfi`) until the interrupt wakes it; it never spins.
- **Python cannot break the firmware's memory.** MicroPython's heap lives in
  the RAM of the chip's unused HiFi4 DSP (128 KB), separate from the firmware.
- **Python's cost can be measured.** AbstractX counts CPU time per task and per
  interrupt, so the price of running Python shows up as the `repl_rx` task.

Scripts reach the hardware through one built-in module, `abstractx`
([`modabstractx.c`](https://github.com/tcmichals/AbstractX/blob/main/apps/e906_repl_app/src/modabstractx.c)):

![Hardware Peripheral & Telemetry C-Binding Functions](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/table4-modabstractx-api.svg)

The board's MicroPython is a minimal build: `bytes.decode` and `bytes.fromhex`
do not exist (use `str(b, "ascii")` and `int(h, 16)`), and there is no
filesystem, so scripts arrive over RPMsg and are compiled in memory.

### 5.1 Concurrency in Python: Why AbstractX Coroutines Instead of `uasyncio`?

A natural question when looking at concurrent I/O in Python is: **"Python has `asyncio` (and MicroPython has `uasyncio`), so why aren't we using that? Is `asyncio` flawed? Or did `uasyncio` miss what microcontrollers actually need?"**

#### Is Python's `asyncio` Flawed?
**No, `asyncio` is not flawed for its intended domain.**

Python's `asyncio` was designed for network applications—web servers (FastAPI, aiohttp), database connection pools, and microservices on Linux and cloud servers with gigabytes of RAM. In that domain:
- Waiting 10–50 ms for an HTTP response or a database query over TCP is typical.
- Allocating 500 bytes of memory per `Task` is completely unnoticeable.
- A 50-microsecond garbage collection pause causes no issues whatsoever.

#### The Historical Mismatch: Why `uasyncio` Should Have Been Built Differently
MicroPython is called **MICRO**Python. Yet when `uasyncio` was created, the project prioritized **desktop CPython compatibility** over **microcontroller physics**:
- *"If someone writes `asyncio` code on their PC, it should copy-paste onto an ESP32 or PyBoard."*

By mimicking CPython's desktop API, `uasyncio` inherited all of its desktop assumptions:
1. **Dynamic Heap Allocation on Every Step**: Every `async def`, `Task`, `Future`, and `await asyncio.sleep()` in MicroPython allocates objects on the Python heap.
2. **The 10 ms Garbage Collection Freeze**: MicroPython uses a mark-and-sweep Garbage Collector. When the heap fragments or fills up, `gc.collect()` freezes CPU execution for **5 to 15 milliseconds**. On an embedded drone or robot balancing an 8 kHz gyroscope and 200 Hz compass, a 15 ms freeze means lost sensor packets, missed attitude corrections, and flight instability.
3. **ROM & Symbol Table Footprint**: Supporting full `async/await` syntax requires enabling `MICROPY_PY_ASYNC_AWAIT = 1` and large QSTR symbol tables (`__aiter__`, `__anext__`, `StopAsyncIteration`), which exceed the lean 192 KB `SRAM_FAST` budget.

An embedded coroutine engine *should never have been a desktop socket scheduler*. It should have been designed for silicon: static channels, zero allocation in the loop pass, and direct hardware interrupt sleep.

#### What an Embedded Coroutine Engine Actually Looks Like (`abstractx_coro.py`)

![Desktop Python vs MicroPython vs AbstractX Coroutines](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/table5-async-concurrency.svg)

Rather than relying on heap-allocating task wrappers, AbstractX brings the exact C++20 coroutine model into MicroPython:

```python
# The AbstractX Coroutine Pattern: 0 Heap Allocations in Steady State
import abstractx
import abstractx_coro

mag_channel = abstractx_coro.AsyncQueue(16)  # Pre-allocated fixed ring buffer
buf = bytearray(6)                           # Reusable zero-copy buffer

def compass_task(channel):
    while True:
        # Hardware interrupt reads into pre-allocated buffer
        n = abstractx.i2c_read_into(0x1E, 0x03, buf)
        if n == 6:
            channel.try_push(bytes(buf))
        # Yields execution; E906 enters wfi sleep (0.01% CPU)
        yield abstractx_coro.sleep_ms(20)
```

- **0 bytes allocated** during steady-state execution $\rightarrow$ **0 Garbage Collection pauses**.
- **Wakes on hardware interrupts**: `abstractx.sleep_ms()` calls `wfi`, letting the core sleep while UART DMA and I2C hardware state machines run in parallel.
- **Direct 1:1 Mapping to C++20**: When the prototype is proven, running the `micropython-to-cpp` skill translates the Python generator coroutine directly into a native C++20 `coro::Task<void>` with zero conceptual friction.

## 6. Experiments without compiling

Everything in this section is Python. The firmware stays loaded the whole time.

### 6.1 Python on Linux runs Python on the E906

The board tests include a helper, `repl_exec()`, that sends a script to the
E906's REPL endpoint and returns its output. Here it checks out the compass
from a Linux Python script:

```python
# e906_run.py: run Python on the E906 from Python on Linux
from e906_link import repl_exec

code = """
import abstractx
abstractx.i2c_init(400000)
print('found', abstractx.i2c_scan())
print('id', bytes(abstractx.i2c_read(0x1E, 0x0A, 3)))
"""
out, err = repl_exec(code, 10.0)
print(out, err)
```

Output on our board:

```text
found [30]
id b'H43'
```

`30` is `0x1E`, and `H43` is the identification string of an HMC5883L. That one
experiment settled a real question: the board had been assumed to carry a
QMC5883L at `0x0D`, and every earlier script read the wrong registers.
[`e906_link.py`](https://github.com/tcmichals/AbstractX/blob/main/tests/board/e906_link.py)
and the trace example above are all it takes to build your own Linux-side test
programs around the E906.

### 6.2 An interactive prompt

For poking around by hand, open the REPL endpoint on the board:

```bash
grep -l abstractx-repl /sys/class/rpmsg/rpmsg*/name   # which /dev/rpmsgN is the REPL
microcom -p /dev/rpmsgN
```

```python
>>> import abstractx
>>> abstractx.version()
'AbstractX 1.0.0 (E906 MicroPython REPL)'
>>> abstractx.irq_stats()       # which interrupt lines have fired, and how often
```

From the PC,
[`e906_workbench.py`](https://github.com/tcmichals/AbstractX/blob/main/apps/e906_repl_app/linux/e906_workbench.py)
does the same over ssh, including one-shot commands (`-c "..."`). Only one
program can hold the REPL at a time.

### 6.3 Check out the compass

```python
>>> abstractx.i2c_init(400000)
>>> bytes(abstractx.i2c_read(0x1E, 0x0A, 3))      # b'H43'
>>> abstractx.i2c_write(0x1E, 0x00, b"\x70")      # 8-sample average, 15 Hz
>>> abstractx.i2c_write(0x1E, 0x01, b"\x20")      # +-1.3 gauss range
>>> abstractx.i2c_write(0x1E, 0x02, b"\x00")      # continuous measurement
>>> abstractx.i2c_read(0x1E, 0x03, 6)             # X, Z, Y, 16-bit big-endian each
```

Turn the board and read again: the numbers change. The decoder used by the
sensor streamer is
[`mag_xyz()` in `raw_sensor_streamer.py`](https://github.com/tcmichals/AbstractX/blob/main/apps/e906_repl_app/raw_sensor_streamer.py).

### 6.4 Check out the GPS

The u-blox powers up at 9600 baud:

```python
>>> abstractx.uart_init(9600)
>>> abstractx.sleep_ms(500)                       # sleeps; UART interrupts fill a buffer
>>> print(str(abstractx.uart_read(512), "ascii")) # NMEA sentences
```

You can also enable the **autonomous 16×64-byte circular DMA ring** in `.sram_c`:

```python
>>> abstractx.uart_dma(True)                      # activates circular DMA on UART3
>>> buf = bytearray(512)
>>> n = abstractx.uart_read_into(buf)             # zero-copy drain from DMA ring
>>> print(str(buf[:n], "ascii"))
```

Switching the GPS to 115,200 baud is a binary configuration message to the GPS
followed by `uart_init(115200)`; the
[streamer](https://github.com/tcmichals/AbstractX/blob/main/apps/e906_repl_app/raw_sensor_streamer.py)
shows the exact bytes. At 115,200 baud, circular DMA absorbs bursts without dropping a byte.

### 6.5 Time things, then put it in a file

```python
>>> t0 = abstractx.cycles()
>>> d = abstractx.i2c_read(0x1E, 0x03, 6)
>>> (abstractx.cycles() - t0) / 200               # microseconds, including Python's own work
```

When the experiment grows past a few lines, write it into a file on the PC,
copy it over and run it:

```bash
scp -O my_probe.py root@192.168.3.3:/root/
ssh root@192.168.3.3 python3 run_e906_script.py my_probe.py 30   # stop after 30 s
```

[`run_e906_script.py`](https://github.com/tcmichals/AbstractX/blob/main/apps/e906_repl_app/run_e906_script.py)
sends the file in chunks, prints what it prints, and Ctrl-C stops the script
on the E906. Edit, copy, run: a few seconds per try. The equivalent C++ change
would be a cross-compile, a firmware copy and a `remoteproc` restart.

### 6.6 Python beside a running stream: Meet AbstractX Studio's CLI

While command-line scripts like `run_e906_script.py` are convenient for batch runs, real-time embedded work is vastly more productive with a dedicated engineering workbench. This brings us to **AbstractX Studio** ([`tools/visualizer`](https://github.com/tcmichals/AbstractX/tree/main/tools/visualizer)), our native cross-platform desktop cockpit.

Connecting to the Cubie A5E over SSH, Studio provides an interactive **CLI tab**. It allows you to inject Python statements directly into the E906's MicroPython engine *while* the compass and GPS streaming tasks continue running concurrently in the background:

![Studio's CLI tab running Python on the E906 while the compass and GPS stream continues](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/studio-cli.png)

*Captured from the live board: `ps` and `drivers` answer from the E906's status report (here `repl_rx` at 7.6 % while the 100 ms sensor streamer runs), and `abstractx.version()` and `i2c_read(0x1E, 0x0A, 3)` were executed by MicroPython on the E906; `[72, 52, 51]` is `b"H43"`.*

A command travels from Studio over ssh to a bridge script on Linux, then over
the REPL stream to the E906; the answer comes back the same way in about 55 ms.
Variables are kept between commands, and an exception is reported without
stopping the stream.

### 6.7 Parallel Multi-Rate Sensor Ingestion (The Coroutine Way)

Instead of sequential polling scripts that waste bus time, the **AbstractX Coroutine Pattern** lets developers orchestrate multiple sensors running at different rates concurrently:

```python
# parallel_sensor_coros.py: Multi-rate concurrent sensor ingestion on the E906
import abstractx
import abstractx_coro

# 1. Initialize hardware buses & circular DMA
abstractx.i2c_init(400000)
abstractx.i2c_write(0x1E, 0x00, b"\x70")      # CRA: 8-sample average, 15 Hz
abstractx.i2c_write(0x1E, 0x01, b"\x20")      # CRB: +-1.3 gauss
abstractx.i2c_write(0x1E, 0x02, b"\x00")      # MR: continuous measurement
abstractx.uart_init(115200)
abstractx.uart_dma(True)                      # activate 16x64B circular DMA

# 2. Channels matching AbstractX C++ AsyncQueue<T, N>
mag_channel = abstractx_coro.AsyncQueue(16)
gps_channel = abstractx_coro.AsyncQueue(16)

# 3. Dedicated Sensor Producers
def compass_producer(channel):
    buf = bytearray(6)
    while True:
        # Hardware interrupt reads into pre-allocated buffer
        n = abstractx.i2c_read_into(0x1E, 0x03, buf)
        if n == 6:
            channel.try_push(bytes(buf))
        # Yields for 20 ms; E906 enters wfi sleep (0.01% CPU)
        yield abstractx_coro.sleep_ms(20)

def gps_producer(channel):
    buf = bytearray(512)
    while True:
        # Autonomous DMA ring drained in zero heap allocations
        n = abstractx.uart_read_into(buf)
        if n > 0:
            channel.try_push(bytes(buf[:n]))
        yield abstractx_coro.sleep_ms(100)

# 4. Primary-Paced Consumer Loop
def flight_fusion_task(primary_q, aux_q):
    last_gps = None
    epoch = 0
    while True:
        # Await next compass sample (acts as the physical pacing clock)
        mag_sample = yield primary_q.pop()

        # Non-blockingly drain auxiliary lower-rate sensor (GPS)
        while True:
            gps = aux_q.try_pop()
            if gps is None:
                break
            last_gps = gps

        epoch += 1
        # Decode 16-bit big-endian X, Z, Y
        x = (mag_sample[0] << 8) | mag_sample[1]
        z = (mag_sample[2] << 8) | mag_sample[3]
        y = (mag_sample[4] << 8) | mag_sample[5]
        if epoch % 10 == 0:  # Print every 10th epoch (5 Hz output rate)
            print("Epoch", epoch, "Mag:", (x, y, z), "GPS bytes:", len(last_gps) if last_gps else 0)

# 5. Cooperative Dispatcher
dispatcher = abstractx_coro.Dispatcher()
dispatcher.spawn(compass_producer(mag_channel))
dispatcher.spawn(gps_producer(gps_channel))
dispatcher.spawn(flight_fusion_task(mag_channel, gps_channel))

# Runs the event loop with hardware wfi idle sleep
dispatcher.run(timeout_ms=10000)
```

Running this on the board gives sub-millisecond epoch accuracy, 0% CPU waste during bus phases, and 0 dropped GPS sentences.

---

## 7. Watching the E906 Work: Inside AbstractX Studio

When developing software for an asymmetric multi-core processor (AMP)—where Linux runs on high-performance application cores while a bare-metal coprocessor handles real-time I/O—the single biggest obstacle is **observability**.

On Linux, developers take a rich suite of diagnostic tools for granted: `top`, `htop`, `perf`, `ftrace`, `gdb`, core dumps, and syslog. But on a bare-metal RISC-V coprocessor, developers are usually **flying blind**:
- **No OS means no `top`**: Without an operating system scheduler slicing threads, there is no built-in way to know whether your coprocessor is 5% busy or 99% busy, or how much CPU time MicroPython's garbage collector is consuming relative to hardware drivers.
- **The `printf` trap & Heisenbugs**: Inserting debug `printf()` calls over a serial port stalls execution. At 115,200 baud, printing a single 100-character line takes **8.7 milliseconds**. In a 1 kHz sensor loop, that delay is fatal—it causes I²C arbitration timeouts, overflows UART FIFOs, and completely distorts the timing you were attempting to measure. The bug disappears or mutates the moment you try to observe it.
- **JTAG halts break real-time physics**: Halting the CPU with a traditional JTAG hardware breakpoint stops the core, but external hardware (the compass, GPS, and DMA controllers) keeps running. Registers overflow and communication interfaces time out, making live debugging impossible.

To eliminate this blindness, AbstractX includes **AbstractX Studio** ([`tools/visualizer`](https://github.com/tcmichals/AbstractX/tree/main/tools/visualizer)): a unified desktop engineering workbench and observability cockpit designed specifically for bare-metal and heterogeneous systems.

### 7.1 What is AbstractX Studio?

AbstractX Studio is a native desktop application written in modern C++20 using **Dear ImGui** and **HelloImGui**. It runs on your development PC (Linux, macOS, or Windows) at a silky 60 FPS and connects to the Cubie A5E over a standard SSH connection or network socket.

The workbench is **fully customizable**: developers can drag, re-dock, split horizontally or vertically, and stack tabs anywhere on the canvas. With multi-viewport docking enabled by default, any tool can be torn off into its own independent OS window on a second monitor with a 1-click `Pop In` button to re-dock. Crucially, Studio is not a fixed dashboard—it allows developers to **embed their own custom tabs and instruments** directly into the workbench using an open Python plugin architecture.

Instead of heavy text logging or intrusive polling, Studio relies on **zero-copy Common Trace Format (CTF 1.8) binary telemetry**. The E906 firmware writes compact 64-byte binary Transaction Layer Packets (TLPs) into lock-free shared SRAM rings (`.sram_c`). 

Emitting a binary trace packet takes only **4 to 12 RISC-V instructions** (~20 to 60 nanoseconds). The coprocessor spends less than **0.4% CPU overhead** reporting its internal state, completely eliminating the probe effect.

Linux on the Cortex-A55 cores simply forwards the raw binary ring buffers over SSH to your workstation. AbstractX Studio on the PC does all the heavy lifting: unpacking binary frames, reading the firmware's ELF DWARF debug symbols to resolve addresses to source lines, and rendering real-time graphs and swimlanes.

### 7.2 The Four Studio Instruments

Studio translates this binary stream into four real-time observability instruments, giving bare-metal developers the same visibility Linux developers expect:

#### 1. CPU & Driver Telemetry (Bare-Metal `top`)

Studio's CPU tab updates once a second, displaying:
- A real-time CPU busy meter and rolling history graph.
- A task table (`rpmsg_rx`, `repl_rx`, `repl_tx`) showing exact cycle percentages consumed by MicroPython versus message routing.
- An interrupt driver table covering every physical CLIC interrupt line (Timer 92, UART3 90, TWI1 91, Msgbox 88), displaying its trigger frequency (Hz), exact execution cost per interrupt in microseconds (e.g. 1.5 µs for I²C, 25 µs for UART byte handler, 2.1 µs for circular DMA), and hardware error counters.
- Memory pool high-water marks, verifying zero memory leaks in steady state.

![Studio's CPU tab: busy percentage over time, the task table, the driver table with uart3 and twi1, and pool usage](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/studio-cpu.png)

*Captured from the live board with the sensor streamer running: the E906 is about 8 % busy; the timer line takes about 1,000 interrupts a second (the profiler's samples), UART3 costs about 25 µs per interrupt and TWI1 about 1.5 µs. In DMA mode (`abstractx.uart_dma(True)`), UART3 drops 0 bytes even during continuous 115200-baud GPS streaming bursts (section 10).*

#### 2. Live Statistical Source-Line Profiler (Bare-Metal `perf`)

To find performance bottlenecks without modifying code:
- A 1,000 Hz hardware timer interrupt samples the E906 Program Counter (PC).
- If the core is sleeping in `wfi`, the ISR samples the idle loop and returns immediately to sleep, preserving the low-power idle state without waking up the event loop.
- On the PC, Studio cross-references the sampled addresses against the firmware ELF binary's DWARF symbol table, displaying a live sorted table of hot functions and source lines.
- You can instantly see where CPU cycles go: e.g. 92% in `wfi` idle sleep, 4% in MicroPython's bytecode dispatch (`mp_execute_bytecode`), 2% in garbage collection (`gc_alloc`), and <1% in hardware drivers. Clicking any line links directly to source code.

![Studio's hot-function table from the live profiler](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/studio-source.png)

*Captured from the live board, 5 seconds of samples. The top line (92 %) is the E906 sleeping in its idle `wfi`; it is listed under `now_cycles()` because the lookup reports the innermost inlined function at that address. The rest is MicroPython's interpreter (`qstr_find_strn`, `gc_alloc`, `mp_execute_bytecode`), each line clickable to its source.*

#### 3. Software Logic Analyzer Timeline (Microsecond Swimlanes)

Like an oscilloscope or logic analyzer for software execution:
- Displays synchronized horizontal swimlanes on a shared 200 MHz CPU cycle clock.
- Shows when tasks run, when each physical interrupt line fires (as microsecond-accurate tick marks), and when the core sleeps in `wfi`.
- Allows sub-microsecond inspection of bus events (e.g. seeing the 11 individual CLIC interrupt pulses of an I²C transaction unfold over time while UART DMA packets arrive in parallel).

![Studio's timeline, animated: swimlanes for idle and each interrupt line on the E906 cycle clock, 100 ms window](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/studio-timeline.gif)

*Captured from the live board, last 100 ms: the long grey bar is the E906 idle, each tick is one interrupt (the timer and UART3 here); a task gets its own lane when it runs inside the window.*

#### 4. Interactive REPL & System CLI (Live Command & Inspection)

Studio includes a dedicated terminal tab connected directly to the E906 over RPMsg (shown in [Section 6.6](#66-python-beside-a-running-stream-meet-abstractx-studios-cli)):
- Allows developers to execute Python one-liners, inspect global variables, and query system diagnostics (`ps`, `drivers`, `irq_stats`, `mem`) live on the coprocessor.
- Operates concurrently with ongoing sensor streaming and control loops—you can poke registers and inspect state without stopping or restarting the stream.
- Commands travel over SSH and RPMsg, returning output in approximately 55 ms with variables preserved between statements.

### 7.3 How Studio is Programmed: Declarative JSON Schemas & Python Plugins

A common limitation of embedded diagnostic GUIs is rigidity: they are compiled binary monoliths with hardcoded packet parsers and static layouts. If an engineer adds a magnetometer axis, swaps an IMU, or changes a telemetry rate, someone has to modify C++ GUI parsing code and recompile the desktop application.

AbstractX Studio takes a completely different approach: it is **declarative and programmable**.

#### 1. Dynamic barectf & CTF 1.8 Telemetry Schemas (`trace_schema.json` & `barectf_config.yaml`)

Telemetry streams in AbstractX are governed by **barectf**—an open-source generator that produces ultra-fast, zero-allocation ANSI C tracers emitting the standardized Common Trace Format (CTF 1.8).

Instead of burning cycles on string formatting or JSON serialization on the microcontroller, the E906 firmware bit-packs trace events directly into shared SRAM rings in 4 to 12 instructions. Furthermore, MicroPython scripts on the E906 can call `abstractx.trace(task_id, state, "msg")` to emit structured barectf binary events into this exact same stream!

On the host PC, Python tools and AbstractX Studio decode these binary streams dynamically using external schemas rather than hardcoded struct offsets:

```json
{
  "application": "gps_imu_app",
  "wire_protocol": { "frame_size_bytes": 64, "header_size_bytes": 20, "payload_size_bytes": 40 },
  "streams": {
    "1": {
      "name": "telemetry",
      "events": {
        "1": {
          "name": "imu_sample",
          "rate_hz": 8000,
          "source_device": "ICM-42688-P (SPI1)",
          "unpack_format": "<i6h",
          "fields": [
            { "name": "sample_seq", "type": "int32", "scale": 1.0, "display_name": "Sequence" },
            { "name": "accel_x_mg", "type": "int16", "unit": "g", "scale": 0.001, "widget": "oscilloscope" },
            { "name": "gyro_z_dps", "type": "int16", "unit": "deg/s", "scale": 0.1, "widget": "oscilloscope" }
          ]
        },
        "2": {
          "name": "mag_sample",
          "rate_hz": 50,
          "source_device": "HMC5883L (I2C1)",
          "unpack_format": "<3hH",
          "fields": [
            { "name": "mag_x_mgauss", "type": "int16", "unit": "mG", "scale": 1.0 },
            { "name": "heading_cdeg", "type": "uint16", "unit": "deg", "scale": 0.01, "widget": "compass" }
          ]
        }
      }
    }
  }
}
```

When Studio or a Python tool connects to a stream:
1. **Dynamic barectf Unpack Compilation**: Studio's `CtfSchemaLoader` (`tools/visualizer/ctf_schema_loader.py`) reads the barectf YAML or JSON configuration and compiles the `unpack_format` (e.g. `<i6h` or `<3hH`) into native Python `struct.unpack` byte decoders on the fly.
2. **Automatic Unit Scaling**: Raw integer values from hardware ADCs are automatically scaled into human-readable engineering units using the `"scale"` multiplier (e.g. `0.001` converts milligauss or milli-$g$ to $g$, and `0.01` converts centidegrees to degrees).
3. **Automatic UI Widget Binding**: The `"widget"` tag binds decoded signals directly to Studio's visualization components—routing high-rate accelerometer and gyroscope data to the 8 kHz oscilloscope, magnetic heading to the vector compass dial, or altitude to the altimeter—without writing any UI wiring code.

#### 2. Declarative Hardware Profiles (`profiles/*.json`)

Hardware profiles describe the physical target board and its interconnect topology:

```json
{
  "profile_id": "cubie_a5e_pure",
  "platform_name": "Allwinner Cubie A5E (Pure Silicon)",
  "board_ip": "192.168.3.3",
  "udp_port": 9870,
  "active_cores": [
    {"id": 0, "role": "Host Linux ARM64", "task": "Flight Supervisor & Telemetry", "clock_mhz": 1400},
    {"id": 1, "role": "XuanTie E906 RISC-V", "task": "Real-Time I/O Reactor & Sensors", "clock_mhz": 200}
  ],
  "hardware_accels": ["UART3 Circular DMA (GPS)", "TWI1 I2C (Compass)", "sun6i-msgbox"],
  "schema_file": "apps/gps_imu_app/trace_schema.json"
}
```

Switching Studio between targets—from the bare-metal XuanTie E906 on the Cubie A5E, to an ESP32-P4, a Raspberry Pi Pico 2 W, or a desktop SITL simulator—is simply a matter of passing `--profile profiles/<target>.json`. Studio automatically reconfigures its core topology views, network endpoints, and telemetry parsers.

#### 3. Customizing Windows & Embedding Your Own Tabs (`AbstractXStudioPlugin`)

A hallmark of AbstractX Studio is that developers are never forced into a single pre-baked layout or rigid set of tools. 

Every engineering project has unique telemetry needs—a robotics team needs motor demand bars and Joint PID curves; a drone project needs artificial horizons and compass roses; a battery management system needs cell voltage heatmaps.

Studio makes it trivial for users to **customize their workspace and embed their own tabs directly into the workbench** using the Python Plugin SDK ([`tools/visualizer/sdk/plugin.py`](https://github.com/tcmichals/AbstractX/blob/main/tools/visualizer/sdk/plugin.py)):

```python
from visualizer.sdk.plugin import AbstractXStudioPlugin
from imgui_bundle import imgui, implot

class CustomSensorDashboard(AbstractXStudioPlugin):
    def __init__(self):
        super().__init__("Custom Dashboard", "1.0")
        self.latest_heading = 0.0

    def on_tlp_packet(self, header, payload_dict):
        # Asynchronously invoked when relevant telemetry packets arrive
        if "heading_cdeg" in payload_dict:
            self.latest_heading = payload_dict["heading_cdeg"] * 0.01

    def render_ui(self, delta_time, state):
        # Rendered at 60 FPS inside Studio's dockable tab
        imgui.text_colored(imgui.ImVec4(0.3, 0.85, 1.0, 1.0), "Custom Flight Deck")
        imgui.text(f"Live Magnetic Heading: {self.latest_heading:.1f}°")
        
        # Interactive controls sending commands to E906 over RPMsg
        if imgui.button("Zero Compass Bias"):
            state.send_cli("abstractx.compass_calibrate()")
```

By subclassing `AbstractXStudioPlugin`, your custom tool becomes a **first-class citizen on the workbench**:
- **Automatic Docking & Layout Customization**: Your tab can be dragged, nested into tab groups, or split side-by-side with CPU gauges, the timeline, or the memory inspector.
- **Multi-Monitor "Pop to Canvas"**: Any embedded tab gets a native OS window tear-off button (`🗖 Pop to Canvas`). You can pull your custom dashboard onto a second monitor, resize it, and dock it back with 1 click (`🗗 Pop In`).
- **Direct Real-Time Target Interaction**: Through `state.send_cli(...)`, your custom tab can send Python expressions, calibrate sensors, or tweak control gains on the live E906 coprocessor while streaming continues uninterrupted.
- **Zero GUI Boilerplate**: No OpenGL shaders, GLFW event loops, or thread synchronizers. You write high-level ImGui and ImPlot Python code, and Studio handles the rendering at 60 FPS.


## 8. Under the hood: how one call reaches the hardware

![The path of abstractx.i2c_read(0x1E, 0x03, 6): Python in repl_rx, modabstractx.c, e906_periph.cpp starting the I²C transfer, a wfi wait loop, one TWI1 interrupt per bus step, and the measurement hooks along the way](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/call-path.png)

*Follow the left column top to bottom; the green box is what happens between the `wfi` and the wake; the amber box shows where Studio's numbers come from.*

One compass read, `abstractx.i2c_read(0x1E, 0x03, 6)`, step by step:

1. **Python** calls a built-in C function. Everything from here runs inside the
   `repl_rx` task, which is why its time is reported under that name.
2. **[`modabstractx.c`](https://github.com/tcmichals/AbstractX/blob/main/apps/e906_repl_app/src/modabstractx.c)**
   converts the arguments and calls the I²C driver.
3. **[`e906_periph.cpp`](https://github.com/tcmichals/AbstractX/blob/main/apps/e906_repl_app/src/e906_periph.cpp)**
   copies the request into a static record (so an interrupt can never write
   into a stack frame that a Ctrl-C has already unwound) and tells the I²C
   block to send START.
4. **The wait**: arm a 20 ms timeout, let pending work run, then sleep in
   `wfi`. The loop never reads an I²C register.
5. **The interrupt**: each bus step raises the I²C interrupt (CLIC line 91).
   The handler takes exactly one step: send the address, send the register,
   repeated START, address with the read bit, store a byte, or STOP. A 6-byte
   register read is 11 steps.
6. **Back in Python**: the bytes come back as a `bytes` object. On a timeout
   the driver sends STOP, counts the error (it shows in Studio's driver table)
   and Python gets `b''`.

`uart_read()` is simpler: it never waits; it takes what the UART interrupt
handler (CLIC line 90) has already put in its buffer.

## 9. Where everything sits in memory

![E906 memory: hot firmware in SRAM Space 0, cold compiler code and trace rings in the lower half of Space 1, Linux RPMsg rings in the upper half, MicroPython heap in DSP RAM](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/memory-layout.png)

The E906 runs entirely from on-chip SRAM; nothing is in DRAM (Part 1 maps the
windows):

![SRAM Space Partitioning for RISC-V Firmware & MicroPython](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/table6-memory-regions.svg)

Space 0 had filled to 99.3 %. Moving the MicroPython compiler (only needed while
a script is being compiled) into Space 1 brought it to 85 %; with the profiler
and DMA code added since, it is at 89.6 %.

## 10. DMA, shared with Linux

Reading a UART one byte at a time costs about 0.29 µs per register read on
the E906 (measured, section 11). At high data rates that adds up, so the next
step is DMA: hardware that copies data while the CPU does something else.

The T527 has one main DMA controller with 16 channels, normally all Linux's.
Sharing it takes three things:

- **Channels:** the overlay's `dma-channels = <8>` (section 2) leaves channels
  8-15 to the E906.
- **Interrupts:** two registers decide which processor sees each channel's
  "done" interrupt. Both default to "everyone". The E906 routes 8-15 to itself
  and away from Linux before using them; otherwise Linux would get interrupts
  it cannot clear.
- **Addresses:** the DMA controller uses physical addresses, which differ from
  the E906's view by a fixed offset; the driver converts them.

What it buys, measured with `abstractx.dma_bench()` (SRAM to SRAM):

![Hardware DMA Engine vs XuanTie E906 CPU memcpy](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/table7-dma-benchmark.svg)

DMA has a fixed cost of about 4.4 µs, and the CPU is free during the transfer:
the "done" interrupt itself costs 1.33 µs. The rule we adopted: **a transfer
bigger than the peripheral's hardware FIFO uses DMA; one that fits uses the
FIFO.**

UART receive streams are always bigger than the FIFO, so we implemented **circular
UART receive by DMA into a 16×64-byte ring buffer (1,024 B total in `.sram_c`)**
with hardware RX timeout triggers (`IIR = 0x0C`) to deliver partial NMEA frames immediately.
At 115,200 baud, this 1,024-byte ring provides **88.8 ms of buffer headroom**, completely
absorbing any temporary MicroPython GC pauses without dropping a single byte or corrupting
checksums. You can query or toggle it live from Python with `abstractx.uart_dma(True)`.

## 11. Measured on the board

All on the Cubie A5E with the E906 at 200 MHz, 2026-10-21 to 2026-10-23:

![Silicon Timings, Interrupt Latency & Resource Utilization](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/table8-measured-results.svg)

The two lessons: the interpreter, not the drivers, is where the time goes in
Python; and register access from this core is expensive enough that DMA
matters at high data rates.

## 12. What went wrong

> **The console flood.** The first profiler firmware sent about 20 trace
> packets a second. After a reader on Linux closed the trace endpoint, the
> E906 kept sending, because Linux never tells the other side that a reader
> went away. Linux logged `virtio_rpmsg_bus: msg received with no recipient`
> for every packet on the serial console, the console became unusable and the
> board had to be rebooted twice. The older status report had the same flaw at
> one packet a second, which is why nobody had noticed. The fix is the 3-second
> hello lease from section 4, plus a board test that checks the kernel log stays
> quiet after the reader closes.
>
> **`wfi + 4`.** A profiler sample that interrupts the idle `wfi` should return
> to the `wfi`. The first fix compared the saved return address with the `wfi`
> instruction, but this E906 saves the address of the next instruction. The
> check never matched and the idle loop still woke about 1,050 times a second.
> The firmware now accepts both addresses, and idle wakes dropped to about one
> a second.
>
> **The DMA bring-up & the "last byte" trap.** Getting hardware DMA working across
> asymmetric processors took days of grueling bring-up. First, Linux claimed all 16 DMA
> channels by default, so we had to patch the device tree (`dma-channels = <8>`) and reprogram
> interrupt routing registers so the Linux kernel wouldn't panic on unhandled coprocessor interrupts.
> Second, the E906 views SRAM through its local core addresses (`0x3FFC0000` / `0x40000000`), but
> the system DMA controller requires physical bus addresses (`0x00020000`); passing raw C++ pointers
> silently caused DMA bus errors. But the most insidious bug was the **"last byte" issue**:
> UART GPS sentences are variable in length (e.g. 73 bytes). If you configure DMA to transfer
> 64 or 512 bytes, the transfer only triggers when the block completes—leaving the **trailing
> bytes of a sentence trapped in the hardware FIFO indefinitely** until the next burst arrived!
> The fix was pairing **circular DMA mode** (a 16×64-byte ring buffer in shared SRAM) with the
> UART's hardware **character timeout interrupt** (`IIR = 0x0C`, `UART_DMA_REQ_EN` bit 2). When the
> line goes idle for 4 character times, the timeout interrupt flushes the pipeline and delivers the
> trailing bytes to Python immediately.
>
> **Studio pop-outs, multi-viewports & the disappearing canvas.** Giving AbstractX Studio multi-monitor
> pop-out capabilities took three iterations to get right. Our first attempt was a crude hack: clicking
> "Pop Out" spawned an external Python subprocess (`flight_display.py`). Clicking it repeatedly spawned
> an avalanche of rogue Python processes that didn't share memory, and clicking "Pop In" was a broken no-op.
> When we moved to native Dear ImGui multi-viewports (`enable_viewports = True`), three subtle bugs hit:
> 1. *The Auto-Merge Trap*: ImGui's default `io.config_viewports_no_auto_merge = False` automatically
>    swallowed any floating window that overlapped the main workbench back into the main OS window,
>    making pop-out look completely broken.
> 2. *Pruned Dock Nodes*: When a window was popped out, ImGui dynamically pruned its parent dock split.
>    Calling `dock_builder_dock_window()` on a pruned node failed silently. A naive fallback called
>    `restore_default_layout()`, which reset *every* window globally—inadvertently slamming the user's
>    flight instruments back into the main window and destroying their layout! We had to detect pruned
>    nodes, re-split the parent node independently (`dock_builder_split_node`), and call
>    `dock_builder_finish()` to stage and commit docking changes cleanly.
> 3. *The Sizing Bar Minimum Width Clamp*: Preset buttons (800×600, 1024×720) were chained on a single
>    horizontal line measuring 1,345 pixels wide. Because ImGui windows can never shrink smaller than
>    their content, clicking the 800×600 preset did nothing until the sizing bar was wrapped into multiple rows.

These problems did not show up in simulation or static analysis. They were found on the physical board
and during real workbench testing, and are fully recorded in the
[engineering log](https://github.com/tcmichals/AbstractX/blob/main/engineering_log.md).

## 13. Limits and what is not done yet

Several early bring-up friction points noted in previous drafts have now been fully resolved in the AbstractX tree:

- **UART Receive DMA is live:** Circular DMA (`abstractx.uart_dma(True)`) is now fully operational on E906 with a 16×64-byte circular ring in shared SRAM and hardware character timeout interrupts (`IIR = 0x0C`), dropping CPU UART load to 0.4 %.
- **Dual-Mode SPI1 DMA Engine is live:** Hardware SPI1 on Port B (PB11..PB14) is fully operational with Single-SPI 1-bit and Dual-SPI 2-bit burst (`BCC[28]`) streaming (`abstractx.spi_init()`, `abstractx.spi_xfer()`, `abstractx.spi_xfer_into()`). Transfers $\le 64$ B use direct FIFO MMIO while bulk transfers use dual-channel DMA.
- **DMA Physical Address Translation & Firewall Handling:** All bus drivers (`spi.cpp`, `uart.cpp`, `i2c.cpp`) translate E906 addresses through `DmaController::pa()` and stage descriptors in SRAM Space 1 (`.sram_c`). Channels 9 and 10 are identified as hardware-firewalled by SoC security policy, reserving channels 8 and 11..15 for autonomous coprocessor DMA.
- **Native Binary Telemetry & Trace are built:** Scripts no longer rely solely on printing text over the console. `abstractx.tlp_send(buf)` streams raw 64-byte binary telemetry frames directly across RPMsg endpoint 1024, and `abstractx.trace(task_id, state, msg)` emits CTF 1.8 binary events directly over endpoint 1025.
- **Profiler Idle Attribution is fixed:** The profiler classification was patched to accept both `wfi` and `wfi + 4` return addresses (`live_profile.py`), correctly mapping idle CPU time to `idle` across Studio's gauges and timeline swimlanes.

### Remaining Boundaries & Next Horizons

- **UART APB1 Clock Ceiling:** The UART clock (APB1) is fixed at 24 MHz on this board, limiting standard integer divisors to 1.5 Mbit/s. Pushing past this to 3.0 or 6.0 Mbit/s requires patching the Linux clock tree driver to reconfigure the parent APB1 PLL.
- **MicroPython in Tight Loops (The Interpreter Tax):** Interpreted Python bytecode consumes ~90 % CPU when executing tight polling loops (e.g. busy-waiting on I2C/UART). MicroPython is designed for rapid interactive bring-up, peripheral prototyping, and scriptable instrumentation; flight-critical real-time control loops (such as 8 kHz IMU fusion) belong in freestanding C++20 coroutines on the exact same HAL drivers (< 1 % CPU).
- **Automated Script-to-Coro Migration:** Tooling is being expanded (see the `micropython-to-cpp` workflow) to translate proven MicroPython async tasks directly into freestanding C++20 coroutines, bridging the gap between interactive REPL experimentation and hardened bare-metal deployment.
- **Full FPGA Switch Fabric Loopback:** Exercising physical loopback tests between the E906 Dual-SPI burst DMA engine and the Tang Primer 20K FPGA crossbar (`asp_router.sv`) on real hardware headers.

## 14. Where to go next

- **The series:** [Part 1](https://www.hackster.io/MichalsTC/unlocking-the-xuantie-risc-v-core-on-allwinner-t527-part-1-976063) (architecture, memory, boot), [Part 2](https://www.hackster.io/MichalsTC/bringing-up-heterogeneous-risc-v-on-allwinner-socs-part-2-53deb8) (remoteproc), [Part 3](https://github.com/tcmichals/cubie-a5e/blob/main/docs/articles/part3_baremetal_firmware_ipc_and_coroutines_intro.md) (message box and RPMsg), [Part 4](https://github.com/tcmichals/cubie-a5e/blob/main/docs/articles/part4_deep_dive_baremetal_cpp_coroutines.md) (coroutines vs FreeRTOS), [device-tree overlays](https://github.com/tcmichals/cubie-a5e/blob/main/docs/articles/devicetreeOverlay.md) (U-Boot and `config.txt`)
- **The code:** [E906 MicroPython app](https://github.com/tcmichals/AbstractX/tree/main/apps/e906_repl_app) (wiring, overlays, build, Studio), [AbstractX Studio](https://github.com/tcmichals/AbstractX/tree/main/tools/visualizer), [board tests](https://github.com/tcmichals/AbstractX/tree/main/tests/board) (including `e906_link.py` and the timing scripts)
- **The same sensors in C++:** [Straight-Line Async: GPS + Compass with C++20 Coroutines](https://github.com/tcmichals/AbstractX/blob/main/docs/articles/gps-compass-straight-line-async/README.md)
- **Bare metal vs FreeRTOS on this core:** [AbstractX on the E906 with and without FreeRTOS](https://github.com/tcmichals/AbstractX/blob/main/docs/articles/baremetal-vs-freertos-e906/README.md)
- **For engineers who want the exact requirements:** the [E906 app specification](https://github.com/tcmichals/AbstractX/blob/main/apps/e906_repl_app/SPECIFICATION.md) and the [E906 target specification](https://github.com/tcmichals/AbstractX/blob/main/targets/allwinner_e906/SPECIFICATION.md) define everything this article describes, and say how each part is tested.
