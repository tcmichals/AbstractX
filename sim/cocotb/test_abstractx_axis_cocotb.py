import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge


async def _reset_dut(dut, cycles=5):
    dut.i_rst_n.value = 0
    dut.i_spi_rx_data.value = 0
    dut.i_spi_rx_valid.value = 0
    dut.i_spi_cs_end.value = 0
    dut.i_spi_tx_ready.value = 0
    dut.i_ing_tready.value = 0
    dut.i_egr_tdata.value = 0
    dut.i_egr_tvalid.value = 0
    dut.i_rx_len.value = 0
    dut.i_status_rx_overflow.value = 0
    dut.i_status_crc_err.value = 0
    dut.i_status_len_err.value = 0
    dut.i_status_resync_evt.value = 0

    for _ in range(cycles):
        await RisingEdge(dut.i_clk)
    dut.i_rst_n.value = 1
    await RisingEdge(dut.i_clk)


@cocotb.test()
async def test_read_status_layout(dut):
    """Validate READ_STATUS returns [VERSION, STATUS, LEN_H, LEN_L]."""
    cocotb.start_soon(Clock(dut.i_clk, 10, units="ns").start())
    await _reset_dut(dut)

    # Make status deterministic.
    dut.i_rx_len.value = 0x1234
    dut.i_ing_tready.value = 1  # sets TX_ACCEPT status bit in baseline layout

    # Send command byte 0x01 (READ_STATUS)
    dut.i_spi_rx_data.value = 0x01
    dut.i_spi_rx_valid.value = 1
    await RisingEdge(dut.i_clk)
    dut.i_spi_rx_valid.value = 0

    # Collect 4 response bytes.
    got = []
    for _ in range(4):
        dut.i_spi_tx_ready.value = 1
        await RisingEdge(dut.i_clk)
        if int(dut.o_spi_tx_valid.value) == 1:
            got.append(int(dut.o_spi_tx_data.value))

    # Expected: version=0x01, status has RX_READY + TX_ACCEPT, len=0x1234
    assert len(got) >= 4, f"Expected >=4 status bytes, got {got}"
    assert got[0] == 0x01, f"Version byte mismatch: {got[0]:02x}"
    assert (got[1] & 0x01) == 0x01, "RX_READY bit should be set"
    assert (got[1] & 0x10) == 0x10, "TX_ACCEPT bit should be set"
    assert got[2] == 0x12 and got[3] == 0x34, f"RX_LEN mismatch: {got[2:4]}"


@cocotb.test()
async def test_write_data_forwards_to_ingress_seam(dut):
    """Validate WRITE_DATA command forwards payload into ingress valid-ready seam."""
    cocotb.start_soon(Clock(dut.i_clk, 10, units="ns").start())
    await _reset_dut(dut)

    dut.i_ing_tready.value = 1

    # Send WRITE_DATA command
    dut.i_spi_rx_data.value = 0x80
    dut.i_spi_rx_valid.value = 1
    await RisingEdge(dut.i_clk)

    # Send one payload byte
    dut.i_spi_rx_data.value = 0xAB
    await RisingEdge(dut.i_clk)

    assert int(dut.o_ing_tvalid.value) == 1, "o_ing_tvalid should assert in WRITE_DATA mode"
    assert int(dut.o_ing_tdata.value) == 0xAB, "o_ing_tdata should carry payload byte"

    # End transaction to return to WAIT_CMD
    dut.i_spi_rx_valid.value = 0
    dut.i_spi_cs_end.value = 1
    await RisingEdge(dut.i_clk)
    dut.i_spi_cs_end.value = 0


@cocotb.test()
async def test_read_data_consumes_egress_seam(dut):
    """Validate READ_DATA drives egress ready/valid handshake."""
    cocotb.start_soon(Clock(dut.i_clk, 10, units="ns").start())
    await _reset_dut(dut)

    # Command READ_DATA
    dut.i_spi_rx_data.value = 0x02
    dut.i_spi_rx_valid.value = 1
    await RisingEdge(dut.i_clk)
    dut.i_spi_rx_valid.value = 0

    # Provide one egress byte and consume it over SPI tx
    dut.i_egr_tdata.value = 0x5A
    dut.i_egr_tvalid.value = 1
    dut.i_spi_tx_ready.value = 1
    await RisingEdge(dut.i_clk)

    assert int(dut.o_spi_tx_valid.value) == 1, "o_spi_tx_valid should follow egress valid"
    assert int(dut.o_spi_tx_data.value) == 0x5A, "o_spi_tx_data should mirror egress data"
    assert int(dut.o_egr_tready.value) == 1, "o_egr_tready should follow i_spi_tx_ready"

    # End transaction
    dut.i_spi_cs_end.value = 1
    await RisingEdge(dut.i_clk)
    dut.i_spi_cs_end.value = 0
