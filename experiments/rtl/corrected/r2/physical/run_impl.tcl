proc required_env {name} {
    if {![info exists ::env($name)] || $::env($name) eq ""} {
        error "required environment variable $name is not set"
    }
    return $::env($name)
}

set top [required_env R2_TOP]
set part [required_env R2_PART]
set period_ns [expr {double([required_env R2_PERIOD_NS])}]
set uncertainty_ns [expr {double([required_env R2_UNCERTAINTY_NS])}]
set out_dir [file normalize [required_env R2_OUT_DIR]]

if {$top ni {cap_r2_normal_top cap_r2_split_normal_top}} {
    error "unsupported frozen R2 top: $top"
}
if {$period_ns <= 0.0 || $uncertainty_ns < 0.0} {
    error "invalid clock constraint"
}

file mkdir $out_dir
set root [file normalize [file join [file dirname [info script]] ../../../../..]]
set baseline_rtl [file join $root experiments/rtl/corrected/r2/cap_r2_normal_decoder.sv]
set split_rtl [file join $root experiments/rtl/corrected/r2/cap_r2_split_phase_decoder.sv]

set_param general.maxThreads 8
read_verilog -sv $baseline_rtl
read_verilog -sv $split_rtl
synth_design -top $top -part $part -mode out_of_context -flatten_hierarchy rebuilt

create_clock -name clk -period $period_ns [get_ports clk]
set_clock_uncertainty $uncertainty_ns [get_clocks clk]

write_checkpoint -force [file join $out_dir post_synth.dcp]
report_utilization -hierarchical -file [file join $out_dir utilization_post_synth.rpt]
report_timing_summary -delay_type max -max_paths 20 -report_unconstrained \
    -file [file join $out_dir timing_post_synth.rpt]

opt_design -directive Explore
place_design -directive Explore
phys_opt_design -directive AggressiveExplore
route_design -directive Explore
phys_opt_design -post_route -directive AggressiveExplore

write_checkpoint -force [file join $out_dir routed.dcp]
write_verilog -force -mode funcsim [file join $out_dir routed_funcsim.v]
write_sdf -force [file join $out_dir routed.sdf]

report_timing_summary -delay_type max -max_paths 100 -report_unconstrained \
    -file [file join $out_dir timing_summary.rpt]
report_utilization -hierarchical -file [file join $out_dir utilization_routed.rpt]
report_route_status -file [file join $out_dir route_status.rpt]
report_drc -file [file join $out_dir drc.rpt]
report_clock_utilization -file [file join $out_dir clock_utilization.rpt]
report_power -file [file join $out_dir power_vectorless_diagnostic.rpt]

set paths [get_timing_paths -delay_type max -max_paths 1]
if {[llength $paths] != 1} {
    error "no constrained maximum-delay timing path found"
}
set wns [expr {double([get_property SLACK [lindex $paths 0]])}]
set critical_delay [expr {$period_ns - $wns}]
if {$critical_delay <= 0.0} {
    error "invalid critical delay derived from period and WNS"
}
set fmax_mhz [expr {1000.0 / $critical_delay}]

set tns 0.0
set failing_paths [get_timing_paths -delay_type max -max_paths 100000 -slack_lesser_than 0.0]
foreach path $failing_paths {
    set tns [expr {$tns + double([get_property SLACK $path])}]
}

set lut_count [llength [get_cells -hierarchical -filter {REF_NAME =~ LUT*}]]
set ff_count [llength [get_cells -hierarchical -filter {REF_NAME =~ FD*}]]
set ramb18_count [llength [get_cells -hierarchical -filter {REF_NAME =~ RAMB18*}]]
set ramb36_count [llength [get_cells -hierarchical -filter {REF_NAME =~ RAMB36*}]]
set dsp_count [llength [get_cells -hierarchical -filter {REF_NAME =~ DSP48*}]]
set bram18eq [expr {$ramb18_count + 2 * $ramb36_count}]
set vivado_version [version -short]

set json [open [file join $out_dir route_metrics.json] w]
puts $json "{"
puts $json "  \"top\": \"$top\","
puts $json "  \"part\": \"$part\","
puts $json "  \"vivado_version\": \"$vivado_version\","
puts $json "  \"target_period_ns\": $period_ns,"
puts $json "  \"clock_uncertainty_ns\": $uncertainty_ns,"
puts $json "  \"wns_ns\": $wns,"
puts $json "  \"tns_ns\": $tns,"
puts $json "  \"critical_delay_ns\": $critical_delay,"
puts $json "  \"estimated_fmax_mhz\": $fmax_mhz,"
puts $json "  \"routed_luts\": $lut_count,"
puts $json "  \"routed_ffs\": $ff_count,"
puts $json "  \"ramb18\": $ramb18_count,"
puts $json "  \"ramb36\": $ramb36_count,"
puts $json "  \"bram18_equivalent\": $bram18eq,"
puts $json "  \"dsp48\": $dsp_count"
puts $json "}"
close $json

puts "R2_IMPLEMENTATION_PASS top=$top part=$part period_ns=$period_ns wns_ns=$wns tns_ns=$tns fmax_mhz=$fmax_mhz bram18eq=$bram18eq"
