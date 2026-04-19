import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge

SYS_VERSION_DEFAULT = 0xA1B2C3D4

async def reset(dut):
    dut.rst.value = 1
    dut.wb_adr_i.value = 0
    dut.wb_dat_i.value = 0
    dut.wb_sel_i.value = 0
    dut.wb_we_i.value = 0
    dut.wb_cyc_i.value = 0
    dut.wb_stb_i.value = 0
    for _ in range(5):
        await RisingEdge(dut.clk)
    dut.rst.value = 0
    await RisingEdge(dut.clk)


async def wb_read(dut, addr):
    """Execute a single Wishbone read cycle and return the 32-bit result."""
    dut.wb_adr_i.value = addr
    dut.wb_we_i.value = 0
    dut.wb_sel_i.value = 0xF
    dut.wb_cyc_i.value = 1
    dut.wb_stb_i.value = 1

    for _ in range(10):
        await RisingEdge(dut.clk)
        if dut.wb_ack_o.value == 1:
            result = int(dut.wb_dat_o.value)
            # Deassert bus at next opportunity (after RisingEdge, not ReadOnly)
            dut.wb_cyc_i.value = 0
            dut.wb_stb_i.value = 0
            await RisingEdge(dut.clk)
            return result

    raise TimeoutError(f"Wishbone read at 0x{addr:08X} never acknowledged!")


async def wb_write(dut, addr, data, sel=0xF):
    """Execute a single Wishbone write cycle."""
    dut.wb_adr_i.value = addr
    dut.wb_dat_i.value = data
    dut.wb_sel_i.value = sel
    dut.wb_we_i.value = 1
    dut.wb_cyc_i.value = 1
    dut.wb_stb_i.value = 1

    for _ in range(10):
        await RisingEdge(dut.clk)
        if dut.wb_ack_o.value == 1:
            dut.wb_cyc_i.value = 0
            dut.wb_stb_i.value = 0
            dut.wb_we_i.value = 0
            await RisingEdge(dut.clk)
            return

    raise TimeoutError(f"Wishbone write at 0x{addr:08X} never acknowledged!")


@cocotb.test()
async def test_read_sys_version(dut):
    """Verify SYS_VERSION register at offset 0x00 returns the parameterized value."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    result = await wb_read(dut, 0x00)
    assert result == SYS_VERSION_DEFAULT, f"Expected 0x{SYS_VERSION_DEFAULT:08X}, got 0x{result:08X}"
    dut._log.info(f"SYS_VERSION = 0x{result:08X} — correct.")


@cocotb.test()
async def test_scratch_register_loopback(dut):
    """Write to scratch register at 0x04 and read it back."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    test_values = [0xDEADBEEF, 0x12345678, 0x00000000, 0xFFFFFFFF]
    for val in test_values:
        await wb_write(dut, 0x04, val)
        result = await wb_read(dut, 0x04)
        assert result == val, f"Scratch loopback failed: wrote 0x{val:08X}, read 0x{result:08X}"
        dut._log.info(f"Scratch loopback 0x{val:08X} — OK")


@cocotb.test()
async def test_byte_select_masking(dut):
    """Verify wb_sel_i byte-lane masking on scratch register."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    await wb_write(dut, 0x04, 0x00000000)
    await wb_write(dut, 0x04, 0xAB000000, sel=0x8)
    result = await wb_read(dut, 0x04)
    assert result == 0xAB000000, f"High-byte sel failed: got 0x{result:08X}"

    await wb_write(dut, 0x04, 0x000000CD, sel=0x1)
    result = await wb_read(dut, 0x04)
    assert result == 0xAB0000CD, f"Low-byte sel failed: got 0x{result:08X}"
    dut._log.info(f"Byte-select masking verified: 0x{result:08X}")


@cocotb.test()
async def test_unknown_address_returns_deadbeef(dut):
    """Read from an unmapped address to confirm DEADBEEF sentinel."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    result = await wb_read(dut, 0x10)
    assert result == 0xDEADBEEF, f"Expected 0xDEADBEEF, got 0x{result:08X}"
    dut._log.info("Unmapped address correctly returns 0xDEADBEEF.")


@cocotb.test()
async def test_version_is_read_only(dut):
    """Writing to the SYS_VERSION address must not change its value."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    await wb_write(dut, 0x00, 0x00000000)
    result = await wb_read(dut, 0x00)
    assert result == SYS_VERSION_DEFAULT, f"SYS_VERSION was modified! Got 0x{result:08X}"
    dut._log.info("SYS_VERSION is confirmed read-only.")
