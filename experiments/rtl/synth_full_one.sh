#!/usr/bin/env bash
set -euo pipefail
TOP="${1:?top module required}"
RTL=experiments/rtl/cap_full_compiled_decoder.sv
OUT="experiments/results/full_compiled_cap_synthesis/${TOP}"
mkdir -p "$OUT"
yosys -Q -p "read_verilog -sv $RTL; hierarchy -check -top $TOP; synth_xilinx -family xc7 -top $TOP; check -assert; stat -tech xilinx; write_json $OUT/$TOP.json" \
  2>&1 | tee "$OUT/$TOP.log"
python3 - "$TOP" "$OUT" <<'PY'
import csv,pathlib,re,sys
name=sys.argv[1];out=pathlib.Path(sys.argv[2]);text=(out/f'{name}.log').read_text(errors='replace')
def count(cell):
 vals=re.findall(rf'^\s*{re.escape(cell)}\s+(\d+)\s*$',text,flags=re.M)
 return int(vals[-1]) if vals else 0
lut=sum(count(f'LUT{i}') for i in range(1,7))
ff=sum(count(x) for x in ['FDRE','FDSE','FDCE','FDPE'])
r18=count('RAMB18E1');r36=count('RAMB36E1');dsp=count('DSP48E1')
row=dict(top=name,lut_primitives=lut,ffs=ff,ramb18=r18,ramb36=r36,ramb18_equiv=r18+2*r36,dsp=dsp)
with (out/'summary.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=row.keys());w.writeheader();w.writerow(row)
print(row)
PY
