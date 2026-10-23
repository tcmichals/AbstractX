# Experiment First, Compile Later: Python on Both Processors of the Allwinner T527

## Part 5 of *Bringing Up Heterogeneous RISC-V on Allwinner SoCs* (Radxa Cubie A5E)

**One Cubie A5E, two kinds of processor, Python on both: check out new hardware and run experiments in seconds, with no compile in the loop.**

The Radxa Cubie A5E is a small board with a surprise inside. Its Allwinner
T527 runs Linux on eight ARM Cortex-A55 cores, and next to them sits a 200 MHz
XuanTie E906 RISC-V core that Linux never uses. In this part we put MicroPython
on that RISC-V core and drive it from Python on Linux. You type a line, the E906
reads a compass over I²C or a GPS over a UART, and the answer comes back over
the link between the two processors. No firmware rebuild, no reflash, no reboot
between experiments. While it runs, a live view on your PC shows what the
little core is spending its time on.

![AbstractX Studio's CPU tab, live: busy percentage, the coroutine table and per-interrupt costs updating once a second from the E906](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/studio-cpu.gif)

*Live from the board: once a second the E906 reports how busy it is, which part of its software used the time, and what each hardware interrupt costs.*

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
   run code on the E906 or read its CPU load. A desktop tool, AbstractX Studio,
   gives you a prompt on the E906 and live views of what it is doing.
6. **The point is speed of learning:** try the hardware in Python, measure what
   it costs, and only then write the fast version in C++ on the same drivers.

### Where this fits in the series

| Part | What it covers | Where |
|---|---|---|
| 1 | The T527's hidden RISC-V core: why it is useful, the memory windows, how it boots from on-chip SRAM, how to debug it | [Hackster](https://www.hackster.io/MichalsTC/unlocking-the-xuantie-risc-v-core-on-allwinner-t527-part-1-976063) |
| 2 | The Linux `remoteproc` driver that loads and starts the E906, and a hardware test suite | [Hackster](https://www.hackster.io/MichalsTC/bringing-up-heterogeneous-risc-v-on-allwinner-socs-part-2-53deb8) |
| 3 | Linux ↔ RISC-V messaging: the message box doorbell, shared SRAM rings and VirtIO RPMsg, measured | [GitHub](https://github.com/tcmichals/cubie-a5e/blob/main/docs/articles/part3_baremetal_firmware_ipc_and_coroutines_intro.md) |
| 4 | C++20 coroutines vs FreeRTOS on the bare-metal E906 | [GitHub](https://github.com/tcmichals/cubie-a5e/blob/main/docs/articles/part4_deep_dive_baremetal_cpp_coroutines.md) |
| side | Device-tree overlays in U-Boot: how `config.txt` picks the hardware setup at boot | [GitHub](https://github.com/tcmichals/cubie-a5e/blob/main/docs/articles/devicetreeOverlay.md) |
| **5** | **This article: Python on both processors, RPMsg streams, live profiling, DMA** | |

You do not need the earlier parts to follow this one: each section below
starts from what you need to know and points back to the part that goes deeper.

---

## 1. The board and what we hang on it

The Cubie A5E's Allwinner T527 combines:

| Block | What it is | Who uses it here |
|---|---|---|
| 8 × ARM Cortex-A55 | Application cores, 64-bit | Linux, Python, the bridges to the PC |
| XuanTie E906 | 32-bit RISC-V (RV32IMAFC) at 200 MHz, single-precision FPU, no MMU, interrupt controller called CLIC | AbstractX firmware and MicroPython |
| On-chip SRAM | Two 256 KB windows the E906 can run from (Part 1 maps them) | E906 code, data, and the shared message rings |
| Message box | A hardware mailbox that raises an interrupt on the other processor | The "look now" doorbell for RPMsg |
| UARTs, I²C (TWI), SPI, DMA | Standard peripherals on the 40-pin header | Split between Linux and the E906 by the device tree |

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

| Endpoint | Name | What flows | How it uses messages |
|---|---|---|---|
| 1026 | `abstractx-repl` | MicroPython text: the prompt, scripts going in, output coming out | A text stream cut into messages; long scripts are sent in 256-byte chunks and reassembled on the E906 |
| 1024 | `abstractx-tlp` | Fixed 64-byte binary packets (sensor data, commands) | Up to 7 packets batched into one message (448 of 496 bytes) |
| 1025 | `abstractx-trace` | Status reports, profiler samples, timeline events | One self-describing binary packet (CTF format) per message, written straight into the RPMsg buffer, so the E906 never copies it |

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

*The three columns are three separate machines as far as software is concerned: a PC, Linux on the A55 cores, and the E906.*

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

| Function | What it does |
| :--- | :--- |
| `i2c_init([hz])`, `i2c_scan()`, `i2c_read(addr, reg, n)`, `i2c_write(addr, reg, data)` | I²C1 (the compass). Returns empty / 0 on a NACK or a 20 ms timeout |
| `uart_init([baud])`, `uart_read([n])`, `uart_write(data)`, `uart_status()` | UART3 (the GPS). `uart_read` takes bytes the interrupt handler already received |
| `sleep_ms(ms)`, `ticks_ms()`, `cycles()` | Sleep in `wfi`; millisecond and CPU-cycle (200 MHz) counters |
| `peek(addr)`, `peek_hex(addr)`, `poke(addr, val)` | Read or write any 32-bit register |
| `irq_stats()` | How often each interrupt line fired, plus error counters |
| `tlp_send(buf)`, `trace(...)`, `console_read([n])` | Send a 64-byte packet, a trace event, or read the REPL stream from a script |
| `dma_bench(n[, port])` | Copy `n` bytes by DMA and by CPU and time both (section 10) |

The board's MicroPython is a minimal build: `bytes.decode` and `bytes.fromhex`
do not exist (use `str(b, "ascii")` and `int(h, 16)`), and there is no
filesystem, so scripts arrive over RPMsg and are compiled in memory.

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

Switching it to 115,200 baud is a binary configuration message to the GPS
followed by `uart_init(115200)`; the
[streamer](https://github.com/tcmichals/AbstractX/blob/main/apps/e906_repl_app/raw_sensor_streamer.py)
shows the exact bytes. At 115,200 baud more than 512 bytes can arrive between
two reads, so read in a loop until a read returns less than 512.

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

### 6.6 Python beside a running stream, from the PC

AbstractX Studio is a desktop tool that connects to the board over ssh.
Its **CLI** tab sends Python to the same MicroPython that is streaming the
compass and GPS, without stopping the stream:

![Studio's CLI tab running Python on the E906 while the compass and GPS stream continues](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/studio-cli.png)

*Captured from the live board: `ps` and `drivers` answer from the E906's status report (here `repl_rx` at 7.6 % while the 100 ms sensor streamer runs), and `abstractx.version()` and `i2c_read(0x1E, 0x0A, 3)` were executed by MicroPython on the E906; `[72, 52, 51]` is `b"H43"`.*

A command travels from Studio over ssh to a bridge script on Linux, then over
the REPL stream to the E906; the answer comes back the same way in about 55 ms.
Variables are kept between commands, and an exception is reported without
stopping the stream.

## 7. Watching the E906 work

AbstractX measures itself, independently of what your script does, and sends
three kinds of trace packets on the trace stream. Studio turns them into
views.

**CPU and drivers, once a second.** How busy the E906 is, how much of that
each task used, and for each interrupt line how often it fired, what one
interrupt costs and how many errors its driver counted.

![Studio's CPU tab: busy percentage over time, the task table, the driver table with uart3 and twi1, and pool usage](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/studio-cpu.png)

*Captured from the live board with the sensor streamer running: the E906 is about 8 % busy; the timer line takes about 1,000 interrupts a second (the profiler's samples), UART3 costs about 25 µs per interrupt and TWI1 about 1.5 µs. UART3's error count is GPS bytes dropped while no script was reading the port, a known gap that the planned UART DMA receive ring closes (section 10).*

**A live profiler, per source line.** A timer interrupt records where the E906
was executing 1,000 times a second. Studio looks each address up in the
firmware file and shows CPU time per function and per source line, so the
hottest code is visible without instrumenting anything.

![Studio's hot-function table from the live profiler](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/studio-source.png)

*Captured from the live board, 5 seconds of samples. The top line (92 %) is the E906 sleeping in its idle `wfi`; it is listed under `now_cycles()` because the lookup reports the innermost inlined function at that address, a labelling bug still to fix. The rest is MicroPython's interpreter (`qstr_find_strn`, `gc_alloc`, `mp_execute_bytecode`), each line clickable to its source.*

**A timeline.** Every task run, every interrupt and every idle period as a bar
on a common clock, like a logic analyser for software.

![Studio's timeline, animated: swimlanes for idle and each interrupt line on the E906 cycle clock, 100 ms window](https://raw.githubusercontent.com/tcmichals/AbstractX/main/docs/articles/micropython-on-e906/assets/studio-timeline.gif)

*Captured from the live board, last 100 ms: the long grey bar is the E906 idle, each tick is one interrupt (the timer and UART3 here); a task gets its own lane when it runs inside the window.*

The profiler does not disturb what it measures: a sample that interrupts the
idle `wfi` returns straight to the `wfi`, so the idle loop does not wake up
1,000 times a second, and the busy figure stays honest (0.3-0.4 % idle with the
profiler on).

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

| Region | E906 address | Holds |
| :--- | :--- | :--- |
| SRAM Space 0, 192 KB | `0x3FFC0000` | Interrupt handlers, the event loop, drivers, RPMsg, the MicroPython interpreter, data, stack |
| SRAM Space 1, lower 128 KB | `0x40000000` | Resource table, boot log, code only used while compiling a script, profiler and timeline buffers |
| SRAM Space 1, upper 128 KB | `0x40020000` | Linux's RPMsg rings and buffers (section 4) |
| HiFi4 DSP RAM, 128 KB | `0x00020000` | MicroPython's heap |

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

| Bytes | DMA, start to done | CPU copy |
| ---: | ---: | ---: |
| 4 | 4.4 µs | 0.45 µs |
| 64 | 4.8 µs | 3.9 µs |
| 256 | 7.0 µs | 14.4 µs |
| 1024 | 10.8 µs | 56.6 µs |

DMA has a fixed cost of about 4.4 µs, and the CPU is free during the transfer:
the "done" interrupt itself costs 1.33 µs. The rule we adopted: **a transfer
bigger than the peripheral's hardware FIFO uses DMA; one that fits uses the
FIFO.** UART receive streams are always bigger than the FIFO, so the next piece
of work is UART receive by DMA into four 64-byte buffers, with an end-of-frame
timeout to deliver partial frames. That ring is designed but **not built yet**;
today UART3 still receives by interrupt.

## 11. Measured on the board

All on the Cubie A5E with the E906 at 200 MHz, 2026-10-21 to 2026-10-23:

| What | Result |
| :--- | :--- |
| Idle board | 0.01 % busy; 0.3-0.4 % with the 1 kHz profiler on |
| MicroPython sensor streamer (reads every 100 ms) | `repl_rx` about 7.6 % |
| MicroPython tight compass and GPS loop | `repl_rx` about 90 % |
| I²C (compass) interrupt | 1.40-1.43 µs each; 11 per 6-byte register read |
| UART3 (GPS) interrupt at 115,200 baud | 50 µs when the driver checked status before every byte; 31.8 µs reading the FIFO level once |
| One UART register read from the E906 | about 0.29 µs |
| Studio CLI round trip | about 55 ms |
| DMA "done" interrupt | 1.33 µs |
| Board self-test | 31 passed, 0 failed, 2 skipped |

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

Neither problem showed up in tests on the PC. Both were found on the board and
are written up in the
[engineering log](https://github.com/tcmichals/AbstractX/blob/main/engineering_log.md)
(2026-10-22).

## 13. Limits and what is not done yet

- **UART speed:** the UART clock (APB1) is 24 MHz on this board, which limits
  UARTs to 1.5 Mbit/s. Faster serial needs Linux to raise that clock.
- **MicroPython is slow in tight loops:** about 90 % of the E906 for a busy
  compass and GPS loop. It is for experiments and bring-up; real-time loops
  belong in C++ coroutines on the same drivers.
- **UART receive DMA:** designed, not built.
- **SPI DMA:** the SPI driver does not use the corrected DMA addresses yet.
- **Sensor packets:** Linux still builds the 64-byte sensor packets from text
  the script prints; having the script send them itself is planned.
- **Profiler label:** idle time is shown under the wrong function name.

## 14. Where to go next

- **The series:** [Part 1](https://www.hackster.io/MichalsTC/unlocking-the-xuantie-risc-v-core-on-allwinner-t527-part-1-976063) (architecture, memory, boot), [Part 2](https://www.hackster.io/MichalsTC/bringing-up-heterogeneous-risc-v-on-allwinner-socs-part-2-53deb8) (remoteproc), [Part 3](https://github.com/tcmichals/cubie-a5e/blob/main/docs/articles/part3_baremetal_firmware_ipc_and_coroutines_intro.md) (message box and RPMsg), [Part 4](https://github.com/tcmichals/cubie-a5e/blob/main/docs/articles/part4_deep_dive_baremetal_cpp_coroutines.md) (coroutines vs FreeRTOS), [device-tree overlays](https://github.com/tcmichals/cubie-a5e/blob/main/docs/articles/devicetreeOverlay.md) (U-Boot and `config.txt`)
- **The code:** [E906 MicroPython app](https://github.com/tcmichals/AbstractX/tree/main/apps/e906_repl_app) (wiring, overlays, build, Studio), [AbstractX Studio](https://github.com/tcmichals/AbstractX/tree/main/tools/visualizer), [board tests](https://github.com/tcmichals/AbstractX/tree/main/tests/board) (including `e906_link.py` and the timing scripts)
- **The same sensors in C++:** [Straight-Line Async: GPS + Compass with C++20 Coroutines](https://github.com/tcmichals/AbstractX/blob/main/docs/articles/gps-compass-straight-line-async/README.md)
- **Bare metal vs FreeRTOS on this core:** [AbstractX on the E906 with and without FreeRTOS](https://github.com/tcmichals/AbstractX/blob/main/docs/articles/BAREMETAL_VS_FREERTOS_E906.md)
- **For engineers who want the exact requirements:** the [E906 app specification](https://github.com/tcmichals/AbstractX/blob/main/apps/e906_repl_app/SPECIFICATION.md) and the [E906 target specification](https://github.com/tcmichals/AbstractX/blob/main/targets/allwinner_e906/SPECIFICATION.md) define everything this article describes, and say how each part is tested.
