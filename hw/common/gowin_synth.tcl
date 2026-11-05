# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later

# Yosys synthesis script for Gowin FPAGs
# Usage: yosys -v 2 -p "tcl hw/common/gowin_synth.tcl" -D TOP=top_tang9k ...

yosys -import

# Read all SV files from RTL and HW specific folders
# One list, package first ([SPEC-TLP-04]); Forencich AXI blocks from pio2 third_party, unmodified
read_verilog -sv rtl/asp_tlp_pkg.sv
foreach f {arbiter priority_encoder} { read_verilog rtl/pio/third_party/verilog-axis/rtl/$f.v }
foreach f {axil_crossbar axil_crossbar_addr axil_crossbar_rd axil_crossbar_wr axil_register_rd axil_register_wr axil_reg_if axil_reg_if_rd axil_reg_if_wr} {
    read_verilog rtl/pio/third_party/verilog-axi/rtl/$f.v
}
read_verilog -sv rtl/asp_top.sv
read_verilog -sv rtl/asp_axis_fifo.sv
read_verilog -sv rtl/asp_router.sv
read_verilog -sv rtl/asp_tlp_axil_master.sv
read_verilog -sv rtl/asp_sys_regs.sv
read_verilog -sv rtl/asp_ctrl_plane.sv
read_verilog -sv rtl/spi/asp_spi_frontend.sv
read_verilog -sv rtl/spi/asp_spi_reg_bank.sv
read_verilog -sv rtl/pio/src/rtl/asp_pio_sm.sv
read_verilog -sv rtl/pio/src/rtl/asp_pio_tile.sv
read_verilog -sv rtl/pio_tlp/asp_pio_tlp_ingress.sv
read_verilog -sv rtl/pio_tlp/asp_pio_tlp_shim.sv
read_verilog -sv rtl/asp_pio_wrap.sv
read_verilog -sv rtl/asp_pio_pins.sv



# Read Board Top
read_verilog -sv $::env(BOARD_TOP_FILE)

hierarchy -top $::env(TOP_MODULE)

# High-level synthesis
synth_gowin -top $::env(TOP_MODULE) -json $::env(OUTPUT_JSON) -nowidelut

