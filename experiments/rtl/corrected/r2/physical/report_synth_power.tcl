proc required_env {name} {
    if {![info exists ::env($name)] || $::env($name) eq ""} {
        error "required environment variable $name is not set"
    }
    return $::env($name)
}

set checkpoint [file normalize [required_env R2_SYNTH_DCP]]
set saif_file [file normalize [required_env R2_SAIF]]
set out_dir [file normalize [required_env R2_OUT_DIR]]
file mkdir $out_dir

open_checkpoint $checkpoint
if {[llength [get_clocks clk]] != 1} {
    error "synthesis checkpoint does not contain the clk constraint"
}

read_saif -input $saif_file -strip_path cap_r2_post_route_tb/dut
report_switching_activity -file [file join $out_dir switching_activity_post_synth.rpt]
report_power -verbose -file [file join $out_dir power_saif_post_synth.rpt]

puts "R2_SYNTH_SAIF_POWER_PASS checkpoint=$checkpoint saif=$saif_file"
