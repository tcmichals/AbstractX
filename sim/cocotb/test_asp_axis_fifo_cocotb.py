# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later

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
async def test_axis_fifo_torture(dut):
    """ Validates AbstractX Constraints using generic Tester framework """
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)
    
    dut._log.info("Starting AbstractX Hostile Environment Test on generic FIFO")
    # Initialize our Torture Test wrappers (random backpressure, etc)
    driver = ASP_AXIS_Driver(dut.clk, dut.s_axis_tdata, dut.s_axis_tvalid, dut.s_axis_tready, dut.s_axis_tlast)
    monitor = ASP_AXIS_Monitor(dut.clk, dut.m_axis_tdata, dut.m_axis_tvalid, dut.m_axis_tready, dut.m_axis_tlast)
    
    payloads = [
        b"\x00\x11\x22",
        b"\xFF\xEE\xDD\xCC\xBB",
        b"\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A"
    ]
    
    for payload in payloads:
        await driver.send_frame(list(payload), random_delays=True)
        
    # Drain the pipeline under hostile backpressure
    for _ in range(200):
        await RisingEdge(dut.clk)
        
    monitor.stop()
    
    dut._log.info(f"Monitor caught {len(monitor.received_frames)} frames out of {len(payloads)}")
    assert len(monitor.received_frames) == len(payloads), "Data was dropped!"
    for i, payload in enumerate(payloads):
        assert monitor.received_frames[i] == payload, f"Frame {i} corrupted!"
