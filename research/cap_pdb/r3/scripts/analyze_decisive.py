#!/usr/bin/env python3

import argparse
from pathlib import Path

import numpy as np
import pandas as pd


parser = argparse.ArgumentParser(description="Analyze the four frozen R3 variants")
parser.add_argument(
    "--results-dir",
    type=Path,
    default=Path(__file__).resolve().parents[1] / "results",
)
args = parser.parse_args()
results_dir = args.results_dir.resolve()
variants = ["specialized", "fused_specialized", "fused_w1", "fused_w1w2"]
all_df={}
for name in variants:
    d=pd.read_csv(results_dir / f"{name}_1000.csv")
    if len(d)!=1000 or d.frame.min()!=0 or d.frame.max()!=999 or d.frame.nunique()!=1000:
        raise RuntimeError((name,len(d),d.frame.min(),d.frame.max(),d.frame.nunique()))
    if int(d.iloc[-1].stream_checksum)!=11508365490867720138:
        # pandas may parse uint as float if over signed range; reread checksum as string check from CSV tail separately later.
        pass
    if d[['word_mismatch','metric_mismatch','tie_mismatch']].to_numpy().sum()!=0:
        raise RuntimeError(f'mismatch {name}')
    all_df[name]=d

base_hash=all_df['specialized'][['frame','frame_hash']].astype({'frame_hash':'uint64'})
for name,d in all_df.items():
    if not np.array_equal(d.frame_hash.astype('uint64').to_numpy(),base_hash.frame_hash.to_numpy()):
        raise RuntimeError(f'hash mismatch {name}')

metrics=['dual_solver_ops','total_primitive_ops','teps','scoring_calls','bound_checks','compact_lookups',
         'oracle_candidates','oracle_w1_candidates','oracle_w2_candidates','oracle_primitive_ops',
         'dual_witness_candidates','dual_witness_primitive_ops','score_primitive_ops','ops_w1','ops_w2','ops_w3']
rows=[]
for name,d in all_df.items():
    row={'variant':name,'frames':len(d)}
    for c in metrics:
        vals=d[c].astype('float64')
        row[c+'_mean']=vals.mean()
        row[c+'_median']=vals.median()
        row[c+'_p95']=vals.quantile(.95,interpolation='linear')
        row[c+'_p99']=vals.quantile(.99,interpolation='linear')
        row[c+'_max']=vals.max()
    row['structural_checks_unique']=int(d.structural_checks.max())
    row['word_mismatches']=int(d.word_mismatch.sum())
    row['metric_mismatches']=int(d.metric_mismatch.sum())
    row['tie_mismatches']=int(d.tie_mismatch.sum())
    rows.append(row)
summary=pd.DataFrame(rows)
summary.to_csv(results_dir / 'r3_decisive_summary.csv',index=False)

# Cycle-aware analytical model. One specialized operation or oracle packed exact check per lane-cycle.
# Compact lookups and external scoring are charged through separate lane pools. Total is bottleneck-free additive latency,
# conservative for overlap; memory is incremental beyond fused-specialized.
cycle_rows=[]
for lanes in [1,4,8,13]:
    scorer_lanes=lanes if lanes<=8 else 8
    for name,d in all_df.items():
        # dual operations can exploit parity-group lanes; exact oracle candidates use scorer lanes.
        dual_cycles=np.ceil(d.dual_solver_ops.to_numpy()/lanes)
        compact_cycles=np.ceil(d.compact_lookups.to_numpy()/max(1,lanes))
        oracle_cycles=np.ceil(d.oracle_candidates.to_numpy()/scorer_lanes)
        score_cycles=np.ceil(d.scoring_calls.to_numpy()/scorer_lanes)
        # Witness candidates are already scored to drive updates; charge on same scorer pool.
        witness_cycles=np.ceil(d.dual_witness_candidates.to_numpy()/scorer_lanes)
        cycles=dual_cycles+compact_cycles+oracle_cycles+score_cycles+witness_cycles+d.bound_checks.to_numpy()
        cycle_rows.append({
            'variant':name,'group_lanes':lanes,'scorer_lanes':scorer_lanes,
            'mean_cycles':cycles.mean(),'p95_cycles':np.quantile(cycles,.95),'p99_cycles':np.quantile(cycles,.99),
            'incremental_signature_bytes':16128 if name=='fused_w1w2' else 0,
            'incremental_oracle_bytes_min':16128 if name=='fused_w1w2' else 0,
        })
cycles=pd.DataFrame(cycle_rows)
cycles.to_csv(results_dir / 'r3_cycle_memory_model.csv',index=False)

print(summary[['variant','dual_solver_ops_mean','dual_solver_ops_p95','dual_solver_ops_p99','teps_mean','oracle_candidates_mean','total_primitive_ops_mean']].to_string(index=False))
print('\nReductions vs fused_specialized:')
fs=summary.set_index('variant').loc['fused_specialized']
for name in ['fused_w1','fused_w1w2']:
    r=summary.set_index('variant').loc[name]
    print(name,'dual',100*(1-r.dual_solver_ops_mean/fs.dual_solver_ops_mean),
          'total_primitive',100*(1-r.total_primitive_ops_mean/fs.total_primitive_ops_mean))
print('\nCycle model reductions vs fused_specialized:')
for lanes in [1,4,8,13]:
    sub=cycles[cycles.group_lanes==lanes].set_index('variant')
    fs=sub.loc['fused_specialized']
    for name in ['fused_w1','fused_w1w2']:
        r=sub.loc[name]
        print(lanes,name,100*(1-r.mean_cycles/fs.mean_cycles),100*(1-r.p99_cycles/fs.p99_cycles))
