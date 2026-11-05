# sim/cocotb/Makefile

# Default to verilator
SIM ?= verilator
TOPLEVEL_LANG ?= verilog

# Paths to RTL
RTL_DIR = $(PWD)/../../rtl

# Default overridable variables for multi-target CMake integration
TOPLEVEL ?= asp_top
MODULE ?= test_asp_tlp_64b_cocotb

# PIO tile behind AbstractX's wrapper ([SPEC-TLP-05], [SPEC-TLP-06]): one list for every build using it
PIO_WRAP_SOURCES = $(RTL_DIR)/pio/src/rtl/asp_pio_sm.sv \
                   $(RTL_DIR)/pio/src/rtl/asp_pio_tile.sv \
                   $(RTL_DIR)/pio/third_party/wb2axip/rtl/wbm2axilite.v \
                   $(RTL_DIR)/pio_tlp/asp_pio_tlp_ingress.sv \
                   $(RTL_DIR)/pio_tlp/asp_pio_tlp_shim.sv \
                   $(RTL_DIR)/asp_pio_wrap.sv

ifeq ($(TOPLEVEL), asp_spi_frontend)
    VERILOG_SOURCES = $(RTL_DIR)/spi/asp_spi_frontend.sv
else ifeq ($(TOPLEVEL), asp_router)
    VERILOG_SOURCES = $(RTL_DIR)/asp_router.sv
else ifeq ($(TOPLEVEL), asp_sys_regs)
    VERILOG_SOURCES = $(RTL_DIR)/asp_sys_regs.sv
else ifeq ($(TOPLEVEL), asp_axis_fifo)
    VERILOG_SOURCES = $(RTL_DIR)/asp_axis_fifo.sv
else ifeq ($(TOPLEVEL), asp_wishbone_master)
    VERILOG_SOURCES = $(RTL_DIR)/asp_wishbone_master.sv
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
    VERILOG_SOURCES = $(RTL_DIR)/asp_top.sv \
                      $(RTL_DIR)/asp_axis_fifo.sv \
                      $(RTL_DIR)/asp_router.sv \
                      $(RTL_DIR)/asp_sys_regs.sv \
                      $(RTL_DIR)/asp_wishbone_master.sv \
                      $(RTL_DIR)/spi/asp_spi_frontend.sv \
                      $(RTL_DIR)/asp_pio_pins.sv \
                      $(PIO_WRAP_SOURCES)
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

include $(shell cocotb-config --makefiles)/Makefile.sim

