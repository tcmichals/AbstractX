# sim/cocotb/Makefile

# Default to verilator
SIM ?= verilator
TOPLEVEL_LANG ?= verilog

# Paths to RTL
RTL_DIR = $(PWD)/../../rtl

# Default overridable variables for multi-target CMake integration
TOPLEVEL ?= asp_top
MODULE ?= test_asp_tlp_64b_cocotb

# One list per block, no file twice ([SPEC-TLP-04] package first; Forencich AXI blocks from pio2's
# third_party, unmodified; verilog-axis provides arbiter / priority_encoder for verilog-axi as in pio2).
TLP_PKG   = $(RTL_DIR)/asp_tlp_pkg.sv
AXI_TP    = $(RTL_DIR)/pio/third_party
AXIL_IF   = $(AXI_TP)/verilog-axi/rtl/axil_reg_if.v $(AXI_TP)/verilog-axi/rtl/axil_reg_if_rd.v \
            $(AXI_TP)/verilog-axi/rtl/axil_reg_if_wr.v
AXIL_XBAR = $(AXI_TP)/verilog-axis/rtl/arbiter.v $(AXI_TP)/verilog-axis/rtl/priority_encoder.v \
            $(AXI_TP)/verilog-axi/rtl/axil_crossbar.v $(AXI_TP)/verilog-axi/rtl/axil_crossbar_addr.v \
            $(AXI_TP)/verilog-axi/rtl/axil_crossbar_rd.v $(AXI_TP)/verilog-axi/rtl/axil_crossbar_wr.v \
            $(AXI_TP)/verilog-axi/rtl/axil_register_rd.v $(AXI_TP)/verilog-axi/rtl/axil_register_wr.v
# PIO tile behind AbstractX's wrapper ([SPEC-TLP-05], [SPEC-TLP-06]), without the package / reg_if
PIO_WRAP_ONLY = $(RTL_DIR)/pio/src/rtl/asp_pio_sm.sv \
                $(RTL_DIR)/pio/src/rtl/asp_pio_tile.sv \
                $(RTL_DIR)/pio_tlp/asp_pio_tlp_ingress.sv \
                $(RTL_DIR)/pio_tlp/asp_pio_tlp_shim.sv \
                $(RTL_DIR)/asp_pio_wrap.sv
PIO_WRAP_SOURCES = $(TLP_PKG) $(AXIL_IF) $(PIO_WRAP_ONLY)
CTRL_ONLY = $(RTL_DIR)/asp_tlp_axil_master.sv $(RTL_DIR)/asp_sys_regs.sv $(RTL_DIR)/asp_ctrl_plane.sv

ifeq ($(TOPLEVEL), asp_spi_frontend)
    VERILOG_SOURCES = $(RTL_DIR)/spi/asp_spi_frontend.sv
else ifeq ($(TOPLEVEL), asp_router)
    VERILOG_SOURCES = $(TLP_PKG) $(RTL_DIR)/asp_router.sv
else ifeq ($(TOPLEVEL), asp_sys_regs)
    VERILOG_SOURCES = $(AXIL_IF) $(RTL_DIR)/asp_sys_regs.sv
else ifeq ($(TOPLEVEL), asp_tlp_axil_master)
    VERILOG_SOURCES = $(TLP_PKG) $(RTL_DIR)/asp_tlp_axil_master.sv
else ifeq ($(TOPLEVEL), asp_ctrl_plane)
    VERILOG_SOURCES = $(TLP_PKG) $(AXIL_IF) $(AXIL_XBAR) $(CTRL_ONLY)
else ifeq ($(TOPLEVEL), asp_axis_fifo)
    VERILOG_SOURCES = $(RTL_DIR)/asp_axis_fifo.sv
else ifeq ($(TOPLEVEL), asp_spi_reg_bank)
    VERILOG_SOURCES = $(RTL_DIR)/spi/asp_spi_reg_bank.sv
else ifeq ($(TOPLEVEL), asp_axi_lite_bridge)
    VERILOG_SOURCES = $(RTL_DIR)/asp_axi_lite_bridge.sv
else ifeq ($(TOPLEVEL), asp_pio_tile)
    VERILOG_SOURCES = $(RTL_DIR)/pio/src/rtl/asp_pio_sm.sv \
                      $(RTL_DIR)/pio/src/rtl/asp_pio_tile.sv
else ifeq ($(TOPLEVEL), asp_pio_wrap)
    VERILOG_SOURCES = $(PIO_WRAP_SOURCES)
else ifeq ($(TOPLEVEL), asp_top)
    VERILOG_SOURCES = $(TLP_PKG) $(AXIL_IF) $(AXIL_XBAR) $(CTRL_ONLY) $(PIO_WRAP_ONLY) \
                      $(RTL_DIR)/asp_top.sv \
                      $(RTL_DIR)/asp_axis_fifo.sv \
                      $(RTL_DIR)/asp_router.sv \
                      $(RTL_DIR)/spi/asp_spi_frontend.sv \
                      $(RTL_DIR)/asp_pio_pins.sv
endif

# Module parameter overrides for a test variant, e.g. SIM_PARAMS=-GDUAL_SPI_ENABLE=0
SIM_PARAMS ?=
# Waveform tracing needs lz4 headers; set TRACE= to build without it.
TRACE ?= --trace-fst --trace-structs
EXTRA_ARGS += $(TRACE) -I$(RTL_DIR) -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Wno-SELRANGE $(SIM_PARAMS)
# [SPEC-TLP-06] waiver for the wrapper's stream link (reason in the file); Verilator builds only
ifneq ($(filter $(VERILOG_SOURCES),$(RTL_DIR)/asp_pio_wrap.sv),)
    EXTRA_ARGS += $(RTL_DIR)/pio_tlp/asp_pio_wrap.vlt
endif
# Third-party AXI blocks are used unmodified; their lint is not ours (pio2 third_party.vlt)
EXTRA_ARGS += $(RTL_DIR)/pio/src/rtl/third_party.vlt

include $(shell cocotb-config --makefiles)/Makefile.sim

