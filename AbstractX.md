# AbstractX: Complete Architecture & Deep-Dive Specification

> **The Architectural Deep Dive: Emulating a Packet-Switched PCIe Fabric on Embedded Silicon with Zero Dynamic Heap, C++20 Coroutines, and Closed-Loop AI SpecTrace.**

---

## Table of Contents
1. [The Vision: Emulating a Massive PCIe Fabric for Embedded Silicon](#1-the-vision-emulating-a-massive-pcie-fabric-for-embedded-silicon)
2. [Architectural Principles: Limiting Memory & Maximizing Processor Speed](#2-architectural-principles-limiting-memory--maximizing-processor-speed)
3. [The "Middle Ground": Defeating Vendor SDK & RTOS Bloat](#3-the-middle-ground-defeating-vendor-sdk--rtos-bloat)
4. [The Zephyr Developer's Rosetta Stone](#4-the-zephyr-developers-rosetta-stone)
5. [Asymmetric Multiprocessing (AMP) & The "PCIe Switch" I/O Architecture](#5-asymmetric-multiprocessing-amp--the-pcie-switch-io-architecture)
6. [Why Traditional RTOSes Miss Distributed Multi-Processor I/O](#6-why-traditional-rtoses-miss-distributed-multi-processor-io)
7. [Polymorphic I/O: From C++ Coroutines to SystemVerilog RTL](#7-polymorphic-io-from-c-coroutines-to-systemverilog-rtl)
8. [Unified Design Pattern: Hardware-Software Co-Design](#8-unified-design-pattern-hardware-software-co-design)
9. [C++20 Coroutines & The Static Task Graph](#9-c20-coroutines--the-static-task-graph)
10. [The Primary-Paced Channel Pattern](#10-the-primary-paced-channel-pattern)
11. [Cross-Platform Silicon Scaling & Execution Matrix](#11-cross-platform-silicon-scaling--execution-matrix)
12. [FPGA Switch Fabric & 64-Byte TLP Integration](#12-fpga-switch-fabric--64-byte-tlp-integration)
13. [Telemetry, Dynamic Barectf CTF 1.8 & Observability](#13-telemetry-dynamic-barectf-ctf-18--observability)
14. [SpecTrace: 100% Specification-to-Code Traceability](#14-spectrace-100-specification-to-code-traceability)
15. [Repository Map & Navigation](#15-repository-map--navigation)

---

## 1. The Vision: Emulating a Massive PCIe Fabric for Embedded Silicon

AbstractX is built around a single, game-changing architectural concept: **What if your entire embedded system behaved like a massive PCIe bus topology with switches, endpoints, and transaction packets?**

In high-performance servers, CPUs do not wire point-to-point bespoke GPIOs or poll blocking hardware buses; they communicate across a **packet-switched PCIe fabric** using standardized **Transaction Layer Packets (TLPs)** routed through **PCIe Switches** to dedicated IP endpoints.

AbstractX brings this exact data-center computing model to microcontrollers, coprocessors, and FPGAs:
* **The Fabric**: Standardized 64-byte TLPs flowing across lock-free SPSC/MPSC rings (in shared SRAM, over SPI, or through FPGA AXI-Stream crossbars).
* **The Switch**: The I/O Processor (RP2350 Core 0, XuanTie E906 RISC-V, or FPGA `asp_router.sv`) acts as the **PCIe Switch**, decoding virtual BAR addresses and channel tags to forward transactions directly to downstream IP blocks (UART, DMA, SPI, GPIO/LED).
* **The Endpoints**: Hardware peripherals (IMU Auto-DMA, ESC controllers, LEDs) appear as clean endpoints on a virtual PCIe bus.
* **Split Transactions**: Application loops never wait for slow physical hardware clocking; they fire non-blocking posted writes (`MemWrite`) or await tagged completions (`CplD`).

---

## 2. Architectural Principles: Limiting Memory & Maximizing Processor Speed

Traditional embedded architectures often rely on a Real-Time Operating System (RTOS) and spawn separate threads for every sensor and I/O bus. While an RTOS is a powerful, proven tool, the "thread-per-bus" approach inherently requires dedicating 2–4 KB of SRAM per task and consumes hundreds of clock cycles for every OS context switch.

AbstractX achieves extreme concurrency differently by introducing the **Coroutine-Native HAL**. Inspired by high-performance server paradigms (like Boost.Asio or epoll), it brings event-driven deterministic scheduling to bare-metal. By replacing OS threads with C++20 compiler-generated state machines, AbstractX achieves two fundamental hardware goals:
1. **Limit Memory Usage**: A suspended coroutine frame consumes less than 100 bytes of static memory (in `.bss`), running the entire system on a single shared 2 KB stack. It enforces **0 bytes of dynamic heap allocation**.
2. **Maximize Processor Speed**: By removing OS preemption, a context switch becomes a direct 15-cycle function pointer jump. Because the CPU never spins in blocking `while(busy)` loops, 100% of the processor's clock cycles are dedicated to useful work (like AHRS matrix math), extracting maximum theoretical throughput from the silicon.

---

## 3. The "Middle Ground": Defeating Vendor SDK & RTOS Bloat

Modern RTOSes (like **Zephyr**) attempt to fix thread bloat using event-driven "Workqueues". However, this forces developers into a painful compromise: write unreadable callback spaghetti to save memory, or spawn heavy threads to get readable, linear code. Furthermore, vendor-provided RTOS environments (like the **Allwinner SDK for the E906 RISC-V core**) often introduce massive binary footprints that suffocate tight silicon.

AbstractX occupies the perfect **"Middle Ground"**: It provides the extreme, ultra-low memory footprint of raw bare-metal callbacks, but delivers the linear, highly-concurrent readability of a modern RTOS thread. AbstractX can run entirely bare-metal to replace a bloated vendor SDK, or it can drop smoothly inside a *single* Zephyr/FreeRTOS thread to handle ultra-high-speed sensor fusion without exhausting the system's memory budget.

---

## 4. The Zephyr Developer's Rosetta Stone

If you are coming from **Zephyr RTOS**, the concepts in AbstractX map directly to the tools you already know—elevated with modern C++20 language features and hardware synthesis:

| Zephyr Concept | AbstractX Equivalent | Key Difference & Advantage |
| :--- | :--- | :--- |
| **Workqueue (`k_work_q`)** | **`abstractx::step()` Event Loop** | Single cooperative loop; runs the entire system on a shared 2 KB stack with zero heap allocation. |
| **Work Item (`k_work`) & Callback** | **C++20 Coroutine (`Task<T>`)** | **No callback spaghetti**: instead of breaking algorithms into `void (*handler)(struct k_work*)`, C++20 `co_await` preserves local state and reads as a sequential, linear function. |
| **Message Queue (`k_msgq`)** | **Lock-Free Ring (`AsyncQueue`, `SpscTlpRing`)** | Single-Producer Single-Consumer static lock-free circular ring; zero mutexes, zero kernel critical sections, ISR/DMA-safe. |
| **Zephyr Bus (`zbus`)** | **64-Byte TLP + Switch Fabric** | `zbus` is a pure **software-in-RAM** pub/sub bus passing C structs. AbstractX's **TLP** is a **hardware-synthesizable, PCIe-style data plane** (20B header, 40B payload, 4B CRC32) routable across FPGA crossbars, shared SRAM, and DMA. |
| **Thread Sleep (`k_sleep`)** | **Async Yield (`co_await timer.sleep()`)** | Never blocks the CPU or yields thread context; simply registers a timestamp deadline and returns execution to the event loop. |

### Does Zephyr Have a TLP-like Structure?
**No.** While Zephyr provides `zbus` for intra-MCU pub/sub messaging and `net_buf` for network packets, these are **pure software abstractions** bound to the local processor's memory space. 

In contrast, the AbstractX **64-byte TLP (Transaction Layer Packet)** is a **unified hardware-software bus routing data plane**:
* **PCIe Switch Architecture on the I/O Processor**: The I/O Processor (whether Core 0 on RP2350, XuanTie E906 RISC-V, or an FPGA crossbar) functions as an **on-silicon PCIe Root Complex / PCIe Switch**. The Application Processor does not invoke ad-hoc RPCs or poke physical peripheral registers; it emits standard bus TLPs. The I/O Processor inspects the routing header (`channel`, `target_address`, `tag`) and **forwards the transaction directly to the actual hardware IP block** (UART IP, SPI DMA engine, I2C core, GPIO/LED controller).
* **Virtual BAR Address Map**: Just like PCIe endpoints behind a PCIe switch, peripherals occupy a standardized virtual BAR address map (`bar::LedBase`, `bar::SerialBase`, `bar::ImuBase`, `bar::EscBase`).
* **Synthesizable in Hardware**: The exact same 64-byte routing logic synthesizes into an FPGA AXI-Stream switch fabric (`rtl/asp_router.sv`) with nanosecond hardware timestamps, split-transaction completions (`CplD`), and hardware CRC32.
* **Silicon-Agnostic Wire Framing**: Moves across cores via zero-copy shared SRAM rings, over physical SPI/UART busses, or directly into high-speed DMA burst engines without any CPU serialization or software `memcpy`.

---

## 5. Asymmetric Multiprocessing (AMP) & The "PCIe Switch" I/O Architecture

For extreme performance, AbstractX completely decouples application math (AHRS / Flight Control) from hardware peripheral manipulation using **Asymmetric Multiprocessing (AMP)** communicating over lock-free **SPSC Rings** and **64-byte TLPs**:
* **The App Processor (Core 1 / Linux)**: Runs linear high-speed estimation and flight control algorithms. When it needs to interact with hardware (e.g. transmit an ESC serial command, read a sensor register, or toggle an LED), it constructs a 64-byte TLP and pushes it into the ring.
* **The I/O Processor (Core 0 / RISC-V / FPGA)**: Acts as the **PCIe Switch / Bus Master**. It consumes TLPs from the ring, decodes the target endpoint from the virtual BAR map or channel ID, and routes the transaction directly onto the destination IP core (UART, SPI DMA, GPIO). If a read is requested, it routes a completion TLP (`CplD`) back to the App Processor.

```text
┌────────────────────────────────────────────────────────────────────────┐
│                   APPLICATION PROCESSOR (Core 1 / Linux)               │
│         High-Speed Sensor Fusion Loop / Flight Control Math            │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ 64-Byte TLP (target_address, tag)
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                 LOCK-FREE SPSC RING (Shared SRAM / Mailbox)            │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Dequeue TLP
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│          I/O PROCESSOR (Core 0 / XuanTie E906 / asp_router.sv)         │
│                        [ PCIe SWITCH / ROUTER ]                        │
│   Decodes Channel & BAR Address ────────► Routes to Downstream IP      │
└───┬───────────────────────────────┬──────────────────────────────┬─────┘
    │                               │                              │
    ▼                               ▼                              ▼
┌────────────────┐          ┌────────────────┐             ┌────────────────┐
│  UART / ESC IP │          │  SPI / DMA IP  │             │  GPIO / LED IP │
│  (bar::EscBase)│          │  (bar::ImuBase)│             │  (bar::LedBase)│
└────────────────┘          └────────────────┘             └────────────────┘
```

---

## 6. Why Traditional RTOSes Miss Distributed Multi-Processor I/O

Modern SoCs are inherently asymmetric (e.g. Raspberry Pi Pico 2 dual-core RP2350, ESP32-P4 dual RISC-V, Allwinner Radxa Cubie A5E Linux + XuanTie E906 RISC-V, or Zynq ARM + FPGA). Yet traditional RTOSes were conceived for a **single monolithic CPU executing local driver APIs**.

When developers need to run I/O on a secondary processor in Zephyr:
1. **The Zephyr Approach (The RPC Trap)**:
   * Zephyr requires running an independent kernel instance on each core.
   * To interact with hardware on Core 0 from Core 1, Zephyr relies on **OpenAMP / RPMsg**.
   * The developer must invent a custom serialization layer, write manual endpoint message handlers, and bridge byte buffers into driver API calls (`uart_tx`, `gpio_pin_set`).
   * **Result**: Massive memory footprint (two full RTOS kernel stacks), heavy serialization overhead, and fragmented, brittle application glue code.

2. **The AbstractX Approach (The Distributed PCIe Bus Fabric)**:
   * AbstractX recognizes that **I/O should not be bound to the application CPU**.
   * Because the entire interconnect is an emulated **PCIe Bus with a Virtual BAR Map**, the Application Processor has **zero knowledge of where the physical peripheral pins live**.
   * Whether an ESC UART or IMU SPI peripheral is on-chip, routed to Core 0, running on an external RISC-V coprocessor, or synthesized into an FPGA crossbar:
     * The App Processor simply emits a 64-byte `MemWrite` or `MemRead` TLP to `bar::EscBase` or `bar::ImuBase`.
     * The **I/O Processor acts as the PCIe Switch**: it drains the ring, decodes the BAR address, and forward-dispatches the transaction directly into the destination hardware IP.
   * **Result**: Zero serialization code, zero RPC boilerplate, zero dual-OS memory bloat, and total silicon portability.

---

## 7. Polymorphic I/O: From C++ Coroutines to SystemVerilog RTL

Because the boundary between the App Processor and the I/O Processor is a standardized 64-byte TLP lock-free ring, the I/O layer is completely **Polymorphic**. You can scale your architecture seamlessly:
1. **Tier 1 (MCU C++ Coroutines)**: The I/O Processor runs C++20 coroutines on a secondary microcontroller core (e.g., Pico 2 Core 0 or ESP32-P4 Core 0), dispatching to chip peripherals.
2. **Tier 2 (FPGA SystemVerilog)**: The exact same I/O design pattern drops into Silicon on an FPGA (e.g., Zynq 7000, Gowin). The C++ I/O coroutines are replaced by the hardware crossbar switch fabric (`asp_router.sv`) and hardware state machines.

To the Application Processor, a C++ coroutine I/O switch and a SystemVerilog hardware crossbar look identical. Both are formally verified by the **SpecTrace AI Anti-Drift Engine** to guarantee 100% architectural compliance.

---

## 8. Unified Design Pattern: Hardware-Software Co-Design

In AbstractX, **hardware and software share the exact same asynchronous, non-blocking execution model**:

```mermaid
flowchart LR
    subgraph HW["FPGA / HARDWARE SWITCH FABRIC"]
        direction TB
        H1["<b>Autonomous AXI-Stream Router</b><br/><code>rtl/asp_router.sv</code><br/>Non-blocking crossbar packet routing"]
        H2["<b>Auto-DMA Burst Engines</b><br/><code>rtl/asp_imu_auto_dma.sv</code><br/>Autonomous SPI burst clocking & DRDY latching"]
        H3["<b>Split-Transaction Handshakes</b><br/><code>tvalid</code> / <code>tready</code> hardware credit flow"]
        H4["<b>64-Byte TLP Packetization</b><br/>Hardware nanosecond timestamps & CRC32"]
        H1 --- H2 --- H3 --- H4
    end

    subgraph PLANE["THE UNIFIED ABSTRACTX DATA PLANE<br/><b>PCIe-Style 64-Byte Transaction Layer Packets (TLP)</b>"]
        direction TB
        P_HDR["<b>20-Byte TLP Header</b><br/>Type • Flags • Tag • Channel • Target Addr • 64-bit ns Timestamp"]
        P_PAYLOAD["<b>40-Byte Binary CTF 1.8 Event Payload</b><br/>Zero-copy barectf binary frame"]
        P_CRC["<b>4-Byte IEEE 802.3 CRC32</b><br/>End-to-end hardware integrity validation"]
        P_HDR --> P_PAYLOAD --> P_CRC
    end

    subgraph SW["PROCESSOR / FIRMWARE DOMAIN"]
        direction TB
        S1["<b>C++20 Stackless Coroutines</b><br/><code>include/asp_coro.hpp</code><br/>Linear sequential task graphs"]
        S2["<b>Async Lock-Free Channels</b><br/><code>AsyncQueue(T, N)</code> & <code>SpscTlpRing(64)</code><br/>Bounded static circular buffers"]
        S3["<b>Non-Blocking Suspension</b><br/><code>co_await</code> cooperative yield without thread sleep"]
        S4["<b>Deterministic Zero-Heap Model</b><br/>0 Bytes dynamic allocation • 2 KB shared stack"]
        S1 --- S2 --- S3 --- S4
    end

    H4 <-->|Hardware AXI-Stream Bus| P_HDR
    P_CRC <-->|Lock-Free Ring Buffers| S1

    classDef hwStyle fill:#1e1b4b,stroke:#818cf8,stroke-width:2px,color:#ffffff;
    classDef planeStyle fill:#312e81,stroke:#c084fc,stroke-width:2px,color:#ffffff;
    classDef swStyle fill:#0f172a,stroke:#38bdf8,stroke-width:2px,color:#ffffff;

    class H1,H2,H3,H4 hwStyle;
    class P_HDR,P_PAYLOAD,P_CRC planeStyle;
    class S1,S2,S3,S4 swStyle;
```

```mermaid
flowchart TD
    subgraph DRIVER_DOMAIN["Event-Driven Hardware & Drivers (Non-Blocking Dispatch)"]
        direction LR
        PIN_INT["<b>Physical DIO / ISR Trigger</b><br/>Sensor DRDY / External Pin Event"]
        DMA_ENG["<b>Autonomous Hardware DMA / PIO</b><br/>Hardware burst clocking • Zero CPU wait states<br/><i>Latches nanosecond hardware timestamp</i>"]
        TLP_GEN["<b>64B TLP Packetizer</b><br/>Encapsulates binary event payload<br/>Direct hardware framing"]
        PIN_INT --> DMA_ENG --> TLP_GEN
    end

    subgraph INTERCONNECT["Lock-Free Inter-Domain Bridge (Static Memory)"]
        direction LR
        SPSC_RING["<b>Lock-Free SPSC Ring (SpscTlpRing(64))</b><br/>Atomic head/tail pointers • Zero mutex locks"]
        DOORBELL["<b>Event Signal / Doorbell</b><br/>Wakes event loop without thread preemption"]
        TLP_GEN -->|push| SPSC_RING
        TLP_GEN -->|signal| DOORBELL
    end

    subgraph EVENT_LOOP["Simple Poll Event Loop (abstractx::step())"]
        direction TB
        DISPATCHER["<b>Event Dispatcher & Poll Loop</b><br/>Processes events • Sole caller of <code>.resume()</code>"]
        
        subgraph TASKS["Linear C++20 Coroutine Task Graph"]
            direction LR
            T_PRIMARY["<b>primary_ingress_task</b><br/><code>co_await sensor.next_sample_async()</code>"]
            T_PROCESS["<b>stream_processor_task</b><br/><code>co_await g_primary_channel.pop()</code><br/>Deterministic Processing & Control Loop"]
            T_EGRESS["<b>telemetry_egress_task</b><br/>Decimated TLP UDP egress"]
            T_PRIMARY -->|try_push| T_PROCESS --> T_EGRESS
        end
        
        DISPATCHER --> TASKS
    end

    DOORBELL -.->|Wakes without OS preemption| DISPATCHER
    SPSC_RING -.->|Zero-copy dequeue| T_PRIMARY

    classDef hwStyle fill:#1e1b4b,stroke:#818cf8,stroke-width:2px,color:#ffffff;
    classDef ringStyle fill:#14532d,stroke:#4ade80,stroke-width:2px,color:#ffffff;
    classDef swStyle fill:#0f172a,stroke:#38bdf8,stroke-width:2px,color:#ffffff;

    class PIN_INT,DMA_ENG,TLP_GEN hwStyle;
    class SPSC_RING,DOORBELL ringStyle;
    class DISPATCHER,T_PRIMARY,T_PROCESS,T_EGRESS swStyle;
```

---

## 9. C++20 Coroutines & The Static Task Graph

### Why Coroutines Replace Preemptive RTOS Threads

```mermaid
flowchart LR
    subgraph RTOS["Traditional Preemptive RTOS (Heavyweight & Jitter-Prone)"]
        direction TB
        T1["<b>Thread 1: High-Rate Ingress</b><br/>4 KB Dedicated Stack"]
        T2["<b>Thread 2: Auxiliary Comms</b><br/>2 KB Dedicated Stack"]
        T3["<b>Thread 3: State Estimation</b><br/>4 KB Dedicated Stack"]
        T4["<b>Thread 4: Digital Filtering</b><br/>8 KB Dedicated Stack"]
        T5["<b>Thread 5: Telemetry Egress</b><br/>4 KB Dedicated Stack"]
        SCHED["<b>Preemptive RTOS Scheduler</b><br/>• Periodic timer tick interruptions<br/>• Expensive context switches & cache thrashing<br/>• Mutex priority inversion hazards<br/>• <b>over 22 KB SRAM wasted on idle stacks</b>"]
        T1 --> SCHED
        T2 --> SCHED
        T3 --> SCHED
        T4 --> SCHED
        T5 --> SCHED
    end

    subgraph CORO["AbstractX Static Coroutine Graph (Deterministic & Zero-Heap)"]
        direction TB
        STACK["<b>Single Shared CPU Stack</b><br/>Only 2 KB total stack space allocated"]
        FRAMES["<b>Static Coroutine Frames (BSS)</b><br/>under 120 Bytes per suspended task<br/>Statically pooled in data segment"]
        DISP["<b>Cooperative Event Dispatcher</b><br/>• Microsecond awakening via doorbells<br/>• 0 OS context switch overhead<br/>• 0 Bytes dynamic heap allocation<br/>• <b>100% Deterministic execution</b>"]
        STACK --> DISP
        FRAMES --> DISP
    end

    classDef rtosNode fill:#450a0a,stroke:#f87171,stroke-width:2px,color:#ffffff;
    classDef coroNode fill:#064e3b,stroke:#34d399,stroke-width:2px,color:#ffffff;

    class T1,T2,T3,T4,T5,SCHED rtosNode;
    class STACK,FRAMES,DISP coroNode;
```

```mermaid
sequenceDiagram
    autonumber
    participant HW as Hardware Bus (SPI DMA / PIO)
    participant Driver as Non-Blocking Driver (DMA/ISR)
    participant Ring as Lock-Free SPSC Ring
    participant EventLoop as Main Dispatch Event Loop (Coroutines)

    EventLoop->>EventLoop: stream_processor_task awaits next sample
    Note over EventLoop: co_await g_primary_channel.pop()<br/>Task suspends cooperatively (0 CPU wasted)
    
    HW->>Driver: Physical Sensor DRDY Interrupt (DIO)
    Driver->>HW: Dispatches autonomous DMA burst clocking
    HW-->>Driver: Burst transfer complete
    Driver->>Ring: Places 64B TLP event onto event ring
    Driver->>EventLoop: Signals event loop doorbell
    
    Note over EventLoop: Event loop prioritizes & awakens coroutine handle
    Ring-->>EventLoop: pop() -> SampleData
    EventLoop->>EventLoop: Executes linear processing & control actuation
```

---

## 10. The Primary-Paced Channel Pattern

Heterogeneous physical data sources operate at vastly differing hardware sample rates:
* **Primary High-Rate Stream**: 1 kHz – 8 kHz (SPI DMA / PIO) → The high-speed **Physical Clock Pacer**
* **Auxiliary Medium-Rate Stream**: 50 Hz – 100 Hz (I2C / CAN) → Periodic auxiliary telemetry / reference
* **Low-Rate Configuration / Navigation Stream**: 5 Hz – 10 Hz (UART) → Low-frequency updates

AbstractX eliminates the legacy nightmare of modulus tick counters (`if (++tick % 80 == 0)`), fragmented callback state machines, and spinlocks by introducing the **Primary-Paced Channel Pattern**:

```cpp
// 100% Linear, Deterministic Multi-Rate Stream Processing (0 Bytes Dynamic Heap)
Task<void> stream_processor_task(hal::ITimer& timer, StateEstimator& estimator) {
    while (true) {
        // 1. Asynchronously await next high-rate packet (Physical Clock Pacer)
        PrimarySample sample = co_await g_primary_channel.pop();
        estimator.update_primary(sample, dt);

        // 2. Non-blockingly drain whatever auxiliary stream packets arrived
        AuxSample aux;
        while (g_aux_channel.try_pop(aux)) {
            estimator.update_aux(aux);
        }

        NavSample nav;
        while (g_nav_channel.try_pop(nav)) {
            estimator.update_nav(nav);
        }

        // 3. Emit 64-byte TLP into telemetry stream
        g_telemetry_ring.push(StateEstimator::to_tlp(estimator.state()));
    }
}
```

---

## 11. Cross-Platform Silicon Scaling & Execution Matrix

AbstractX applications are written against the universal `abstractx::` API and compile identically with **zero application-level `#ifdef` directives** across multiple silicon topologies:

| Metric | [Raspberry Pi Pico 2 W](docs/tier3_targets/bsp/PICO2W_DUAL_CORE_ARCHITECTURE.md) | [Espressif ESP32-P4](docs/tier3_targets/hardware/ESP32P4_WAVESHARE_WIFI6_AND_ERRATA_SPEC.md) | [Radxa Cubie A5E (Pure Silicon)](docs/tier3_targets/bsp/E906_COPROCESSOR_ARCHITECTURE.md) | [Radxa Cubie A5E + FPGA](docs/tier3_targets/hardware/TANG9K_PINOUT.md) | [AMD Zynq / Gowin Tang](docs/tier3_targets/hardware/TANG9K_PINOUT.md) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Silicon Architecture** | Dual ARM Cortex-M33 @ 150 MHz | Dual RISC-V @ 400 MHz | Quad AArch64 A55 + RISC-V E906 (No FPGA) | Quad AArch64 A55 + E906 + FPGA Fabric | Dual ARM Cortex-A9 + FPGA Fabric |
| **Floating-Point Engine** | Hardware single-precision FPU | Hardware single/double FPU | Hardware ARM NEON FPU | Hardware ARM NEON FPU + FPGA DSPs | Hardware VFPv3 FPU + FPGA DSPs |
| **Tier 1 I/O Engine** | Core 0 (PIO DMA + CYW43) | Core 0 (GDMA + Wi-Fi 6) | XuanTie E906 (On-Chip SPI1, TWI1, UART3 DMA) | FPGA Logic (`asp_imu_auto_dma.sv` + AXI) | FPGA Logic (`asp_imu_auto_dma.sv`) |
| **Tier 2 Coroutine Engine** | Core 1 (Coroutine Dispatcher) | Core 1 (Coroutine Dispatcher) | Core 0 (Linux PREEMPT_RT Thread) | Core 0 (Linux Userspace / E906) | Core 0 (Linux Userspace / RTOS) |
| **Inter-Domain Bridge** | Hardware SIO FIFO Doorbell | Hardware IPC Mailbox | Shared SRAM A3/C + `sun6i-msgbox` | AXI-Stream TLP Descriptors & DMA | AXI-Stream DMA Descriptor Rings |
| **FPGA Requirement** | None (0 LUTs) | None (0 LUTs) | **None (Pure SoC Silicon, 0 LUTs)** | **Gowin / AMD FPGA (~9k–85k LUTs)** | Gowin / AMD FPGA (~9k–85k LUTs) |
| **Static Memory Footprint** | `< 1 KB` SRAM (Estimator + Rings) | `< 1 KB` SRAM (Estimator + Rings) | `< 1 KB` SRAM (Estimator + Rings) | `< 1 KB` SRAM (Estimator + Rings) | `< 1 KB` SRAM (Estimator + Rings) |
| **Loop Step Latency** | **4.2 µs** | **1.8 µs** | **0.8 µs** | **0.4 µs** (Hardware Offloaded) | **0.4 µs** (Hardware Offloaded) |

---

## 12. FPGA Switch Fabric & 64-Byte TLP Integration

In AbstractX, **the FPGA is really just all hardware drivers**. Rather than burning CPU cycles executing software driver routines, autonomous synthesizable SystemVerilog cores act as pure hardware drivers on Wishbone / AXI-Stream buses:
* **[`rtl/asp_top.sv`](rtl/asp_top.sv)**: Top-level switch fabric wrapper interconnecting Wishbone / AXI-Stream buses.
* **[`rtl/asp_router.sv`](rtl/asp_router.sv)**: Full-crossbar AXI-Stream router switching packets based on 64B TLP channel tags.
* **[`rtl/pio/asp_pio_tile.sv`](rtl/pio/asp_pio_tile.sv)**: **Autonomous Programmable I/O Tile** with 4 concurrent state machines running RP2040/RP2350 ISA microcode. Delivers **0.0% CPU overhead** for 8 kHz IMU sampling with single-cycle 512-bit TLP auto-DMA, saving **~45% FPGA logic** (~940 LUTs / ~700 FFs vs separate hardwired cores).
* **[`rtl/imu/asp_imu_auto_dma.sv`](rtl/imu/asp_imu_auto_dma.sv)**: Dedicated hardware Auto-DMA driver core latching DIO pin triggers (IMU DRDY) and clocking SPI bursts without CPU intervention.
* **[`rtl/motor/asp_dshot_core.sv`](rtl/motor/asp_dshot_core.sv)**: 4-Channel hardware DShot actuator driver with bidirectional telemetry & PWM generation.
* **[`rtl/serial/asp_uart_core.sv`](rtl/serial/asp_uart_core.sv)**: High-speed full-duplex UART core for GPS and RC receiver telemetry.

```text
0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Type (1B)    |  Flags (1B)   |   Tag (1B)    | Channel (1B)  |  Header (20B)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Target Address (32-bit)                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       Length DW (16-bit)      |      Sequence ID (16-bit)     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Timestamp Low (32-bit ns)                  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Timestamp High (32-bit ns)                 |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|             Payload (40 Bytes CTF 1.8 Binary Event)           |  Payload (40B)
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       IEEE 802.3 CRC32                        |  CRC (4B)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

---

## 13. Telemetry, Dynamic Barectf CTF 1.8 & Observability

Real-time embedded loops must **never perform string formatting or blocking socket operations**. Instead, AbstractX adopts the **Common Trace Format (CTF 1.8) via Barectf** as the universal binary telemetry format across both silicon and software, rendered at 120 FPS in **AbstractX Studio** via `imgui-bundle`.

### Dynamic YAML/JSON Trace Schema
The telemetry format is completely self-describing via [`trace/barectf_config.yaml`](trace/barectf_config.yaml) and [`apps/gps_imu_app/trace_schema.json`](apps/gps_imu_app/trace_schema.json). Every field specifies:
* Primitive binary types (`uint32`, `int16`, `int32`, `uint8`)
* Scaling multipliers (e.g. `0.01` for centidegrees → degrees, `1e-7` for coordinates)
* Physical engineering units (`deg`, `m`, `m/s`, `g`, `deg/s`, `us`)
* UI widget bindings (`artificial_horizon`, `altimeter`, `compass`, `oscilloscope`, `throttle_bar`)

---

## 14. SpecTrace: 100% Specification-to-Code Traceability

AbstractX treats Markdown specifications as the absolute **Single Source of Truth (SSOT)**:
1. **Authoritative Specification (`[SPEC-*]`)**: Requirements define architecture before code is written.
2. **AI Prompts & Workspace Invariants (`AGENTS.md`)**: Automatically loaded by AI coding agents on every turn.
3. **In-Code Implementation Traceability (`@impl`)**: Code tags tie directly to specification requirements.
4. **Automated Verification (`tools/audit_specs.py`)**: CI gate proving 100% specification-to-code parity.
5. **SpecTrace Closed Loop**: When bugs or defects occur, the mistake is cataloged in `engineering_log.md`, classified, and fed back into the specification *first* so AI agents never repeat the same error.

For comprehensive details on the SpecTrace methodology, read [`docs/SPECTRACE.md`](docs/SPECTRACE.md) and [`docs/SPECTRACE_GUIDE.md`](docs/SPECTRACE_GUIDE.md).

---

## 15. Repository Map & Navigation

* **Architectural Handbook & Polymorphic Design**: [`docs/ARCHITECTURE_AND_EXTENSIBILITY.md`](docs/ARCHITECTURE_AND_EXTENSIBILITY.md)
* **AI Agent Invariants & Rules**: [`AGENTS.md`](AGENTS.md)
* **SpecTrace Guide**: [`docs/SPECTRACE_GUIDE.md`](docs/SPECTRACE_GUIDE.md)
* **Application Library (`applib/`)**: Concrete peripheral chip drivers (C++ and Python) in [`applib/`](applib/)
* **AbstractX Core Headers**: [`include/abstractx/`](include/abstractx/)
* **Hardware RTL**: [`rtl/`](rtl/) and [`rtl/pio/`](rtl/pio/)
* **Target BSPs**: [`targets/`](targets/)
* **AbstractX Studio**: [`tools/visualizer/abstractx_studio.py`](tools/visualizer/abstractx_studio.py)
