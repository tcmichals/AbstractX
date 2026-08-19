# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, ClockCycles

CMD_READ_STATUS = 0xA0
CMD_WRITE_BURST = 0xA1
CMD_READ_BURST  = 0xA2


async def reset(dut):
    dut.rst_n.value = 0
    dut.i_cs_n.value = 1
    dut.i_sclk.value = 0
    dut.io_sdio0.value = 0
    dut.io_sdio1.value = 0
    dut.i_egress_count.value = 0
    dut.i_tlp_rx_ready.value = 1
    dut.i_tlp_tx_data.value = 0
    dut.i_tlp_tx_valid.value = 0
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1
    await ClockCycles(dut.clk, 5)


async def send_cmd_byte(dut, cmd_byte: int):
    """Sends 8-bit command over MOSI (io_sdio0)."""
    for b in range(7, -1, -1):
        dut.io_sdio0.value = (cmd_byte >> b) & 1
        dut.io_sdio1.value = 0
        await ClockCycles(dut.clk, 2)
        dut.i_sclk.value = 1
        await ClockCycles(dut.clk, 4)
        dut.i_sclk.value = 0
        await ClockCycles(dut.clk, 2)


@cocotb.test()
async def test_read_status_command(dut):
    """CMD 0xA0 must shift out 32-bit status frame containing egress count and diagnostics."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    dut.i_egress_count.value = 0x07
    await ClockCycles(dut.clk, 2)

    # Assert CS
    dut.i_cs_n.value = 0
    await ClockCycles(dut.clk, 10)

    # Send 0xA0 command byte
    await send_cmd_byte(dut, CMD_READ_STATUS)

    # Read 32 bits in Dual-SPI mode (16 clocks, 2 bits per clock)
    rx_bits = 0
    for _ in range(16):
        await ClockCycles(dut.clk, 2)
        dut.i_sclk.value = 1
        await ClockCycles(dut.clk, 2)
        b0 = int(dut.io_sdio0_o.value)
        b1 = int(dut.io_sdio1_o.value)
        rx_bits = (rx_bits << 2) | ((b1 << 1) | b0)
        await ClockCycles(dut.clk, 2)
        dut.i_sclk.value = 0
        await ClockCycles(dut.clk, 2)

    dut.i_cs_n.value = 1
    await ClockCycles(dut.clk, 10)

    dut._log.info(f"Status word read: 0x{rx_bits:08X}")
    assert (rx_bits >> 24) == 0x64, f"Status magic 0x64 mismatch: got 0x{(rx_bits >> 24):02X}"
    egress_cnt_read = (rx_bits >> 8) & 0xFF
    assert egress_cnt_read == 0x07, f"Egress count mismatch: expected 7, got {egress_cnt_read}"
    dut._log.info("[SUCCESS] CMD 0xA0 Status frame verified!")


@cocotb.test()
async def test_write_and_read_tlp_bursts(dut):
    """CMD 0xA1 write burst and CMD 0xA2 read burst over Dual-SPI."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    # 1. Test Ingress Write (0xA1) with rx_ready=0 to hold valid
    dut.i_tlp_rx_ready.value = 0
    test_payload = bytes([i % 256 for i in range(64)])
    dut.i_cs_n.value = 0
    await ClockCycles(dut.clk, 10)
    await send_cmd_byte(dut, CMD_WRITE_BURST)

    for byte_val in test_payload:
        for pair_idx in range(3, -1, -1):
            val2 = (byte_val >> (pair_idx * 2)) & 0x3
            dut.io_sdio0.value = val2 & 1
            dut.io_sdio1.value = (val2 >> 1) & 1
            await ClockCycles(dut.clk, 2)
            dut.i_sclk.value = 1
            await ClockCycles(dut.clk, 4)
            dut.i_sclk.value = 0
            await ClockCycles(dut.clk, 2)

    # Check rx_valid asserted upon completion
    await ClockCycles(dut.clk, 5)
    assert dut.o_tlp_rx_valid.value == 1, "o_tlp_rx_valid did not assert!"
    rx_int = int(dut.o_tlp_rx_data.value)
    expected_int = int.from_bytes(test_payload, byteorder='big')
    assert rx_int == expected_int, "Ingress TLP payload mismatch!"
    dut._log.info("[SUCCESS] Ingress Dual-SPI Write 64B TLP verified!")

    # Consume and deassert CS
    dut.i_tlp_rx_ready.value = 1
    await ClockCycles(dut.clk, 2)
    dut.i_tlp_rx_ready.value = 0
    dut.i_cs_n.value = 1
    await ClockCycles(dut.clk, 10)

    # 2. Test Egress Read (0xA2)
    tx_test_payload = bytes([(255 - i) % 256 for i in range(64)])
    dut.i_tlp_tx_data.value = int.from_bytes(tx_test_payload, byteorder='big')
    dut.i_tlp_tx_valid.value = 1
    await ClockCycles(dut.clk, 5)

    dut.i_cs_n.value = 0
    await ClockCycles(dut.clk, 10)
    await send_cmd_byte(dut, CMD_READ_BURST)

    rx_readback = bytearray()
    for _ in range(64):
        byte_val = 0
        for _ in range(4):
            await ClockCycles(dut.clk, 2)
            dut.i_sclk.value = 1
            await ClockCycles(dut.clk, 2)
            b0 = int(dut.io_sdio0_o.value)
            b1 = int(dut.io_sdio1_o.value)
            byte_val = (byte_val << 2) | ((b1 << 1) | b0)
            await ClockCycles(dut.clk, 2)
            dut.i_sclk.value = 0
            await ClockCycles(dut.clk, 2)
        rx_readback.append(byte_val)

    dut.i_cs_n.value = 1
    await ClockCycles(dut.clk, 10)

    assert bytes(rx_readback) == tx_test_payload, "Egress Dual-SPI readback mismatch!"
    dut._log.info("[SUCCESS] Egress Dual-SPI Read 64B TLP verified!")
