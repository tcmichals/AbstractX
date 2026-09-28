# [Component Name] Specification & Architectural Contract

**Document:** `docs/tier3_targets/[target_name]/SPECIFICATION.md` (or `apps/[app_name]/SPECIFICATION.md`)  
**Status:** Authoritative Foundation (SSOT)  
**Implementing Target:** `[path/to/source]`

---

## 1. Overview & Architectural Placement

* **Domain**: `[e.g., Sensor Driver / Hardware Offloader / Control Loop / Application]`
* **Execution Core**: `[Core 0 (I/O) / Core 1 (Coroutine Flight Loop)]`
* **Allocation Policy**: Strictly `0 B` dynamic heap allocation. Statically pooled or ring-buffered.

---

## 2. Memory Map & Hardware Topology

Define virtual PCIe BAR memory regions, peripheral registers, or shared SRAM offsets:

| Address Offset | Register / Field Name | Type | Reset Value | Description |
| :--- | :--- | :--- | :--- | :--- |
| `0x00` | `REG_CTRL_STATUS` | R/W | `0x00000000` | Master control and status flags. |
| `0x04` | `REG_DATA_BURST`  | RO  | `0x00000000` | Streamed data payload register. |

---

## 3. Multi-Rate Timing & Channel Ingestion

* **Primary Pacing Clock**: `[e.g., IMU DRDY @ 8 kHz / Timer Tick @ 100 Hz]`
* **Ingress Interface**: `co_await g_primary_channel.pop()`
* **Auxiliary Drain Interfaces**: Non-blocking `while (g_aux_channel.try_pop(sample))`
* **Egress Interface**: Lock-free SPSC TLP ring (`SpscTlpRing<64>`)

---

## 4. Architectural Invariants

* **`[SPEC-XXX-01]` Zero Dynamic Heap**:
  No `malloc`, `free`, `new`, or `delete`. All task frames, queues, and descriptors are statically allocated in `.bss`/`.data`.
* **`[SPEC-XXX-02]` Non-Blocking Awaitable Driver**:
  Driver methods return `coro::Task<bool>`. No blocking spinloops or synchronous bus operations.
* **`[SPEC-XXX-03]` Symmetrical Wire Framing**:
  Emits fixed 64-byte `Tlp64` packets with nanosecond timestamps governed by dynamic CTF 1.8 schema.

---

## 5. Traceability & CppUTest Verification Gates

| Specification Tag | Implementing Source File | Verification Test Suite | Pass Criteria |
| :--- | :--- | :--- | :--- |
| `[SPEC-XXX-01]` | `src/driver.cpp` | `tests/test_driver.cpp` | Compiles with `-fno-exceptions -fno-rtti`, 0 B heap. |
| `[SPEC-XXX-02]` | `src/driver.cpp` | `tests/test_driver.cpp` | Driver yields execution cooperatively; 0 bus spinloops. |
| `[SPEC-XXX-03]` | `src/driver.cpp` | `tests/test_driver.cpp` | Emitted 64B frames pass CRC32 and CTF schema validation. |
