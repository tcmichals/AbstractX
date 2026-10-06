# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-AC7020C-01] hw/zynq7000/alinx_ac7020c/SPECIFICATION.md

set script_dir [file dirname [file normalize [info script]]]
set target_dir [file normalize [file join $script_dir ..]]
set project_name "ac7020c_base"
set project_dir [file join $target_dir build $project_name]

create_project -force $project_name $project_dir -part xc7z020clg400-2
set_property target_language Verilog [current_project]
source [file join $script_dir system_bd.tcl]

set wrapper_files [make_wrapper -files [get_files system.bd] -top]
add_files -norecurse $wrapper_files
set_property top system_wrapper [current_fileset]
add_files -fileset constrs_1 -norecurse [file join $target_dir constraints ac7020c.xdc]
update_compile_order -fileset sources_1

puts "Created [file join $project_dir ${project_name}.xpr]"
