# AbstractX TLP Over VirtIO RPMsg Routing Specification & Guide

**Document:** `docs/tier2_contracts/TLP_OVER_RPMSG_ROUTING.md`  
**Governing Requirements:** `[SPEC-ARCH-08]`, `[SPEC-TLP-01]`, `[SPEC-TLP-03]`, `[SPEC-E906-IPC-04]`  
**Status:** Authoritative Foundation (Tier 2 SSOT)

---

## 1. Architectural Overview & Scope

In AbstractX multi-processor architectures (such as the Allwinner Radxa Cubie A5E with quad Cortex-A53 Linux and XuanTie E906 RISC-V coprocessor), real-time sensor streams and control transactions are routed between processors using **64-Byte Transaction Layer Packets (`Tlp64`)** encapsulated inside **VirtIO RPMsg frames**.

This specification defines:
1. **Memory Geometry**: Allocation of vrings and buffer pools in shared on-chip SRAM.
2. **Packet Framing & Batching**: How 64-byte `Tlp64` packets pack into 512-byte RPMsg buffers without head-of-line blocking.
3. **Zero-Copy Ingress & Egress**: Direct buffer borrow (`emplace()`) and commit lifecycles.
4. **Host Dynamic Endpoint Discovery**: How Linux kernel `/dev/rpmsg_ctrl0` creates `/dev/rpmsgN` character devices and learns addresses via hello handshakes.
5. **UDP Gateway Fan-Out**: Bridging RPMsg streams to AbstractX Studio and ground-control software over loopback UDP.

---

## 2. End-to-End System Topology

```mermaid
sequenceDiagram
    autonumber
    participant HW as Hardware Sensors (IMU/GPS)
    participant E906 as XuanTie E906 (Core 1)
    participant SRAM as SRAM Space 1 (0x40020000)
    participant MBOX as sun6i-msgbox (Mailbox)
    participant KERNEL as Linux Kernel (Remoteproc)
    participant BRIDGE as e906_rpmsg_bridge.py
    participant STUDIO as AbstractX Studio (GUI)

    Note over E906,SRAM: Boot & Resource Table Initialization
    E906->>SRAM: Publish VirtIO Resource Table (FW_RSC_ADDR_ANY)
    KERNEL->>SRAM: Carve out 128 KB DMA pool & allocate 2x16 vring buffers
    
    Note over BRIDGE,KERNEL: Endpoint Setup & Address Handshake
    BRIDGE->>KERNEL: ioctl(RPMSG_CREATE_EPT_IOCTL, "abstractx-tlp", addr=1024)
    KERNEL-->>BRIDGE: Creates /dev/rpmsgN
    BRIDGE->>E906: Send 'helo' packet (Endpoint Address Handshake)
    E906->>E906: Learn Linux host source address (host_addr = msg.src)

    Note over HW,STUDIO: Real-Time Zero-Copy TLP Streaming
    HW->>E906: Sensor Data Ready (ISR / DMA)
    E906->>SRAM: emplace() acquires slot directly in VirtIO TX Buffer
    E906->>SRAM: Pack 64B Tlp64 into slot (Batch up to 7 TLPs = 448B)
    E906->>MBOX: Kick Mailbox Channel 1 (Interrupt to Linux)
    MBOX->>KERNEL: Mailbox IRQ fires; updates Used Ring
    KERNEL->>BRIDGE: read(/dev/rpmsgN, 512) returns 448B payload
    BRIDGE->>BRIDGE: Validate TLP batch (len % 64 == 0)
    BRIDGE->>STUDIO: sendto(UDP 127.0.0.1:9871, 7 TLPs)
    STUDIO->>STUDIO: Real-time PFD, attitude wireframe, and scope update
```

---

## 3. Shared SRAM Memory Mapping & Buffer Budget

All RPMsg communications between the Linux host and XuanTie E906 occur inside **on-chip SRAM Space 1** (`0x40020000`–`0x4003FFFF`, 128 KB), eliminating DRAM bus contention, MMU translation overhead, and cache maintenance operations (`[SPEC-E906-CACHE-02]`):

<!-- ascii-ok: table: SRAM Space 1 buffer budget (component and size) -->
```text
+-----------------------------------------------------------------------------------+
|                        Allwinner SRAM Space 1 (128 KB Pool)                       |
|   Core Address: 0x40020000 - 0x4003FFFF  |  Host Address: 0x072E0000 - 0x072FFFFF |
+------------------------------------+----------------------------------------------+
| Component                          | Size & Layout                                |
+------------------------------------+----------------------------------------------+
| VirtIO RX Vring (Host -> E906)     | 16 Descriptors, Avail Ring, Used Ring (2 KB) |
| VirtIO TX Vring (E906 -> Host)     | 16 Descriptors, Avail Ring, Used Ring (2 KB) |
| Buffer Pool (16 x 512B TX buffers) | 8 KB (Host allocated, DMA-coherent)         |
| Buffer Pool (16 x 512B RX buffers) | 8 KB (Host allocated, DMA-coherent)         |
| Free Reserved Headroom             | 108 KB (Available for future pools)          |
+------------------------------------+----------------------------------------------+
```

### RPMsg Frame Geometry & The 7-TLP Batching Rule
A standard VirtIO RPMsg buffer is 512 bytes:
* **RPMsg Header**: 16 bytes (`src: u32`, `dst: u32`, `reserved: u32`, `len: u16`, `flags: u16`).
* **Available Payload**: $512 - 16 = 496 \text{ bytes}$.
* **TLP Frame Size**: Exactly 64 bytes (`asp_tlp64_t`).
* **Maximum TLPs Per Message**:
  $$\lfloor 496 / 64 \rfloor = 7 \text{ TLPs} \quad (7 \times 64 = 448 \text{ bytes payload, } 48 \text{ bytes padding})$$

<!-- ascii-ok: packet byte layout of the 512-byte RPMsg buffer -->
```
+-----------------------------------------------------------------------------------+
|                         512-Byte VirtIO RPMsg Buffer                              |
+------------------+-----------------------------------------------+----------------+
| 16-Byte Header   | 448-Byte Payload: 7 x 64-Byte TLP Packets     | 48-Byte Unused |
| src, dst, len... | [TLP 0] [TLP 1] [TLP 2] [TLP 3] [TLP 4]...    | Padding        |
+------------------+-----------------------------------------------+----------------+
```

---

## 4. Multi-Endpoint Allocation Over A Single Message Box

AbstractX routes three distinct communications planes over a single physical mailbox channel (`sun6i-msgbox`, channel 9) by binding three independent RPMsg endpoints:

| Endpoint Name | Destination Port | Wire Format | Architectural Role | Linux Device |
| :--- | :--- | :--- | :--- | :--- |
| `abstractx-tlp` | `1024` | **Batched 64-byte `Tlp64` binary packets** | Real-time sensor fusion & actuator commands | `/dev/rpmsgN` |
| `abstractx-trace` | `1025` | **CTF 1.8 binary trace event frames** | Execution profiling & nanosecond timelines | `/dev/rpmsgM` |
| `abstractx-repl` | `1026` | **UTF-8 interactive line text stream** | MicroPython interactive shell & scripting | `/dev/rpmsg0` |

### Head-of-Line Blocking Protection (`kTlpReserve`)
Both `abstractx-tlp` and `abstractx-trace` share the 16 host transmit buffers. To prevent diagnostic tracing from starving flight telemetry:
* The trace transport may borrow a buffer **only if at least `kTlpReserve = 4` free buffers remain** in the pool.
* The TLP transport has high priority and may borrow any free buffer.
* Buffer tokens allow out-of-order commits: a half-filled trace buffer never stalls high-rate 100 Hz TLP flushes.

---

## 5. C++ E906 Firmware Driver Contract (`RpmsgTlpChannel`)

The E906 firmware interface (`targets/allwinner_e906/include/hal/rpmsg_endpoints.hpp`) provides a zero-copy, non-blocking API:

```cpp
// 1. In-place slot acquisition (Zero-Copy):
// Directly borrows memory inside the current 512-byte RPMsg buffer.
asp_tlp64_unaligned_t* slot = tlp_channel.emplace();
if (slot) {
    slot->type = static_cast<uint8_t>(TlpType::DmaStream);
    slot->channel = static_cast<uint8_t>(Channel::Telemetry);
    slot->timestamp_ns = hal_get_timestamp_ns();
    // Pack 40-byte sensor payload directly into slot...
}

// 2. Value-based submission (Single 64-byte copy from pre-built struct):
abstractx::Tlp64 tlp = StateEstimator::to_tlp(state);
tlp_channel.send(tlp);

// 3. Opportunistic batch flush:
// Called once per cooperative event loop step (abstractx::step()) to kick
// partially-filled batches over the hardware message box.
tlp_channel.flush();
```

---

## 6. Linux Host Bridge (`e906_rpmsg_bridge.py`)

On the Linux Cortex-A53 host, user-space tools communicate with the E906 via `tools/e906_rpmsg_bridge.py`:

```bash
# Terminal 1: Launch high-rate TLP packet bridge
python3 tools/e906_rpmsg_bridge.py tlp --dev /dev/rpmsg1

# Terminal 2: Launch nanosecond barectf trace bridge
python3 tools/e906_rpmsg_bridge.py trace --dev /dev/rpmsg2
```

### Network Fan-Out & Port Mapping:
* **E906 -> Linux Host (Port 9871)**: Bridges 64-byte TLP packets to AbstractX Studio, logging daemons, and flight instruments.
* **Linux Host -> E906 (Port 9872)**: Receives command TLPs (mode changes, arming, parameter updates) and injects them back into E906 ingress queues.
* **E906 -> Studio Tracing (Port 9870)**: Bridges CTF 1.8 packets to the Studio timeline visualizer.

---

## 7. MicroPython & TLP Integration: Zero-Heap Binding Architecture

### 7.1 Efficiency Analysis: ASCII REPL Strings vs. Binary TLPs
In embedded flight control on the XuanTie E906 RISC-V co-processor, emitting telemetry as ASCII strings over REPL stdout (`print("[TLM ...]")`) is an educational prototype pattern, not an aerospace flight standard:
* **Garbage Collection Pressure**: MicroPython strings (`str(...)`, string concatenation `+`, and formatting) allocate temporary objects on the GC heap. In high-rate flight loops (10 Hz – 100 Hz), rapid string churn triggers unpredictable GC pauses that introduce jitter into control laws.
* **31-Bit Integer Truncation**: Minimal MicroPython (`MICROPY_LONGINT_IMPL_NONE`) limits positive integers to $2^{30}-1$ ($1,073,741,823$). Representing a 32-bit longitude in $10^{-7}$ degrees (which spans up to $1,800,000,000$) overflows in pure Python without string decomposition.
* **Wire Bloat & CPU Overhead**: An ASCII telemetry line requires 150–250 bytes of text and $> 500 \ \mu\text{s}$ of string formatting time. A binary `Tlp64` packet requires **exactly 64 bytes** and is packed in **$< 5 \ \mu\text{s}$**.

### 7.2 Zero-Heap TLP Python Bindings (`[SPEC-E906-REPL-10]`)
To achieve production-grade performance, MicroPython binds directly to the AbstractX C++ TLP infrastructure using the Python Buffer Protocol:

```python
import abstractx

# 1. Zero-Allocation Navigation Telemetry:
# Native C binding emplaces directly into SRAM Space 1 RPMsg buffer slot (0 bytes GC heap)
abstractx.tlp_send_nav(
    lat=418781234,      # 41.8781234 deg N (1e-7 deg)
    lon=-876297982,     # 87.6297982 deg W (1e-7 deg)
    alt_mm=182400,      # 182.4 m MSL
    speed_mms=1420,     # 1.42 m/s ground speed
    heading_cdeg=35900, # 359.00 deg heading
    sats=12,            # 12 satellites tracked
    fix=3               # 3D Fix
)

# 2. Generic TLP Ingress (Host -> E906 Commands):
# Pop packet directly into pre-allocated 64-byte mutable buffer without creating objects
cmd_buf = bytearray(64)
if abstractx.tlp_poll_into(cmd_buf):
    # Process 64-byte command packet in place
    pass
```

### 7.3 Performance & Resource Comparison

| Metric | ASCII REPL Text (`print`) | Binary TLP Ingestion (`Tlp64`) | Improvement |
| :--- | :--- | :--- | :--- |
| **GC Heap Allocation** | ~180 – 350 bytes per epoch | **0 bytes** (Zero allocation) | **$\infty$ (Eliminates GC pauses)** |
| **Wire Bandwidth** | ~180 bytes / epoch | **64 bytes** fixed frame | **64.4% reduction** |
| **Execution Latency** | ~480 – 850 $\mu\text{s}$ | **$< 5 \ \mu\text{s}$** | **$> 100\times$ faster** |
| **Numeric Precision** | String float approximation | **32-bit exact binary integers** | **Deterministic precision** |
| **Transport** | Interactive text (`/dev/rpmsg0`) | Real-time packet bus (`abstractx-tlp`) | **Clean plane isolation** |

---

## 8. Serial TLP Streaming & Tunneling (`Channel::EscSerial`)

### 8.1 Serial Data Encapsulation in 64-Byte TLPs (`[SPEC-E906-REPL-11]`)
In the AbstractX hardware switch fabric and processor runtime, streaming serial interfaces (such as UART GPS, bidirectional ESC telemetry, and MAVLink radio links) do not bypass the bus as unmanaged raw byte streams. Instead, serial traffic is tunneled across **`Channel::EscSerial` (`0x05`)**:

<!-- ascii-ok: packet byte layout of the 64-byte serial TLP -->
```
+-----------------------------------------------------------------------------------------+
|                              64-Byte Serial TLP Packet                                  |
+--------------------------+------------------------------------+-------------------------+
|  24-Byte Header          |  40-Byte Serial Stream Chunk       |  4-Byte Footer          |
|  - type: DmaStream (0x10)|  - 40 Raw Serial Payload Bytes     |  - Hardware CRC32       |
|  - channel: EscSerial(05)|    (UBX binary, ESC telemtry frame,|                         |
|  - length_dw: 10 (40 B)  |     or MAVLink stream packet)      |                         |
|  - sequence: uint16      |                                    |                         |
|  - timestamp_ns: uint64  |                                    |                         |
+--------------------------+------------------------------------+-------------------------+
```

### 8.2 End-to-End Serial TLP Dataflow
```mermaid
flowchart LR
    subgraph Serial Hardware Layer
        UART_DEV["UART3 / ESC Pins"] -->|Hardirq RX FIFO| E906_ISR["C++ UART ISR Engine"]
    end

    subgraph AbstractX C++ Streaming Pipeline
        E906_ISR -->|Pack 40B Chunks| TLP_SERIAL_INGRESS["Tlp64::make_stream_tx(Channel::EscSerial)"]
        TLP_SERIAL_INGRESS -->|Zero-Copy Push| SPSC_SERIAL["AsyncQueue&lt;Tlp64, 16&gt;"]
    end

    subgraph MicroPython Flight Executive
        SPSC_SERIAL -->|abstractx.serial_tlp_poll(buf)| MP_CORE["Python Protocol Engine<br/>(UBX/MAVLink Parser)"]
        MP_CORE -->|abstractx.serial_tlp_send(data)| SPSC_EGRESS["Egress SPSC Ring"]
    end

    subgraph Hardware Serializer
        SPSC_EGRESS -->|DMA Transmission| UART_TX["UART3 TX Pin"]
    end
```

### 8.3 MicroPython Serial TLP Interface
```python
import abstractx

# 1. Zero-Copy Serial Ingress:
# Drains 40-byte serial TLP payload into a pre-allocated bytearray
serial_frame = bytearray(64)
if abstractx.serial_tlp_poll(serial_frame):
    # Extract packet metadata directly from fixed offsets
    seq = serial_frame[10] | (serial_frame[11] << 8)
    # Payload bytes reside at offsets 20..59 (up to 40 bytes)
    payload_chunk = memoryview(serial_frame)[20:60]

# 2. Stream Egress:
# Slices larger arbitrary byte payloads into 40-byte streaming TLPs
ubx_cfg = bytes([0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0x64, 0x00, 0x01, 0x00, 0x01, 0x00, 0x7A, 0x12])
abstractx.serial_tlp_send(ubx_cfg)
```
