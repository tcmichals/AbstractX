# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later

import struct
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, ClockCycles

# TLP Types matching asp_wishbone_master.sv
TYPE_MEM_RD = 0x01
TYPE_MEM_WR = 0x02
TYPE_CPL_D  = 0x03

SYS_VERSION = 0xA1B2C3D4


def pack_tlp_int(tlp_type: int, flags: int, tag: int, channel: int, addr: int, len_dw: int, seq: int, ts: int, write_data: int = 0) -> int:
    """Packs fields into a 512-bit integer representing a 64-byte TLP vector."""
    dw0 = (tlp_type << 24) | (flags << 16) | (tag << 8) | channel
    dw1 = addr & 0xFFFFFFFF
    dw2 = ((len_dw & 0xFFFF) << 16) | (seq & 0xFFFF)
    dw3_4 = ts & 0xFFFFFFFFFFFFFFFF
    dw5 = write_data & 0xFFFFFFFF

    tlp_val = (dw0 << 480) | (dw1 << 448) | (dw2 << 416) | (dw3_4 << 352) | (dw5 << 320) | 0xDEADBEEF
    return tlp_val


def unpack_cpl_tlp(tlp_val: int):
    """Extracts fields from 512-bit integer CplD TLP."""
    tlp_type = (tlp_val >> 504) & 0xFF
    tag      = (tlp_val >> 488) & 0xFF
    channel  = (tlp_val >> 480) & 0xFF
    addr     = (tlp_val >> 448) & 0xFFFFFFFF
    len_dw   = (tlp_val >> 432) & 0xFFFF
    seq      = (tlp_val >> 416) & 0xFFFF
    ts       = (tlp_val >> 352) & 0xFFFFFFFFFFFFFFFF
    read_data = (tlp_val >> 320) & 0xFFFFFFFF
    return tlp_type, tag, channel, addr, len_dw, seq, ts, read_data


async def reset(dut):
    dut.rst_n.value = 0
    dut.s_tlp_tvalid.value = 0
    dut.s_tlp_tdata.value = 0
    dut.m_cpl_tready.value = 1
    dut.wb_ack_i.value = 0
    dut.wb_dat_i.value = 0
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1
    await ClockCycles(dut.clk, 5)


async def send_tlp(dut, tlp_val: int):
    """Drives a 512-bit TLP into s_tlp with standard ready/valid handshake."""
    dut.s_tlp_tdata.value = tlp_val
    dut.s_tlp_tvalid.value = 1
    while True:
        await RisingEdge(dut.clk)
        if dut.s_tlp_tready.value == 1:
            break
    dut.s_tlp_tvalid.value = 0
    dut.s_tlp_tdata.value = 0


async def wb_slave_responder(dut, read_data_map=None, max_cycles=500):
    """Simulates Wishbone slave responding with single-cycle ack."""
    if read_data_map is None:
        read_data_map = {}
    for _ in range(max_cycles):
        await RisingEdge(dut.clk)
        if dut.wb_cyc_o.value == 1 and dut.wb_stb_o.value == 1 and dut.wb_ack_i.value == 0:
            addr = int(dut.wb_adr_o.value)
            if dut.wb_we_o.value == 0:
                dut.wb_dat_i.value = read_data_map.get(addr, 0xDEADBEEF)
            dut.wb_ack_i.value = 1
        else:
            dut.wb_ack_i.value = 0


@cocotb.test()
async def test_mem_rd_cpld_generation(dut):
    """MemRd TLP should execute Wishbone read and return a valid CplD completion TLP."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    wb_map = {0x40000000: SYS_VERSION}
    cocotb.start_soon(wb_slave_responder(dut, wb_map))

    rd_tlp = pack_tlp_int(
        tlp_type=TYPE_MEM_RD,
        flags=0,
        tag=0x42,
        channel=0x01,
        addr=0x40000000,
        len_dw=1,
        seq=100,
        ts=12345678,
    )
    await send_tlp(dut, rd_tlp)

    # Wait for CplD output
    while dut.m_cpl_tvalid.value == 0:
        await RisingEdge(dut.clk)

    cpl_val = int(dut.m_cpl_tdata.value)
    tlp_type, tag, channel, addr, len_dw, seq, ts, read_data = unpack_cpl_tlp(cpl_val)

    dut._log.info(f"CplD TLP Received: Type=0x{tlp_type:02X} Tag=0x{tag:02X} Addr=0x{addr:08X} Data=0x{read_data:08X}")
    assert tlp_type == TYPE_CPL_D, f"Expected CplD type (0x03), got 0x{tlp_type:02X}"
    assert tag == 0x42, f"Tag mismatch: expected 0x42, got 0x{tag:02X}"
    assert channel == 0x01, f"Channel mismatch: expected 0x01, got 0x{channel:02X}"
    assert addr == 0x40000000, f"Addr mismatch: expected 0x40000000, got 0x{addr:08X}"
    assert read_data == SYS_VERSION, f"Data mismatch: expected 0x{SYS_VERSION:08X}, got 0x{read_data:08X}"
    dut._log.info("[SUCCESS] MemRd -> CplD cycle verified with exact tag matching!")


@cocotb.test()
async def test_mem_wr_execution(dut):
    """MemWr TLP should execute Wishbone write cycle with latched data."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    cocotb.start_soon(wb_slave_responder(dut, {}))

    wr_tlp = pack_tlp_int(
        tlp_type=TYPE_MEM_WR,
        flags=0,
        tag=0x11,
        channel=0x01,
        addr=0x40000200,
        len_dw=1,
        seq=101,
        ts=23456789,
        write_data=0xCAFEBABE,
    )
    await send_tlp(dut, wr_tlp)

    # Wait for Wishbone cycle to complete
    for _ in range(20):
        await RisingEdge(dut.clk)
        if dut.wb_cyc_o.value == 1 and dut.wb_we_o.value == 1:
            assert int(dut.wb_adr_o.value) == 0x40000200, f"Wrong WB write addr: 0x{int(dut.wb_adr_o.value):08X}"
            assert int(dut.wb_dat_o.value) == 0xCAFEBABE, f"Wrong WB write data: 0x{int(dut.wb_dat_o.value):08X}"
            dut._log.info("[SUCCESS] Wishbone write executed with exact address 0x40000200 and data 0xCAFEBABE!")
            break


@cocotb.test()
async def test_cpld_backpressure(dut):
    """Wishbone Master must hold CplD output when m_cpl_tready is stalled."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    wb_map = {0x40000010: 0x55AA55AA}
    cocotb.start_soon(wb_slave_responder(dut, wb_map))

    # Stall egress port
    dut.m_cpl_tready.value = 0

    rd_tlp = pack_tlp_int(
        tlp_type=TYPE_MEM_RD,
        flags=0,
        tag=0x99,
        channel=0x01,
        addr=0x40000010,
        len_dw=1,
        seq=102,
        ts=34567890,
    )
    await send_tlp(dut, rd_tlp)

    # Wait until m_cpl_tvalid asserts
    for _ in range(50):
        await RisingEdge(dut.clk)
        if dut.m_cpl_tvalid.value == 1:
            break

    assert dut.m_cpl_tvalid.value == 1, "CplD valid did not assert under backpressure!"

    # Hold backpressure for 20 more cycles and verify CplD remains valid
    for _ in range(20):
        await RisingEdge(dut.clk)
        assert dut.m_cpl_tvalid.value == 1, "CplD valid dropped while stalled!"

    # Release backpressure and capture CplD
    dut.m_cpl_tready.value = 1
    await RisingEdge(dut.clk)

    cpl_val = int(dut.m_cpl_tdata.value)
    _, tag, _, _, _, _, _, read_data = unpack_cpl_tlp(cpl_val)
    assert tag == 0x99
    assert read_data == 0x55AA55AA
    dut._log.info("[SUCCESS] CplD backpressure held data cleanly until ready asserted!")

