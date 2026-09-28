# Design Prompt: Markdown Drives the Code (Spec-Driven Architecture)

## 1. Core Principle: Single Source of Truth (SSOT)
In this repository, **Markdown documentation is the authoritative design specification that drives all code implementations.**

- **Architecture, protocols, hardware memory maps, and state machines are defined in Markdown first** using clean GitHub Markdown and Mermaid diagrams.
- **Source code (`.hpp`, `.cpp`, `.S`, `.sv`) must be lean, minimal, and free of essay-style duplicate comments.**
- Code comments should be strictly minimal (1–2 lines) and provide direct cross-reference links back to the authoritative Markdown document (e.g. `// Spec: docs/ARCHITECTURE.md#split-queue-model`).

---

## 2. Multi-Target Top-Level Application Architecture
```mermaid
flowchart TD
    subgraph APP["Universal Flight Application Layer (C++20 Coroutines • Zero Dynamic Heap)"]
        direction TB
        APP_CODE["<b>FlightApp / inav-abstractx</b><br/>• Single codebase across all platforms<br/>• Sensor setup & calibration (ICM-42688-P, U-Blox GPS)<br/>• 8 kHz Rate PID, EKF3 Navigation, State Machines<br/>• <code>co_await imu.next_sample()</code> • <code>co_await gps.next_fix()</code>"]
    end

    subgraph ROUTER["AbstractX Transport & Routing Layer (64-Byte Split-Transaction TLPs)"]
        direction TB
        TLP_ROUTER["<b>AbstractX Channel / SpscTlpRing</b><br/>Lock-free memory-mapped queues (MemRd, MemWr, CplD, DMA_Stream)"]
    end

    APP -->|Dispatches Requests / Awaits Completions| ROUTER

    subgraph PLATFORMS["Target Interconnect & Queuing Topologies"]
        direction LR
        
        subgraph LINUX_PLAT["1. Linux SBC (Cubie A5E)"]
            direction TB
            L_HOST["<b>ARM64 Host (A55)</b><br/>Flight Daemon (PREEMPT_RT)"]
            L_IPC["<b>RemoteProc & Shared SRAM A3/C</b><br/>Lock-free SPSC Descriptors + MSGBox IRQ"]
            L_COPROC["<b>XuanTie E907 Co-Processor (600 MHz)</b><br/>• SPI DMA (ICM-42688-P @ 8 kHz)<br/>• S_UART0 (U-Blox GPS)<br/>• DRDY Hardware Pin ISR"]
            L_HOST <-->|Shared SRAM + Doorbell| L_IPC <--> L_COPROC
        end

        subgraph PICO_PLAT["2. Pico 2 W (RP2350)"]
            direction TB
            P_CORE1["<b>Core 1: Worker Core</b><br/>Flight Coroutine Engine"]
            P_SIO["<b>SIO Inter-Core FIFO & Ring</b><br/>Cross-core doorbell IRQ"]
            P_CORE0["<b>Core 0: I/O & Network Engine</b><br/>• PIO SPI DMA (ICM-42688-P)<br/>• UART0 (U-Blox GPS)<br/>• CYW43439 Wi-Fi"]
            P_CORE1 <-->|Lock-free SPSC| P_SIO <--> P_CORE0
        end

        subgraph ESP_PLAT["3. ESP32-P4 Dual-Core"]
            direction TB
            E_CORE1["<b>Core 1: Worker Core</b><br/>Flight Coroutine Engine"]
            E_IPC["<b>Dual-Core IPC Mailbox</b><br/>Hardware Cross-Core IRQ"]
            E_CORE0["<b>Core 0: I/O & Network Engine</b><br/>• GDMA SPI (ICM-42688-P)<br/>• UART1 (U-Blox GPS)<br/>• Wi-Fi 6 / Ethernet"]
            E_CORE1 <-->|Lock-free GDMA Ring| E_IPC <--> E_CORE0
        end

        subgraph SITL_PLAT["4. Desktop Simulation"]
            direction TB
            S_HOST["<b>Host Workstation / CI</b><br/>Unit tests & 6-DOF SITL"]
            S_MOCK["<b>In-Memory SPSC Queues</b><br/>Background DMA worker thread"]
            S_SIM["<b>Mock Hardware Synthesizer</b><br/>Simulated 8 kHz IMU & GPS stream"]
            S_HOST <--> S_MOCK <--> S_SIM
        end
    end

    ROUTER --> LINUX_PLAT
    ROUTER --> PICO_PLAT
    ROUTER --> ESP_PLAT
    ROUTER --> SITL_PLAT

    classDef appBox fill:#1e3a8a,stroke:#3b82f6,stroke-width:2px,color:#ffffff;
    classDef routerBox fill:#4c1d95,stroke:#8b5cf6,stroke-width:2px,color:#ffffff;
    classDef targetBox fill:#0f172a,stroke:#475569,stroke-width:1px,color:#e2e8f0;

    class APP appBox;
    class ROUTER routerBox;
    class LINUX_PLAT,PICO_PLAT,ESP_PLAT,SITL_PLAT targetBox;
```

---

## 3. Strict Development Rules for AI Agents & Developers

1. **Markdown First**:
   - Every new driver, peripheral contract, or transport queue must be documented in `docs/` before code modification.
2. **Minimal In-Code Comments**:
   - Avoid duplicate verbose comments in `.cpp` / `.hpp` files.
   - Use cross-reference markers: `// Spec: docs/<file>.md#<anchor>`.
3. **Zero Dynamic Allocation**:
   - All coroutine frames, TLP packets, and driver request/completion rings must allocate from static atomic pools (`CoroutineStaticPool`, `etl::queue_spsc_isr`).
4. **Split-Transaction Queue Contract**:
   - Every peripheral driver (UART, SPI, Timer, Mailbox) must expose an Input (Request) Queue and an Output (Completion) Queue with non-blocking C++20 awaiters.
5. **Universal Application Code**:
   - The top-level flight application (`inav-abstractx`) must remain 100% agnostic to whether the underlying execution domain is Linux userspace, Pico 2 W Core 1, or ESP32-P4 Core 1.
