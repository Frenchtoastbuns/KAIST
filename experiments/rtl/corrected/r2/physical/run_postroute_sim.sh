#!/usr/bin/env bash
set -euo pipefail

TOP=${1:?usage: run_postroute_sim.sh <top> <implementation-dir> <trace-hex>}
IMPL_DIR=$(realpath "${2:?missing implementation directory}")
TRACE_HEX=$(realpath "${3:?missing trace hex}")
case "$TOP" in
  cap_r2_normal_top|cap_r2_split_normal_top) ;;
  *) echo "unsupported frozen R2 top: $TOP" >&2; exit 2 ;;
esac

ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd)
TB="$ROOT/experiments/rtl/corrected/r2/physical/post_route_tb.sv"
NETLIST="$IMPL_DIR/routed_funcsim.v"
SIM_DIR="$IMPL_DIR/postroute_sim"
mkdir -p "$SIM_DIR"
cd "$SIM_DIR"

: "${XILINX_VIVADO:?XILINX_VIVADO must identify the selected Vivado installation}"
GLBL="$XILINX_VIVADO/data/verilog/src/glbl.v"

xvlog --sv -d "DUT_MODULE=$TOP" "$NETLIST" "$TB" | tee xvlog.log
xvlog "$GLBL" | tee -a xvlog.log
xelab cap_r2_post_route_tb glbl -L unisims_ver -L secureip \
  --timescale 1ns/1ps -s r2_postroute | tee xelab.log

cat > run_full.tcl <<'TCL'
run all
quit
TCL
xsim r2_postroute \
  -testplusarg "TRACE=$TRACE_HEX" \
  -testplusarg "START=0" \
  -testplusarg "COUNT=1000" \
  -testplusarg "CSV=$SIM_DIR/postroute_1000.csv" \
  -tclbatch run_full.tcl | tee postroute_1000.log
grep -q '^POST_ROUTE_REPLAY_PASS start=0 count=1000 errors=0$' postroute_1000.log

# Use the first 100 frozen frames for activity annotation. Functional
# equivalence still covers all 1,000 frames above.
cat > run_power.tcl <<TCL
open_saif $SIM_DIR/activity_100.saif
log_saif [get_objects -r /cap_r2_post_route_tb/dut/*]
run all
close_saif
quit
TCL
xsim r2_postroute \
  -testplusarg "TRACE=$TRACE_HEX" \
  -testplusarg "START=0" \
  -testplusarg "COUNT=100" \
  -testplusarg "CSV=$SIM_DIR/postroute_power_100.csv" \
  -tclbatch run_power.tcl | tee postroute_power_100.log
grep -q '^POST_ROUTE_REPLAY_PASS start=0 count=100 errors=0$' postroute_power_100.log
test -s activity_100.saif

printf 'POST_ROUTE_SIM_PASS top=%s\n' "$TOP"
