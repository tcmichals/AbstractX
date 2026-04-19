import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, ReadOnly

# Channel constants matching asp_router.sv
CH_CONTROL     = 0x01
CH_TELEMETRY   = 0x02
CH_FC_LOG      = 0x03
CH_DEBUG_TRACE = 0x04
CH_ESC_SERIAL  = 0x05
CH_UNKNOWN     = 0xFF

async def reset(dut):
    dut.rst.value = 1
    dut.m_ctrl_tready.value = 1
    dut.m_tel_tready.value = 1
    dut.m_log_tready.value = 1
    dut.m_dbg_tready.value = 1
    dut.m_esc_tready.value = 1
    dut.s_frame_tvalid.value = 0
    dut.s_frame_tdata.value = 0
    dut.s_frame_tlast.value = 0
    dut.s_frame_channel.value = 0
    dut.s_frame_flags.value = 0
    dut.s_frame_seq.value = 0
    dut.s_frame_payload_len.value = 0
    for _ in range(5):
        await RisingEdge(dut.clk)
    dut.rst.value = 0
    await RisingEdge(dut.clk)


@cocotb.test()
async def test_all_channels_routing(dut):
    """Drive frames on each known channel and verify correct egress routing."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    channels = {
        CH_CONTROL:     ("ctrl",  dut.m_ctrl_tvalid, dut.m_ctrl_tdata, dut.m_ctrl_tlast, dut.m_ctrl_tready),
        CH_TELEMETRY:   ("tel",   dut.m_tel_tvalid,  dut.m_tel_tdata,  dut.m_tel_tlast,  dut.m_tel_tready),
        CH_FC_LOG:      ("log",   dut.m_log_tvalid,  dut.m_log_tdata,  dut.m_log_tlast,  dut.m_log_tready),
        CH_DEBUG_TRACE: ("dbg",   dut.m_dbg_tvalid,  dut.m_dbg_tdata,  dut.m_dbg_tlast,  dut.m_dbg_tready),
        CH_ESC_SERIAL:  ("esc",   dut.m_esc_tvalid,  dut.m_esc_tdata,  dut.m_esc_tlast,  dut.m_esc_tready),
    }

    for ch_id, (name, tvalid, tdata, tlast, tready) in channels.items():
        payload = [ch_id, 0xDE, 0xAD]
        # Accept on target port, stall all others
        for _, (_, _, _, _, other_tready) in channels.items():
            other_tready.value = 0
        tready.value = 1

        collected = []
        dut.s_frame_channel.value = ch_id
        for i, b in enumerate(payload):
            dut.s_frame_tdata.value = b
            dut.s_frame_tvalid.value = 1
            dut.s_frame_tlast.value = 1 if i == len(payload) - 1 else 0
            # Combinational router: data appears on output same cycle
            # Sample after rising edge settles
            await RisingEdge(dut.clk)

        # After loop, sample the last byte that was driven
        dut.s_frame_tvalid.value = 0
        dut.s_frame_tlast.value = 0

        # The router is purely combinational passthrough. We drove 3 bytes
        # across 3 rising edges with tready=1. The downstream slave saw all 3.
        # To verify, we re-drive and sample simultaneously.
        # Reset and do a proper drive+sample loop:
        collected = []
        for i, b in enumerate(payload):
            dut.s_frame_tdata.value = b
            dut.s_frame_tvalid.value = 1
            dut.s_frame_tlast.value = 1 if i == len(payload) - 1 else 0
            await ReadOnly()
            # Combinational: output should mirror input right now
            if int(tvalid.value) == 1 and int(tready.value) == 1:
                collected.append(int(tdata.value))
            await RisingEdge(dut.clk)

        dut.s_frame_tvalid.value = 0
        dut.s_frame_tlast.value = 0
        await RisingEdge(dut.clk)

        dut._log.info(f"Channel {name} (0x{ch_id:02X}): sent {[hex(b) for b in payload]}, got {[hex(b) for b in collected]}")
        assert collected == payload, f"Channel {name} routing mismatch!"

    dut._log.info("All 5 channels verified independently.")


@cocotb.test()
async def test_unknown_channel_dropped(dut):
    """Verify unknown channel frames are consumed (tready=1) but dropped."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    payload = [0x01, 0x02, 0x03]
    dut.s_frame_channel.value = CH_UNKNOWN

    drop_seen = False
    for i, b in enumerate(payload):
        dut.s_frame_tdata.value = b
        dut.s_frame_tvalid.value = 1
        dut.s_frame_tlast.value = 1 if i == len(payload) - 1 else 0
        await ReadOnly()
        assert dut.s_frame_tready.value == 1, "Unknown channel must always accept (drop)!"
        # Advance past ReadOnly before any writes
        await RisingEdge(dut.clk)
        # o_route_drop is registered — it pulses 1 cycle after each handshake
        # We're now at the start of the NEXT cycle, safe to read registered output
        await ReadOnly()
        if dut.o_route_drop.value == 1:
            drop_seen = True
        # Advance past ReadOnly so the next loop iteration can write
        await RisingEdge(dut.clk)

    dut.s_frame_tvalid.value = 0
    dut.s_frame_tlast.value = 0
    await RisingEdge(dut.clk)

    assert drop_seen, "Router should report drop for unknown channel!"
    dut._log.info("Unknown channel correctly dropped with tready=1.")


@cocotb.test()
async def test_backpressure_propagation(dut):
    """Verify downstream tready stalls propagate to ingress s_frame_tready."""
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset(dut)

    # Stall the CONTROL output port
    dut.m_ctrl_tready.value = 0
    dut.s_frame_channel.value = CH_CONTROL
    dut.s_frame_tdata.value = 0xFF
    dut.s_frame_tvalid.value = 1
    dut.s_frame_tlast.value = 0

    await RisingEdge(dut.clk)
    await ReadOnly()
    assert dut.s_frame_tready.value == 0, "Backpressure must propagate upstream!"

    # Release backpressure (wait for next RisingEdge first to exit ReadOnly)
    await RisingEdge(dut.clk)
    dut.m_ctrl_tready.value = 1
    await RisingEdge(dut.clk)
    await ReadOnly()
    assert dut.s_frame_tready.value == 1, "Ready should propagate when downstream accepts!"

    await RisingEdge(dut.clk)
    dut.s_frame_tvalid.value = 0
    dut._log.info("Backpressure propagation verified.")
