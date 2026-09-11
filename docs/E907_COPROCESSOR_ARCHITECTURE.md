# Allwinner XuanTie E907 Co-Processor Architecture

This document describes the heterogeneous co-processor execution architecture for the **Allwinner XuanTie E907 RISC-V 32-bit core** (Allwinner T527 / A733 SoCs), integrating with Linux ARM64 host flight stacks via `remoteproc`, shared SRAM, and hardware MSGBox.

---

## 1. Dual-Domain Execution Model

The XuanTie E907 runtime is cleanly separated into two non-blocking domains:
1. **I/O Interrupt Domain (PLIC ISR Context)**: High-priority hardware event handling and DMA queue management.
2. **Coroutine Work Domain (Main Thread Context)**: Cooperative execution of sensor fusion, packet formatting, and Linux RPC handlers.

```mermaid
graph TD
    subgraph Linux_ARM64["Linux Host (ARM64 Core / inav-abstractx)"]
        LinuxFlight["Linux Flight App"]
        LinuxSram["Shared SRAM A3/C (0x40000000)<br/>Lock-Free SPSC 64B TLP Ring"]
        LinuxMsgBox["sun6i-msgbox Driver"]
    end

    subgraph E907_PLIC["E907 I/O Interrupt Domain (PLIC ISR)"]
        MsgBoxISR["MSGBox Doorbell ISR"]
        SpiDmaISR["SPI DMA Completion ISR (ICM-42688-P)"]
        UartDmaISR["UART RX FIFO ISR (U-Blox GPS)"]
        TimerISR["Hardware Timer / SysTick ISR"]
    end

    subgraph E907_Queue["Domain Bridge Work Queue"]
        WorkQueue["IsrSafeCoroutineQueue<32><br/>(etl::queue_spsc_isr)"]
    end

    subgraph E907_Coro["E907 Coroutine Domain (Main Loop)"]
        MsgBoxTask["Linux RPC Task<br/>co_await msgbox.recv_async()"]
        ImuTask["8 kHz IMU Pipeline<br/>co_await imu.next_sample_async()"]
        GpsTask["GPS Navigation Parser<br/>co_await gps.next_fix_async()"]
        TimerTask["ETL Software Timer Multiplexer<br/>co_await timer.sleep_ms_async()"]
    end

    %% Linux to E907
    LinuxMsgBox -->|Doorbell IRQ| MsgBoxISR
    MsgBoxISR -->|push_from_isr()| WorkQueue

    %% Peripherals to ISR
    SpiDmaISR -->|push_from_isr()| WorkQueue
    UartDmaISR -->|push_from_isr()| WorkQueue
    TimerISR -->|push_from_isr()| WorkQueue

    %% Dispatcher
    WorkQueue -->|Dispatcher::process()| MsgBoxTask
    WorkQueue -->|Dispatcher::process()| ImuTask
    WorkQueue -->|Dispatcher::process()| GpsTask
    WorkQueue -->|Dispatcher::process()| TimerTask

    %% Outbound Data
    ImuTask -->|Write 64B TLP| LinuxSram
    GpsTask -->|Write 64B TLP| LinuxSram
    ImuTask -->|Doorbell Notification| LinuxMsgBox

    classDef host fill:#0f172a,stroke:#3b82f6,stroke-width:2px,color:#ffffff;
    classDef isr fill:#450a0a,stroke:#ef4444,stroke-width:2px,color:#ffffff;
    classDef wq fill:#14532d,stroke:#22c55e,stroke-width:2px,color:#ffffff;
    classDef coro fill:#1e1b4b,stroke:#818cf8,stroke-width:2px,color:#ffffff;

    class LinuxFlight,LinuxSram,LinuxMsgBox host;
    class MsgBoxISR,SpiDmaISR,UartDmaISR,TimerISR isr;
    class WorkQueue wq;
    class MsgBoxTask,ImuTask,GpsTask,TimerTask coro;
```

---

## 2. Interaction Contract: ISR vs Coroutines

| Phase | Executing Entity | Action | Constraints |
| :--- | :--- | :--- | :--- |
| **1. Hardware Event** | Peripheral (SPI, UART, MSGBox, Timer) | Triggers PLIC interrupt vector on E907. | Hardware masked. |
| **2. ISR Handler** | PLIC ISR Function | Acknowledges HW IRQ bit, calls `push_completion_from_isr()`, and posts coroutine handle (`IsrDispatcher::post(handle)`). | **< 1.5 µs execution**, 0 heap allocation, no parsing. |
| **3. Dispatcher Drain** | Main Event Loop | `Dispatcher::process()` pulls coroutines from `IsrSafeCoroutineQueue` and calls `handle.resume()`. | Interrupts enabled. |
| **4. Coroutine Execution** | Coroutine Frame | Extracts data, parses UBX frames, converts raw IMU LSBs to $g$/$\text{dps}$, packages into 64B TLPs. | Cooperative non-preemptive. |
| **5. Idle State** | Main Event Loop | Executes RISC-V `wfi` (Wait For Interrupt) when work queue is empty. | 0% idle CPU power. |

---

## 3. ETL Timer Multiplexing

To avoid dedicating scarce hardware timer channels to individual tasks, the E907 uses `etl::callback_timer_interrupt<InterruptLock, 16>`:
- The hardware timer generates a **1 kHz tick**.
- Multiple coroutines register non-blocking sleep awaiters (`co_await timer.sleep_ms_async(ms)`).
- When the deadline expires, the ETL callback posts the specific coroutine handle to `IsrDispatcher`.

---

## 4. Shared Memory Mapping (`memory_map.h`)

| Memory Region | Physical Address | Size | Function |
| :--- | :--- | :--- | :--- |
| **SRAM A1 (E907 Code)** | `0x00020000` | 64 KB | E907 boot code (`startup.S`), vector table, and `.text`. |
| **SRAM A3 / C (TLP Ring)** | `0x40000000` | 64 KB | Lock-free SPSC TLP telemetry ring (`asp_tlp64_t`). |
| **DRAM Reserved (Trace)**| `0x4E000000` | 1 MB | Linux RemoteProc Resource Table & Trace Ring (`trace0`). |
| **MSGBox MMIO** | `0x03003000` | 4 KB | Hardware cross-core doorbells (Channels 0–3). |
