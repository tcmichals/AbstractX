# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-AC7020C-01] hw/zynq7000/alinx_ac7020c/SPECIFICATION.md
# @impl [SPEC-AC7020C-06] hw/zynq7000/alinx_ac7020c/SPECIFICATION.md

set script_dir [file dirname [file normalize [info script]]]
set target_dir [file normalize [file join $script_dir ..]]
set project_name "ac7020c_base"
set project_dir [file join $target_dir build $project_name]
set project_file [file join $project_dir ${project_name}.xpr]
set output_dir [file join $target_dir output]
set bit_file [file join $project_dir ${project_name}.runs impl_1 system_wrapper.bit]

if {![file exists $project_file]} {
    error "Project not found: $project_file. Run create_project.tcl first."
}

file mkdir $output_dir
open_project $project_file
update_compile_order -fileset sources_1
reset_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1

if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
    error "Implementation failed; inspect the Vivado run log."
}
if {![file exists $bit_file]} {
    error "Bitstream not found after implementation: $bit_file"
}

open_run impl_1
write_hw_platform -fixed -include_bit -force -file [file join $output_dir ac7020c_base.xsa]
file copy -force $bit_file [file join $output_dir ac7020c_base.bit]
puts "Generated [file join $output_dir ac7020c_base.bit]"
puts "Generated [file join $output_dir ac7020c_base.xsa]"
