# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-AC7020C-05] hw/zynq7000/alinx_ac7020c/SPECIFICATION.md

# AC7020C PL user LED LED2, active low.
set_property PACKAGE_PIN R19 [get_ports {pl_led_tri_o[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pl_led_tri_o[0]}]
