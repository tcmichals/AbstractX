import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge
import struct

# Opcodes (must match asp_wishbone_master.sv)
OP_PING        = 0x06
OP_READ_BLOCK  = 0x10
OP_WRITE_BLOCK = 0x11
OP_GET_CAPS    = 0x12
OP_HELLO       = 0x13

RES_OK            = 0x00
RES_NOT_SUPPORTED = 0x04

SYS_VERSION = 0xA1B2C3D4


async def reset(dut):
    dut.rst.value = 1
    dut.s_cmd_tvalid.value = 0
    dut.s_cmd_tdata.value = 0
    dut.s_cmd_tlast.value = 0
    dut.s_tid.value = 0
    dut.m_rsp_tready.value = 1
    dut.m_dbg_tready.value = 1
    dut.wb_ack_i.value = 0
    dut.wb_dat_i.value = 0
    for _ in range(5):
        await RisingEdge(dut.clk)
    dut.rst.value = 0
    await RisingEdge(dut.clk)


async def send_cmd_stream(dut, payload_bytes):
    """Drive a command byte stream into the AXIS ingress with tlast on last byte."""
    for i, b in enumerate(payload_bytes):
        dut.s_cmd_tdata.value = b
        dut.s_cmd_tvalid.value = 1
        dut.s_cmd_tlast.value = 1 if i == len(payload_bytes) - 1 else 0
        # Wait for tready handshake
        while True:
            await RisingEdge(dut.clk)
            if dut.s_cmd_tready.value == 1:
                break
    dut.s_cmd_tvalid.value = 0
    dut.s_cmd_tlast.value = 0


async def collect_response(dut, max_cycles=200):
    """Collect response bytes from m_rsp egress until tlast or timeout."""
    response = []
    dut.m_rsp_tready.value = 1
    for _ in range(max_cycles):
        await RisingEdge(dut.clk)
        if dut.m_rsp_tvalid.value == 1 and dut.m_rsp_tready.value == 1:
            response.append(int(dut.m_rsp_tdata.value))
            if dut.m_rsp_tlast.value == 1:
                break
    return response


async def wb_auto_responder(dut, read_data_map, max_cycles=200):
    """
    Simulate a Wishbone slave for the master to talk to.
    read_data_map: dict of addr -> 32-bit value
    """
    for _ in range(max_cycles):
        await RisingEdge(dut.clk)
        # Sample outputs after the rising edge (before ReadOnly)
        if dut.wb_cyc_o.value == 1 and dut.wb_stb_o.value == 1 and dut.wb_ack_i.value == 0:
            addr = int(dut.wb_adr_o.value)
            if dut.wb_we_o.value == 0:
                dut.wb_dat_i.value = read_data_map.get(addr, 0xDEADBEEF)
            dut.wb_ack_i.value = 1
        else:
            dut.wb_ack_i.value = 0


@cocotb.test()
async def test_ping_response(dut):
    """Single-byte PING command should return RES_OK."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    # PING is a zero-payload opcode (sent with tlast=1 on first byte)
    await send_cmd_stream(dut, [OP_PING])

    resp = await collect_response(dut)
    dut._log.info(f"PING response: {[hex(b) for b in resp]}")
    assert len(resp) >= 1, "No response to PING!"
    assert resp[0] == RES_OK, f"Expected RES_OK, got 0x{resp[0]:02X}"


@cocotb.test()
async def test_hello_response(dut):
    """HELLO command should return RES_OK."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    await send_cmd_stream(dut, [OP_HELLO])

    resp = await collect_response(dut)
    dut._log.info(f"HELLO response: {[hex(b) for b in resp]}")
    assert resp[0] == RES_OK


@cocotb.test()
async def test_get_caps_response(dut):
    """GET_CAPS command should return RES_OK."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    await send_cmd_stream(dut, [OP_GET_CAPS])

    resp = await collect_response(dut)
    dut._log.info(f"GET_CAPS response: {[hex(b) for b in resp]}")
    assert resp[0] == RES_OK


@cocotb.test()
async def test_read_block(dut):
    """READ_BLOCK should execute a Wishbone read and return the data."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    # Start WB slave responder in background
    wb_map = {0x00000000: SYS_VERSION}
    cocotb.start_soon(wb_auto_responder(dut, wb_map, max_cycles=500))

    # READ_BLOCK: [OP] [Space] [Addr3..Addr0] [LenH] [LenL]
    cmd = struct.pack(">BBBBBBBB", OP_READ_BLOCK, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04)
    await send_cmd_stream(dut, list(cmd))

    resp = await collect_response(dut, max_cycles=500)
    dut._log.info(f"READ_BLOCK response: {[hex(b) for b in resp]}")

    assert len(resp) == 7, f"Expected 7-byte response, got {len(resp)}"
    assert resp[0] == RES_OK, f"Expected RES_OK, got 0x{resp[0]:02X}"

    # Extract 32-bit data from response bytes [3..6]
    read_val = (resp[3] << 24) | (resp[4] << 16) | (resp[5] << 8) | resp[6]
    assert read_val == SYS_VERSION, f"WB read returned 0x{read_val:08X}, expected 0x{SYS_VERSION:08X}"
    dut._log.info(f"READ_BLOCK returned 0x{read_val:08X} — correct!")


@cocotb.test()
async def test_write_block(dut):
    """WRITE_BLOCK should execute a Wishbone write cycle."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    # WB slave auto-ack (we don't need a read map for writes)
    cocotb.start_soon(wb_auto_responder(dut, {}, max_cycles=500))

    # WRITE_BLOCK: [OP] [Space] [Addr3..Addr0] [LenH] [LenL] [D0..D3]
    write_val = 0xCAFEBABE
    cmd = struct.pack(">BBBBBBBBBBBB", OP_WRITE_BLOCK, 0x00,
                      0x00, 0x00, 0x00, 0x04,  # addr=0x04
                      0x00, 0x04,               # len=4
                      (write_val >> 24) & 0xFF,
                      (write_val >> 16) & 0xFF,
                      (write_val >>  8) & 0xFF,
                      (write_val >>  0) & 0xFF)
    await send_cmd_stream(dut, list(cmd))

    resp = await collect_response(dut, max_cycles=500)
    dut._log.info(f"WRITE_BLOCK response: {[hex(b) for b in resp]}")

    assert len(resp) >= 1, "No response to WRITE_BLOCK!"
    assert resp[0] == RES_OK, f"Expected RES_OK, got 0x{resp[0]:02X}"


@cocotb.test()
async def test_response_backpressure(dut):
    """Stall m_rsp_tready to verify the WB master holds response data."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    cocotb.start_soon(wb_auto_responder(dut, {0: SYS_VERSION}, max_cycles=500))

    # Send READ_BLOCK
    cmd = struct.pack(">BBBBBBBB", OP_READ_BLOCK, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04)
    await send_cmd_stream(dut, list(cmd))

    # Stall the response port for 50 cycles
    dut.m_rsp_tready.value = 0
    for _ in range(50):
        await RisingEdge(dut.clk)

    # Now collect with random backpressure
    response = []
    import random
    for _ in range(300):
        dut.m_rsp_tready.value = 1 if random.random() > 0.3 else 0
        await RisingEdge(dut.clk)
        if dut.m_rsp_tvalid.value == 1 and dut.m_rsp_tready.value == 1:
            response.append(int(dut.m_rsp_tdata.value))
            if dut.m_rsp_tlast.value == 1:
                break

    dut._log.info(f"Backpressure response: {[hex(b) for b in response]}")
    assert len(response) == 7, f"Expected 7-byte response under backpressure, got {len(response)}"
    assert response[0] == RES_OK

    read_val = (response[3] << 24) | (response[4] << 16) | (response[5] << 8) | response[6]
    assert read_val == SYS_VERSION, f"Data corrupted under backpressure! Got 0x{read_val:08X}"
    dut._log.info("Response integrity verified under hostile backpressure!")
