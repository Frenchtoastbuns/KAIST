proc required_env {name} {
    if {![info exists ::env($name)] || $::env($name) eq ""} {
        error "required environment variable $name is not set"
    }
    return $::env($name)
}

set checkpoint [file normalize [required_env R2_ROUTED_DCP]]
set saif_file [file normalize [required_env R2_SAIF]]
set out_dir [file normalize [required_env R2_OUT_DIR]]
file mkdir $out_dir

open_checkpoint $checkpoint
if {[llength [get_clocks clk]] != 1} {
    error "routed checkpoint does not contain the frozen clk constraint"
}

# The testbench instance is cap_r2_post_route_tb/dut. The strip path maps
# activity onto the routed design top. Failure to annotate is fatal; this gate
# does not fall back to vectorless power.
read_saif -input $saif_file -strip_path cap_r2_post_route_tb/dut
report_switching_activity -file [file join $out_dir switching_activity.rpt]
report_power -verbose -file [file join $out_dir power_saif.rpt]

puts "R2_SAIF_POWER_PASS checkpoint=$checkpoint saif=$saif_file"
