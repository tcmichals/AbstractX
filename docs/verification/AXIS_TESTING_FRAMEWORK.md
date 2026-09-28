# AbstractX AXIS IP Testing Framework

This document is the **canonical reference** for how to write, structure, and execute tests for any AXI-Stream IP module within the AbstractX project. It covers both **isolated unit tests** (single-module verification) and **End-to-End (E2E) integration tests** (full pipeline validation through the VIP).

---

## Testing Philosophy

AbstractX follows a **"Tested, Not Guessed At"** methodology. Every module must survive a hostile simulation environment before it can be integrated. The project provides two reusable Python libraries that enforce this:

| Library | File | Purpose |
|---|---|---|
| `ASP_AXIS_Driver` / `ASP_AXIS_Monitor` | `sim/cocotb/asp_axis_tester.py` | Isolated AXIS unit testing with randomized backpressure torture |
| `AbstractX_VIP` | `sim/cocotb/asp_e2e_vip.py` | Full E2E integration testing through physical SPI pin emulation |

---

## Part 1: Isolated AXIS Unit Testing

### When to Use
Every new `.sv` module that exposes AXI-Stream ports (`tdata`, `tvalid`, `tready`, `tlast`) **MUST** have an isolated unit test using the `ASP_AXIS_Driver` and `ASP_AXIS_Monitor` classes.

### The Torture Test Classes

#### `ASP_AXIS_Driver`
Injects byte-stream frames into a module's **ingress** AXIS port. Automatically introduces randomized inter-byte pipeline stalls (~30% probability) to prove the DUT correctly honors `tvalid` handshaking.

```python
from asp_axis_tester import ASP_AXIS_Driver

driver = ASP_AXIS_Driver(
    clk   = dut.clk,
    tdata = dut.s_axis_tdata,
    tvalid= dut.s_axis_tvalid,
    tready= dut.s_axis_tready,
    tlast = dut.s_axis_tlast   # Optional, pass None if module has no tlast
)

# Send a frame with random source-side stalls
await driver.send_frame([0xDE, 0xAD, 0xBE, 0xEF], random_delays=True)
```

#### `ASP_AXIS_Monitor`
Attaches to the module's **egress** AXIS port. Applies hostile random backpressure (~25% of cycles, `tready` is deasserted) to prove the DUT never drops, corrupts, or duplicates data.

```python
from asp_axis_tester import ASP_AXIS_Monitor

monitor = ASP_AXIS_Monitor(
    clk   = dut.clk,
    tdata = dut.m_axis_tdata,
    tvalid= dut.m_axis_tvalid,
    tready= dut.m_axis_tready,
    tlast = dut.m_axis_tlast   # Optional
)

# Let simulation run...
for _ in range(200):
    await RisingEdge(dut.clk)

monitor.stop()

# Validate received frames
assert len(monitor.received_frames) == expected_count
assert monitor.received_frames[0] == b"\xDE\xAD\xBE\xEF"
```

### Step-by-Step: Adding a New AXIS Module Test

1. **Create the SystemVerilog module** in `rtl/` (e.g., `rtl/asp_my_filter.sv`)
2. **Create the testbench** in `sim/cocotb/test_asp_my_filter_cocotb.py`:

```python
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge
from asp_axis_tester import ASP_AXIS_Driver, ASP_AXIS_Monitor

async def reset(dut):
    dut.rst_n.value = 0
    for _ in range(5):
        await RisingEdge(dut.clk)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)

@cocotb.test()
async def test_my_filter_torture(dut):
    """AbstractX Standard: Hostile environment validation"""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    driver = ASP_AXIS_Driver(dut.clk, dut.s_axis_tdata, dut.s_axis_tvalid,
                              dut.s_axis_tready, dut.s_axis_tlast)
    monitor = ASP_AXIS_Monitor(dut.clk, dut.m_axis_tdata, dut.m_axis_tvalid,
                                dut.m_axis_tready, dut.m_axis_tlast)

    test_frames = [
        b"\x01\x02\x03",
        b"\xFF\xFE\xFD\xFC\xFB",
        bytes(range(64)),   # Larger burst
    ]

    for frame in test_frames:
        await driver.send_frame(list(frame), random_delays=True)

    # Drain pipeline
    for _ in range(300):
        await RisingEdge(dut.clk)

    monitor.stop()

    # Verify
    assert len(monitor.received_frames) == len(test_frames), "Frames were dropped!"
    for i, expected in enumerate(test_frames):
        assert monitor.received_frames[i] == expected, f"Frame {i} corrupted!"
```

3. **Register in the Makefile** (`sim/cocotb/Makefile`):

```makefile
else ifeq ($(TOPLEVEL), asp_my_filter)
    VERILOG_SOURCES = $(RTL_DIR)/asp_my_filter.sv
```

4. **Register in CMake** (`sim/CMakeLists.txt`):

```cmake
add_cocotb_test(sim-test-my-filter asp_my_filter test_asp_my_filter_cocotb)
```

5. **Add to `sim-test-all` DEPENDS list** in `sim/CMakeLists.txt`.

6. **Run**: `make sim-test-my-filter` to test isolation, `make test-all` for full regression.

---

## Part 2: End-to-End (E2E) Integration Testing

### When to Use
Once your isolated module passes the AXIS torture test, it must be wired into `asp_top.sv` (or your project's top-level integrator) and tested through the **AbstractX Verification IP (VIP)**. This proves the module functions correctly when actual host traffic is tunneled through the complete SPI → RegBank → Router → Wishbone pipeline.

### The `AbstractX_VIP` Class

The VIP emulates a Linux SPI host driver. It bit-bangs real `MOSI`, `SCLK`, and `CS_N` signals exactly as the `asp_tun_driver.py` would on physical hardware.

```python
from asp_e2e_vip import AbstractX_VIP

vip = AbstractX_VIP(dut, clk_domain_freq_mhz=100, spi_freq_mhz=5)

# Reset the full FPGA pipeline
await vip.reset_hardware()

# INGRESS: Tunnel any payload into the routing fabric
await vip.write_stream([0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04])

# STATUS: Poll until the FIFO reports data waiting
ver, status, rx_len = await vip.read_status()

# EGRESS: Pull the response payload
response = await vip.read_stream(rx_len)
```

### Tunneling Custom Data

The VIP is **payload-agnostic**. The bytes you pass to `write_stream()` are your tunneling data — the VIP only wraps them in the SPI `CMD_WRITE_DATA` framing. This means developers can tunnel **any** custom application protocol through the standard AbstractX hardware pipeline:

```python
# Example: Custom sensor telemetry frame
sensor_payload = struct.pack(">BHI", 0x42, 1024, 0xDEADBEEF)
await vip.write_stream(sensor_payload)

# Example: Custom register write command
reg_write = struct.pack(">BBBBBBBB", OP_WRITE_BLOCK, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x04)
await vip.write_stream(reg_write)
```

### Step-by-Step: Adding a New E2E Test

1. **Wire your module** into `asp_top.sv` (or your project top-level)
2. **Create the E2E test** in `sim/cocotb/test_asp_top_cocotb.py` (or a new file):

```python
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge
import struct
from asp_e2e_vip import AbstractX_VIP

@cocotb.test()
async def test_my_custom_e2e(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    vip = AbstractX_VIP(dut)
    await vip.reset_hardware()

    # Tunnel your custom payload
    my_payload = struct.pack(">BBBB", 0xCA, 0xFE, 0xBA, 0xBE)
    await vip.write_stream(my_payload)

    # Wait for processing
    for _ in range(200):
        await RisingEdge(dut.clk)

    # Poll and verify
    _, _, rx_len = await vip.read_status()
    assert rx_len > 0, "No response from custom hardware!"

    response = await vip.read_stream(rx_len)
    # Validate your custom response format
    assert response[0] == 0x00  # Your expected status byte
```

3. **Ensure `asp_top` source list** in `sim/cocotb/Makefile` includes your new module.

---

## Part 3: Verification Checklist

Before marking any AXIS IP module as "verified", the developer must confirm:

| # | Requirement | Tool |
|---|---|---|
| 1 | Module passes with **random source stalls** (30% `tvalid` drops) | `ASP_AXIS_Driver` |
| 2 | Module passes with **hostile backpressure** (25% `tready` drops) | `ASP_AXIS_Monitor` |
| 3 | **Zero data corruption** across all test frames | Assert in testbench |
| 4 | **Zero data drops** — all transmitted frames are received | Assert in testbench |
| 5 | `tlast` framing is **cycle-accurate** (no early/late assertion) | `ASP_AXIS_Monitor` |
| 6 | Module compiles **warning-free** under Verilator strict mode | `make sim-test-<name>` |
| 7 | Module registered in **CMake** and **Makefile** source maps | `sim/CMakeLists.txt`, `sim/cocotb/Makefile` |
| 8 | If integrated into top-level, **E2E test passes** via `AbstractX_VIP` | `make sim-test-top` |

---

## Part 4: Build System Integration

### Directory Layout
```
sim/
  cocotb/
    asp_axis_tester.py       # AXIS Driver + Monitor (unit tests)
    asp_e2e_vip.py           # AbstractX VIP (E2E tests)
    test_asp_<module>_cocotb.py   # Per-module isolated testbench
    test_asp_top_cocotb.py   # E2E integration testbench
    Makefile                 # Verilator/cocotb runner with TOPLEVEL dispatch
  CMakeLists.txt             # CMake target registration
```

### Available Make Targets
| Target | Description |
|---|---|
| `make sim-test-reg-bank` | Isolated: SPI Register Bank |
| `make sim-test-spi-frontend` | Isolated: SPI MAC Layer |
| `make sim-test-axis-fifo` | Isolated: Generic AXIS FIFO |
| `make sim-test-top` | E2E: Full pipeline through VIP |
| `make test-all` | Complete regression suite |
| `make info` | Self-documenting target list |

### Adding a New Target
In `sim/cocotb/Makefile`, add your source binding:
```makefile
else ifeq ($(TOPLEVEL), asp_my_module)
    VERILOG_SOURCES = $(RTL_DIR)/asp_my_module.sv
```

In `sim/CMakeLists.txt`, register the target:
```cmake
add_cocotb_test(sim-test-my-module asp_my_module test_asp_my_module_cocotb)
```

And append to **both** the `sim-test-all` DEPENDS list and the `docs/BUILD.md` manifest.

---

## Related Documentation
- `docs/DESIGN_RULES.md` — Engineering invariants (Section 4: AXIS Test Methodology)
- `docs/E2E_VERIFICATION.md` — VIP architecture and tunneling philosophy
- `docs/BUILD.md` — Build system walkthrough and target manifest
