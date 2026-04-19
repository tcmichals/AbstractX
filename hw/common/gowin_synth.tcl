# Yosys synthesis script for Gowin FPAGs
# Usage: yosys -v 2 -p "tcl hw/common/gowin_synth.tcl" -D TOP=top_tang9k ...

yosys -import

# Read all SV files from RTL and HW specific folders
read_verilog -sv rtl/asp_top.sv
read_verilog -sv rtl/asp_axis_fifo.sv
read_verilog -sv rtl/asp_router.sv
read_verilog -sv rtl/asp_sys_regs.sv
read_verilog -sv rtl/asp_wishbone_master.sv
read_verilog -sv rtl/spi/asp_spi_frontend.sv
read_verilog -sv rtl/spi/asp_spi_reg_bank.sv

# Read Board Top
read_verilog -sv $::env(BOARD_TOP_FILE)

hierarchy -top $::env(TOP_MODULE)

# High-level synthesis
synth_gowin -top $::env(TOP_MODULE) -json $::env(OUTPUT_JSON)
