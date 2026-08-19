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
else ifeq ($(TOPLEVEL), asp_sys_regs)
    VERILOG_SOURCES = $(RTL_DIR)/asp_sys_regs.sv
else ifeq ($(TOPLEVEL), asp_axis_fifo)
    VERILOG_SOURCES = $(RTL_DIR)/asp_axis_fifo.sv
else ifeq ($(TOPLEVEL), asp_wishbone_master)
    VERILOG_SOURCES = $(RTL_DIR)/asp_wishbone_master.sv
else ifeq ($(TOPLEVEL), asp_imu_auto_dma)
    VERILOG_SOURCES = $(RTL_DIR)/imu/asp_imu_auto_dma.sv
else ifeq ($(TOPLEVEL), asp_dshot_core)
    VERILOG_SOURCES = $(RTL_DIR)/motor/asp_dshot_core.sv
else ifeq ($(TOPLEVEL), asp_pwm_decoder)
    VERILOG_SOURCES = $(RTL_DIR)/pwm/asp_pwm_decoder.sv
else ifeq ($(TOPLEVEL), asp_neopixel_core)
    VERILOG_SOURCES = $(RTL_DIR)/led/asp_neopixel_core.sv
else ifeq ($(TOPLEVEL), asp_top)
    VERILOG_SOURCES = $(RTL_DIR)/asp_top.sv \
                      $(RTL_DIR)/asp_axis_fifo.sv \
                      $(RTL_DIR)/asp_router.sv \
                      $(RTL_DIR)/asp_sys_regs.sv \
                      $(RTL_DIR)/asp_wishbone_master.sv \
                      $(RTL_DIR)/spi/asp_spi_frontend.sv \
                      $(RTL_DIR)/imu/asp_imu_auto_dma.sv \
                      $(RTL_DIR)/motor/asp_dshot_core.sv \
                      $(RTL_DIR)/pwm/asp_pwm_decoder.sv \
                      $(RTL_DIR)/led/asp_neopixel_core.sv
endif

EXTRA_ARGS += --trace-fst --trace-structs -I$(RTL_DIR) -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Wno-SELRANGE

include $(shell cocotb-config --makefiles)/Makefile.sim

