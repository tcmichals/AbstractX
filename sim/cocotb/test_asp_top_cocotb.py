# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer, ReadOnly
import struct
from asp_e2e_vip import AbstractX_VIP

OP_READ_BLOCK   = 0x10
OP_WRITE_BLOCK  = 0x11

@cocotb.test()
async def test_end_to_end_wishbone_read(dut):
    """
    End-to-End Simulation: SPI -> RegBank -> Wishbone Master -> Sys Regs 
    Proves that a host SPI write natively tunnels via ASP_E2E_VIP to query the bus.
    """
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start()) # 100MHz core
    vip = AbstractX_VIP(dut)
    
    await vip.reset_hardware()
    
    # 1. Send READ_BLOCK over SPI targeting Address 0x00000000 (SYS_VERSION)
    # Wishbone Master OP format: [OP] [Space] [Addr3] [Addr2] [Addr1] [Addr0] [LenH] [LenL]
    payload = struct.pack(">BBBBBBBB", OP_READ_BLOCK, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04)
    
    dut._log.info("E2E INGRESS: Injecting SPI Payload into Routing Fabric")
    await vip.write_stream(payload)
    
    # Wait for the internal Wishbone state machine to naturally execute
    for _ in range(100):
        await RisingEdge(dut.clk)
        
    # 2. Read Status (Poll until RX_LEN > 0)
    dut._log.info("E2E STATUS: Polling Wishbone Response Status...")
    rx_len = 0
    for _ in range(25):
        ver, status, rx_len = await vip.read_status()
        if rx_len > 0:
            break
            
    assert rx_len > 0, "Wishbone timeout! VIP did not receive an ingress notification."
    assert rx_len == 7, f"Wishbone response should be exactly 7 bytes, got {rx_len}"
    dut._log.info(f"E2E STATUS: Registered {rx_len} byte reply waiting in AXIS Egress FIFO.")
            
    # 3. Read Data payload from Egress
    dut._log.info("E2E EGRESS: Pulling Target Output over SPI Hardware Pins")
    wb_resp = await vip.read_stream(rx_len)
    
    dut._log.info(f"Wishbone output bytes: {[hex(b) for b in wb_resp]}")
    
    # Wishbone Response format: [RES_OK] [LenH] [LenL] [D0] [D1] [D2] [D3]
    assert wb_resp[0] == 0x00, f"Expected RES_OK (0x00), got {wb_resp[0]}"
    
    # The SYS_VERSION parameter in asp_top is naturally defined as 0xA1B2C3D4
    sys_version = (wb_resp[3] << 24) | (wb_resp[4] << 16) | (wb_resp[5] << 8) | (wb_resp[6])
    assert sys_version == 0xA1B2C3D4, f"End-to-End pipeline structural failure! Got {hex(sys_version)}"
    
    dut._log.info(f"E2E VERIFICATION PASSED: Pulled SYS_VERSION ({hex(sys_version)}) over hardware SPI wrapper.")
