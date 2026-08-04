#!/usr/bin/env bash
set -euo pipefail

TOP=${1:?usage: run_postsynth_sim.sh <top> <synthesis-dir> <trace-hex>}
SYNTH_DIR=$(realpath "${2:?missing synthesis directory}")
TRACE_HEX=$(realpath "${3:?missing trace hex}")
case "$TOP" in
  cap_r2_normal_top|cap_r2_split_normal_top) ;;
  *) echo "unsupported frozen R2 top: $TOP" >&2; exit 2 ;;
esac

ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd)
TB="$ROOT/experiments/rtl/corrected/r2/physical/post_route_tb.sv"
NETLIST="$SYNTH_DIR/post_synth_funcsim.v"
SIM_DIR="$SYNTH_DIR/postsynth_sim"
mkdir -p "$SIM_DIR"
cd "$SIM_DIR"

: "${XILINX_VIVADO:?XILINX_VIVADO must identify the selected Vivado installation}"
GLBL="$XILINX_VIVADO/data/verilog/src/glbl.v"

xvlog --sv -d "DUT_MODULE=$TOP" "$NETLIST" "$TB" | tee xvlog.log
xvlog "$GLBL" | tee -a xvlog.log
xelab cap_r2_post_route_tb glbl -L unisims_ver -L secureip \
  --timescale 1ns/1ps -s r2_postsynth | tee xelab.log

cat > run_full.tcl <<TCL
open_saif $SIM_DIR/activity_1000.saif
log_saif [get_objects -r /cap_r2_post_route_tb/dut/*]
run all
close_saif
quit
TCL

xsim r2_postsynth \
  -testplusarg "TRACE=$TRACE_HEX" \
  -testplusarg "START=0" \
  -testplusarg "COUNT=1000" \
  -testplusarg "CSV=$SIM_DIR/postsynth_1000.csv" \
  -tclbatch run_full.tcl | tee postsynth_1000.log

grep -q '^POST_ROUTE_REPLAY_PASS start=0 count=1000 errors=0$' postsynth_1000.log
test -s activity_1000.saif
printf 'R2_POSTSYNTH_SIM_PASS top=%s saif=%s\n' "$TOP" "$SIM_DIR/activity_1000.saif"
