#!/usr/bin/env bash
set -euo pipefail
RTL=experiments/rtl/cap_full_compiled_decoder.sv
OUT=experiments/results/full_compiled_cap_synthesis
mkdir -p "$OUT"
for top in cap_full_normal_top cap_full_s45_top cap_full_s48_top; do
  echo "=== $top ==="
  yosys -Q -p "read_verilog -sv $RTL; hierarchy -check -top $top; synth_xilinx -family xc7 -top $top; check -assert; stat -tech xilinx; write_json $OUT/$top.json" \
    2>&1 | tee "$OUT/$top.log"
done
python3 - <<'PY'
import csv, pathlib, re
out=pathlib.Path('experiments/results/full_compiled_cap_synthesis')
rows=[]
for log in sorted(out.glob('*.log')):
 text=log.read_text(errors='replace')
 def count(name):
  vals=re.findall(rf'^\s*{re.escape(name)}\s+(\d+)\s*$',text,flags=re.M)
  return int(vals[-1]) if vals else 0
 lut=sum(count(f'LUT{i}') for i in range(1,7))
 ff=sum(count(x) for x in ['FDRE','FDSE','FDCE','FDPE'])
 r18=count('RAMB18E1');r36=count('RAMB36E1');dsp=count('DSP48E1')
 rows.append(dict(top=log.stem,lut_primitives=lut,ffs=ff,ramb18=r18,ramb36=r36,
                  ramb18_equiv=r18+2*r36,dsp=dsp))
with (out/'summary.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
print(rows)
PY
