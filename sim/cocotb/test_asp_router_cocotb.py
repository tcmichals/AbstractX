# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, ClockCycles

CH_CONTROL    = 0x01
CH_TELEMETRY  = 0x02
CH_ESC_SERIAL = 0x05
CH_UNKNOWN    = 0xFE


def make_tlp(channel: int, payload_val: int = 0x12345678) -> int:
    """Builds a 512-bit TLP integer with given channel in bits [487:480]."""
    dw0 = (0x02 << 24) | (0x00 << 16) | (0x00 << 8) | (channel & 0xFF)
    return (dw0 << 480) | (payload_val << 320) | 0xDEADBEEF


async def reset(dut):
    dut.rst_n.value = 0
    dut.s_tlp_tvalid.value = 0
    dut.s_tlp_tdata.value = 0
    dut.m_ctrl_tready.value = 1
    dut.m_tel_tready.value = 1
    dut.m_esc_tready.value = 1
    dut.m_egr_tready.value = 1
    dut.s_wb_cpl_tvalid.value = 0
    dut.s_wb_cpl_tdata.value = 0
    dut.s_imu_stream_tvalid.value = 0
    dut.s_imu_stream_tdata.value = 0
    dut.s_esc_stream_tvalid.value = 0
    dut.s_esc_stream_tdata.value = 0
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1
    await ClockCycles(dut.clk, 5)


@cocotb.test()
async def test_ingress_routing(dut):
    """Verify 64-byte TLP routing to correct downstream endpoints by channel ID."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    destinations = [
        (CH_CONTROL,    dut.m_ctrl_tvalid, dut.m_ctrl_tdata, "CONTROL (Wishbone)"),
        (CH_TELEMETRY,  dut.m_tel_tvalid,  dut.m_tel_tdata,  "TELEMETRY (IMU)"),
        (CH_ESC_SERIAL, dut.m_esc_tvalid,  dut.m_esc_tdata,  "ESC_SERIAL (DShot)"),
    ]

    for ch_id, out_valid, out_data, name in destinations:
        tlp_val = make_tlp(ch_id, 0xAABBCCDD)
        dut.s_tlp_tdata.value = tlp_val
        dut.s_tlp_tvalid.value = 1
        await RisingEdge(dut.clk)

        assert out_valid.value == 1, f"Channel {name} valid not asserted!"
        assert int(out_data.value) == tlp_val, f"Channel {name} data mismatch!"
        dut._log.info(f"[SUCCESS] Routed TLP to {name} correctly!")

        dut.s_tlp_tvalid.value = 0
        await RisingEdge(dut.clk)


@cocotb.test()
async def test_ingress_unknown_channel_dropped(dut):
    """Unknown channel TLPs must be accepted with ready=1 and dropped without asserting any output valid."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    tlp_val = make_tlp(CH_UNKNOWN, 0x00000000)
    dut.s_tlp_tdata.value = tlp_val
    dut.s_tlp_tvalid.value = 1
    await RisingEdge(dut.clk)

    assert dut.s_tlp_tready.value == 1, "Router should accept and drop unknown channel!"
    assert dut.m_ctrl_tvalid.value == 0, "Control port should not assert on unknown channel!"
    assert dut.m_tel_tvalid.value == 0, "Telemetry port should not assert on unknown channel!"
    assert dut.m_esc_tvalid.value == 0, "ESC port should not assert on unknown channel!"
    dut._log.info("[SUCCESS] Unknown channel 0xFE safely dropped!")


@cocotb.test()
async def test_ingress_backpressure(dut):
    """Downstream port stalling must propagate backpressure to s_tlp_tready."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    # Stall CONTROL port
    dut.m_ctrl_tready.value = 0
    tlp_val = make_tlp(CH_CONTROL)
    dut.s_tlp_tdata.value = tlp_val
    dut.s_tlp_tvalid.value = 1
    await RisingEdge(dut.clk)

    assert dut.s_tlp_tready.value == 0, "Backpressure failed to propagate to s_tlp_tready!"

    # Release stall
    dut.m_ctrl_tready.value = 1
    await RisingEdge(dut.clk)
    assert dut.s_tlp_tready.value == 1, "Ready failed to restore after backpressure release!"
    dut._log.info("[SUCCESS] Ingress backpressure propagation verified!")


@cocotb.test()
async def test_egress_priority_arbitration(dut):
    """Egress arbiter must prioritize Wishbone CplD over IMU Stream over ESC Stream."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    tlp_cpl = make_tlp(CH_CONTROL, 0x11111111)
    tlp_imu = make_tlp(CH_TELEMETRY, 0x22222222)
    tlp_esc = make_tlp(CH_ESC_SERIAL, 0x33333333)

    # Assert all three concurrently
    dut.s_wb_cpl_tdata.value = tlp_cpl
    dut.s_wb_cpl_tvalid.value = 1
    dut.s_imu_stream_tdata.value = tlp_imu
    dut.s_imu_stream_tvalid.value = 1
    dut.s_esc_stream_tdata.value = tlp_esc
    dut.s_esc_stream_tvalid.value = 1

    # 1. Wishbone CplD must win first
    await RisingEdge(dut.clk)
    assert dut.m_egr_tvalid.value == 1
    assert int(dut.m_egr_tdata.value) == tlp_cpl
    assert dut.s_wb_cpl_tready.value == 1
    assert dut.s_imu_stream_tready.value == 0
    assert dut.s_esc_stream_tready.value == 0
    dut._log.info("[SUCCESS] Priority 1: Wishbone CplD won egress arbitration!")

    # Clear CplD, IMU should win next
    dut.s_wb_cpl_tvalid.value = 0
    await RisingEdge(dut.clk)
    assert dut.m_egr_tvalid.value == 1
    assert int(dut.m_egr_tdata.value) == tlp_imu
    assert dut.s_imu_stream_tready.value == 1
    assert dut.s_esc_stream_tready.value == 0
    dut._log.info("[SUCCESS] Priority 2: IMU Telemetry won egress arbitration!")

    # Clear IMU, ESC should win last
    dut.s_imu_stream_tvalid.value = 0
    await RisingEdge(dut.clk)
    assert dut.m_egr_tvalid.value == 1
    assert int(dut.m_egr_tdata.value) == tlp_esc
    assert dut.s_esc_stream_tready.value == 1
    dut._log.info("[SUCCESS] Priority 3: ESC Stream won egress arbitration!")

