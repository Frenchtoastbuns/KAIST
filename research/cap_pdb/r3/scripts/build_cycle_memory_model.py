#!/usr/bin/env python3

import argparse
from pathlib import Path

import numpy as np
import pandas as pd

parser = argparse.ArgumentParser(description="Build the R3 cycle and memory model")
parser.add_argument(
    "--results-dir",
    type=Path,
    default=Path(__file__).resolve().parents[1] / "results",
)
args = parser.parse_args()
base=args.results_dir.resolve()
variants=['specialized','fused_specialized','fused_w1','fused_w1w2']
dfs={v:pd.read_csv(base/f'{v}_1000.csv') for v in variants}
rows=[]
for group_lanes in [1,4,8,13]:
  for score_latency in [1,2,4,8,16,32,64]:
    scorer_lanes=4
    vals={}
    for v,d in dfs.items():
      exact_checks=(d.scoring_calls+d.dual_witness_candidates+d.oracle_candidates).to_numpy(float)
      cycles=(np.ceil(d.dual_solver_ops.to_numpy(float)/group_lanes)
              +np.ceil(d.compact_lookups.to_numpy(float)/group_lanes)
              +np.ceil(exact_checks*score_latency/scorer_lanes)
              +d.bound_checks.to_numpy(float))
      vals[v]=cycles
      rows.append(dict(variant=v,group_lanes=group_lanes,scorer_lanes=scorer_lanes,
                       exact_score_latency_cycles=score_latency,
                       mean_cycles=cycles.mean(),p95_cycles=np.quantile(cycles,.95),p99_cycles=np.quantile(cycles,.99),
                       raw_pair_signature_bytes=16128 if v=='fused_w1w2' else 0,
                       minimum_extra_ramb18=8 if v=='fused_w1w2' else 0))
model=pd.DataFrame(rows)
model.to_csv(base/'r3_cycle_memory_sensitivity.csv',index=False)
# Comparison gates for fused_w1 and fused_w1w2 vs fused_specialized.
comp=[]
for group_lanes in [1,4,8,13]:
  for latency in [1,2,4,8,16,32,64]:
    sub=model[(model.group_lanes==group_lanes)&(model.exact_score_latency_cycles==latency)].set_index('variant')
    b=sub.loc['fused_specialized']
    for v in ['fused_w1','fused_w1w2']:
      x=sub.loc[v]
      cycle_gain=1-x.mean_cycles/b.mean_cycles
      for base_bram in [41,150]:
        variant_bram=base_bram+(8 if v=='fused_w1w2' else 0)
        throughput_per_bram_gain=(b.mean_cycles/x.mean_cycles)*(base_bram/variant_bram)-1
        comp.append(dict(variant=v,group_lanes=group_lanes,score_latency=latency,
                         base_bram18=base_bram,variant_bram18=variant_bram,
                         mean_cycle_reduction_percent=100*cycle_gain,
                         throughput_per_bram_gain_percent=100*throughput_per_bram_gain))
comp=pd.DataFrame(comp)
comp.to_csv(base/'r3_oracle_gate_sensitivity.csv',index=False)
print(comp[(comp.variant=='fused_w1w2')&(comp.group_lanes==13)].to_string(index=False))
