# SPDX-License-Identifier: GPL-3.0-or-later

VIVADO ?= $(HOME)/tools/Xilinx/2025.2/Vivado/bin/vivado
TARGET_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

.PHONY: all xpr xsa ps-init buildroot-setup clean-vivado

all: xsa

xpr:
	$(VIVADO) -mode batch -nojournal -nolog -source $(TARGET_DIR)/vivado/create_project.tcl

xsa: xpr
	$(VIVADO) -mode batch -nojournal -nolog -source $(TARGET_DIR)/vivado/build.tcl

ps-init: xsa
	$(TARGET_DIR)/tools/install_ps7_init.sh

buildroot-setup:
	$(TARGET_DIR)/tools/setup_workspace.sh

clean-vivado:
	rm -rf $(TARGET_DIR)/build $(TARGET_DIR)/output
