# AbstractX Applications Directory

Welcome to the `apps/` directory of AbstractX. This directory contains deployable end-to-end applications designed around the **Universal Asynchronous Hardware Offloader & Heterogeneous Interconnect Framework**.

---

## 1. Application Architecture Philosophy

In AbstractX, **applications are organized by application capability, NOT by silicon target.**

Every application is strictly decoupled from physical microcontroller or processor architectures:
1. **Single Portable Entrypoint (`src/main.cpp`)**:
   - The application defines its sensor/actuator pipeline in pure **C++20 Stackless Coroutines** (`Task<T>`, `co_await`, `when_all`, `when_any`).
   - Guarantees **0 dynamic heap memory allocations (`0 B`)** via static atomic frame pools.
   - Operates solely on high-level engineering units ($g$, $\text{dps}$, coordinates) or 64-byte TLP messages.
   - Configures the I/O channel descriptors (`AutoChannelConfig`) and SPSC rings in an `abstractx::Config` structure.
2. **Unified Master Runtime Lifecycle (`abstractx::init()` and `abstractx::run()`)**:
   - `abstractx::init(config)` initializes the target platform clocks, peripheral HAL drivers, and autonomous co-processor domain placement (Core 0 vs Core 1 on RP2350, E907 vs Linux on Allwinner SoCs, or SITL loop).
   - `abstractx::run(app_main())` executes application coroutines, I/O reactors, and background CTF trace dispatchers cooperatively.
   - **Zero `#ifdef` Directives**: The identical `src/main.cpp` compiles and executes across Linux SITL, Raspberry Pi Pico 2 W, and XuanTie E907 without code modifications.

```mermaid
graph TD
    subgraph APP["Unified Application: apps/gps_imu_app/src/main.cpp"]
        CONFIG["<b>Config Setup</b><br/>abstractx::init(config)"]
        CORO["<b>Coroutine Tasks</b><br/>imu_test_task, gps_test_task"]
        RUN["<b>Master Runner</b><br/>abstractx::run(app_main())"]
        CONFIG --> RUN
        RUN --> CORO
    end

    subgraph RUNTIME["AbstractX Master Runtime (include/abstractx/abstractx.hpp)"]
        AUTOPLACE["<b>Autonomous Silicon Placement Engine</b>"]
        DISP["<b>Cooperative DomainDispatcher</b>"]
        TRACE["<b>Trace Dispatcher (1 KB Ping-Pong Sinks)</b>"]
    end

    subgraph TARGETS["Target BSPs & HAL Implementations (targets/)"]
        T_PICO["targets/pico2w_rp2350 (Core 0 I/O, Core 1 Coro)"]
        T_E907["targets/allwinner_e907 (Shared SRAM A3/C + MsgBox)"]
        T_LINUX["targets/linux (POSIX Workers + epoll Reactor)"]
    end

    APP --> RUNTIME
    RUNTIME --> T_PICO
    RUNTIME --> T_E907
    RUNTIME --> T_LINUX

    classDef appStyle fill:#1e3a8a,stroke:#3b82f6,stroke-width:2px,color:#ffffff;
    classDef rtStyle fill:#14532d,stroke:#22c55e,stroke-width:2px,color:#ffffff;
    classDef tgtStyle fill:#4c1d95,stroke:#8b5cf6,stroke-width:2px,color:#ffffff;

    class CONFIG,CORO,RUN appStyle;
    class AUTOPLACE,DISP,TRACE rtStyle;
    class T_PICO,T_E907,T_LINUX tgtStyle;
```

---

## 2. Directory Index

| Application | Description | Authoritative Spec | Status |
| :--- | :--- | :--- | :--- |
| **`gps_imu_app/`** | Flagship unified 8 kHz IMU auto-burst & GPS navigation streaming application (runs across Pico 2 W, Linux, and E907). | [`apps/gps_imu_app/SPECIFICATION.md`](gps_imu_app/SPECIFICATION.md) | 🚀 Active |
| **`e907_coprocessor/`** | XuanTie E907 RemoteProc DDR coprocessor firmware. | [`apps/e907_coprocessor/CMakeLists.txt`](e907_coprocessor/CMakeLists.txt) | 🚀 Active |
| **`esp32p4_hub/`** | Legacy ESP32-P4 sensor hub app (migrated to `gps_imu_app`). | [`apps/esp32p4_hub/CMakeLists.txt`](esp32p4_hub/CMakeLists.txt) | Legacy |
