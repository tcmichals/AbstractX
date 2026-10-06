# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later

# AbstractX Physical Constraints for QMTECH Zynq-7020 Starter Kit
# Board: QMTECH XC7Z020 Starter Kit (XC7Z020-1CLG484C / CLG400 compatible)
#
# @impl [SPEC-ZYNQ-05] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-05

# -----------------------------------------------------------------------------
# Timing Constraints (100 MHz PS AXI Fabric Clock)
# -----------------------------------------------------------------------------
create_clock -period 10.000 -name s_axi_aclk [get_ports s_axi_aclk]

# -----------------------------------------------------------------------------
# Onboard User LEDs & Pushbuttons (Bank 34, 3.3V LVCMOS)
# -----------------------------------------------------------------------------
# Carrier Board User LED D3
set_property PACKAGE_PIN P22 [get_ports o_led_carrier]
set_property IOSTANDARD LVCMOS33 [get_ports o_led_carrier]

# Core Board User LED D2
set_property PACKAGE_PIN M14 [get_ports o_led_core]
set_property IOSTANDARD LVCMOS33 [get_ports o_led_core]

# Carrier Board User Key (SW)
set_property PACKAGE_PIN P16 [get_ports i_key_carrier]
set_property IOSTANDARD LVCMOS33 [get_ports i_key_carrier]

# -----------------------------------------------------------------------------
# High-Rate Primary IMU SPI & DRDY (JP5 PMOD / Extension Header - Bank 34)
# -----------------------------------------------------------------------------
set_property PACKAGE_PIN L22 [get_ports o_imu_sclk]
set_property IOSTANDARD LVCMOS33 [get_ports o_imu_sclk]

set_property PACKAGE_PIN L21 [get_ports o_imu_cs_n]
set_property IOSTANDARD LVCMOS33 [get_ports o_imu_cs_n]

set_property PACKAGE_PIN K20 [get_ports o_imu_mosi]
set_property IOSTANDARD LVCMOS33 [get_ports o_imu_mosi]

set_property PACKAGE_PIN K19 [get_ports i_imu_miso]
set_property IOSTANDARD LVCMOS33 [get_ports i_imu_miso]

set_property PACKAGE_PIN J22 [get_ports i_imu_int]
set_property IOSTANDARD LVCMOS33 [get_ports i_imu_int]

# -----------------------------------------------------------------------------
# Motor Demands (DShot600 / DShot300 / PWM Channels 1..4 - Bank 34)
# -----------------------------------------------------------------------------
set_property PACKAGE_PIN J21 [get_ports {o_motor_pins[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {o_motor_pins[0]}]

set_property PACKAGE_PIN K18 [get_ports {o_motor_pins[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {o_motor_pins[1]}]

set_property PACKAGE_PIN J18 [get_ports {o_motor_pins[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {o_motor_pins[2]}]

set_property PACKAGE_PIN J17 [get_ports {o_motor_pins[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {o_motor_pins[3]}]

# -----------------------------------------------------------------------------
# WS2812B NeoPixel Status LED Output (Bank 34)
# -----------------------------------------------------------------------------
set_property PACKAGE_PIN J16 [get_ports o_neopixel]
set_property IOSTANDARD LVCMOS33 [get_ports o_neopixel]

# -----------------------------------------------------------------------------
# Hardware Logic Analyzer Debug Pins (Bank 35)
# -----------------------------------------------------------------------------
set_property PACKAGE_PIN G22 [get_ports {o_debug_pins[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {o_debug_pins[0]}]

set_property PACKAGE_PIN H22 [get_ports {o_debug_pins[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {o_debug_pins[1]}]

set_property PACKAGE_PIN F22 [get_ports {o_debug_pins[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {o_debug_pins[2]}]

set_property PACKAGE_PIN F21 [get_ports {o_debug_pins[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {o_debug_pins[3]}]
