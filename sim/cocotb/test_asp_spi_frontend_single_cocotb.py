# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Single-SPI build of asp_spi_frontend (SIM_PARAMS=-GDUAL_SPI_ENABLE=0).
# @verifies [SPEC-TLP-CRC-01] docs/tier2_contracts/TLP_BUS_SPECIFICATION.md

import cocotb

from test_asp_spi_frontend_cocotb import (  # noqa: F401  (re-exported tests)
    test_empty_read_returns_zeros,
    test_incremental_crc_varied_frames,
)


@cocotb.test()
async def test_built_single_spi(dut):
    """Guard: this module must run against the single-SPI build."""
    assert int(dut.DUAL_SPI_ENABLE.value) == 0, "run with SIM_PARAMS=-GDUAL_SPI_ENABLE=0"
