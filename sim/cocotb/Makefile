# sim/cocotb/Makefile

# Default to verilator
SIM ?= verilator
TOPLEVEL_LANG ?= verilog

# Paths to RTL
RTL_DIR = $(PWD)/../../rtl

# Default overridable variables for multi-target CMake integration
TOPLEVEL ?= asp_top
MODULE ?= test_asp_tlp_64b_cocotb

ifeq ($(TOPLEVEL), asp_spi_frontend)
    VERILOG_SOURCES = $(RTL_DIR)/spi/asp_spi_frontend.sv
else ifeq ($(TOPLEVEL), asp_router)
    VERILOG_SOURCES = $(RTL_DIR)/asp_router.sv
else ifeq ($(TOPLEVEL), asp_wishbone_master)
    VERILOG_SOURCES = $(RTL_DIR)/asp_wishbone_master.sv
else ifeq ($(TOPLEVEL), asp_imu_auto_dma)
    VERILOG_SOURCES = $(RTL_DIR)/imu/asp_imu_auto_dma.sv
else ifeq ($(TOPLEVEL), asp_top)
    VERILOG_SOURCES = $(RTL_DIR)/asp_top.sv \
                      $(RTL_DIR)/spi/asp_spi_frontend.sv \
                      $(RTL_DIR)/asp_router.sv \
                      $(RTL_DIR)/asp_wishbone_master.sv \
                      $(RTL_DIR)/imu/asp_imu_auto_dma.sv
endif

EXTRA_ARGS += --trace-fst --trace-structs

include $(shell cocotb-config --makefiles)/Makefile.sim
