#!/usr/bin/env bash
set -euo pipefail

RTL=experiments/rtl/cap_threshold_compiled_query_engine.sv
OUT=experiments/results/compiled_cap_synthesis
mkdir -p "$OUT"

for top in cap_streamed_normal_top cap_compiled_s45_top cap_compiled_s48_top; do
  echo "=== $top ==="
  yosys -Q -p "read_verilog -sv $RTL; hierarchy -check -top $top; synth_xilinx -family xc7 -top $top; check -assert; stat -tech xilinx; write_json $OUT/$top.json" \
    2>&1 | tee "$OUT/$top.log"
done

python3 - <<'PY'
import csv
import pathlib
import re

out = pathlib.Path('experiments/results/compiled_cap_synthesis')
rows = []
for log in sorted(out.glob('*.log')):
    text = log.read_text(errors='replace')
    top = log.stem

    def count(name):
        values = re.findall(
            rf'^\s*{re.escape(name)}\s+(\d+)\s*$',
            text,
            flags=re.MULTILINE,
        )
        return int(values[-1]) if values else 0

    lut = sum(count(name) for name in (
        'LUT1', 'LUT2', 'LUT3', 'LUT4', 'LUT5', 'LUT6'
    ))
    ff = sum(count(name) for name in (
        'FDRE', 'FDSE', 'FDCE', 'FDPE'
    ))
    ramb18 = count('RAMB18E1')
    ramb36 = count('RAMB36E1')
    dsp = count('DSP48E1')
    rows.append({
        'top': top,
        'lut_primitives': lut,
        'ffs': ff,
        'ramb18': ramb18,
        'ramb36': ramb36,
        'ramb18_equiv': ramb18 + 2 * ramb36,
        'dsp': dsp,
    })

with (out / 'summary.csv').open('w', newline='') as handle:
    writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)

print('\nSYNTHESIS SUMMARY')
for row in rows:
    print(row)
PY
