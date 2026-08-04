#!/usr/bin/env bash
set -euo pipefail

TOP=${1:?usage: synth_corrected_cap.sh <top>}
case "$TOP" in
  cap_nonblocking_normal_top|cap_nonblocking_s45_top) ;;
  *) echo "unsupported corrected CAP top: $TOP" >&2; exit 2 ;;
esac

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT"
OUT="experiments/results/corrected_cap_synthesis/$TOP"
mkdir -p "$OUT"

cat > "$OUT/synth.ys" <<YOSYS
read_verilog -sv -I experiments/rtl/corrected experiments/rtl/corrected/cap_nonblocking_corrected_decoder.sv
hierarchy -check -top $TOP
check -assert
synth_xilinx -family xc7 -top $TOP
flatten
opt_clean -purge
check -assert
tee -o $OUT/stat.json stat -json
tee -o $OUT/stat_xilinx.txt stat -tech xilinx
write_json $OUT/netlist.json
write_verilog -noattr $OUT/netlist.v
YOSYS

yosys -ql "$OUT/yosys.log" "$OUT/synth.ys"

grep -E '^(===|   Number of|Estimated number of LCs:|Warnings:|Found and reported)' \
  "$OUT/yosys.log" > "$OUT/summary.txt" || true
printf 'top=%s\n' "$TOP" >> "$OUT/summary.txt"
printf 'yosys_version=%s\n' "$(yosys -V | head -n1)" >> "$OUT/summary.txt"
