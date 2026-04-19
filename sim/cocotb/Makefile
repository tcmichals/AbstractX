# sim/cocotb/Makefile

# Default to verilator
SIM ?= verilator
TOPLEVEL_LANG ?= verilog

# Paths to RTL
RTL_DIR = $(PWD)/../../rtl

# Default overridable variables for multi-target CMake integration
TOPLEVEL ?= asp_spi_reg_bank
# Source bindings specific to the TOPLEVEL module requested by CMake
ifeq ($(TOPLEVEL), asp_spi_reg_bank)
    VERILOG_SOURCES = $(RTL_DIR)/spi/asp_spi_reg_bank.sv
else ifeq ($(TOPLEVEL), asp_spi_frontend)
    VERILOG_SOURCES = $(RTL_DIR)/spi/asp_spi_frontend.sv
else ifeq ($(TOPLEVEL), asp_axis_fifo)
    VERILOG_SOURCES = $(RTL_DIR)/asp_axis_fifo.sv
else ifeq ($(TOPLEVEL), asp_router)
    VERILOG_SOURCES = $(RTL_DIR)/asp_router.sv
else ifeq ($(TOPLEVEL), asp_sys_regs)
    VERILOG_SOURCES = $(RTL_DIR)/asp_sys_regs.sv
else ifeq ($(TOPLEVEL), asp_wishbone_master)
    VERILOG_SOURCES = $(RTL_DIR)/asp_wishbone_master.sv
else ifeq ($(TOPLEVEL), asp_top)
    VERILOG_SOURCES = $(RTL_DIR)/asp_top.sv \
                      $(RTL_DIR)/spi/asp_spi_frontend.sv \
                      $(RTL_DIR)/spi/asp_spi_reg_bank.sv \
                      $(RTL_DIR)/asp_axis_fifo.sv \
                      $(RTL_DIR)/asp_router.sv \
                      $(RTL_DIR)/asp_wishbone_master.sv \
                      $(RTL_DIR)/asp_sys_regs.sv
endif

include $(shell cocotb-config --makefiles)/Makefile.sim
