#!/usr/bin/env python3
import argparse,csv,glob,json,math,statistics
from pathlib import Path

# Fresh post-FIFO-fix Xilinx-7 synthesis counts from run 30691594871.
NORMAL={'estimated_lcs':34944,'luts':39124,'ffs':15473,'bram18eq':329,'dsp48':3}
S45={'estimated_lcs':38584,'luts':44070,'ffs':15788,'bram18eq':251,'dsp48':3}

def q(values,p):
    v=sorted(values)
    if not v:return 0.0
    x=(len(v)-1)*p;i=int(math.floor(x));j=int(math.ceil(x))
    return float(v[i] if i==j else v[i]+(v[j]-v[i])*(x-i))

def stats(values):
    return {'mean':statistics.fmean(values),'p50':q(values,.5),'p95':q(values,.95),'p99':q(values,.99),'min':min(values),'max':max(values)}

def ints(rows,name):return [int(r[name],0) for r in rows]
def floats(rows,name):return [float(r[name]) for r in rows]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--glob',required=True);ap.add_argument('--out',required=True);args=ap.parse_args()
    rows=[]
    for path in sorted(glob.glob(args.glob)):
        with open(path,newline='') as f:rows.extend(csv.DictReader(f))
    rows.sort(key=lambda r:int(r['frame']))
    frames=[int(r['frame']) for r in rows]
    if frames!=list(range(1000)):raise SystemExit(f'frame coverage mismatch: {len(rows)} rows')
    mismatch=sum(int(r['errors'])!=0 for r in rows)
    if mismatch:raise SystemExit(f'{mismatch} mismatch frames')
    out=Path(args.out);out.mkdir(parents=True,exist_ok=True)
    with (out/'canonical_replay_per_frame.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    result={'frames':1000,'mismatch_frames':mismatch,'tie_frames':sum(int(r['expected_tie']) for r in rows),'resources':{'normal':NORMAL,'s45':S45,'s45_normalized_to_normal':{k:S45[k]/NORMAL[k] for k in NORMAL}}}
    for arch in ('normal','s45'):
        build=ints(rows,f'{arch}_build_cycles');decode=ints(rows,f'{arch}_decode_cycles');total=ints(rows,f'{arch}_total_cycles');issues=ints(rows,f'{arch}_issues')
        result[arch]={
          'build_cycles':stats(build),'decode_cycles':stats(decode),'total_cycles':stats(total),
          'scoring_issues':stats(issues),'bound_rows':stats(ints(rows,f'{arch}_bound_rows')),
          'prune_rate':stats(floats(rows,f'{arch}_prune_rate')),
          'scorer_utilisation_weighted':sum(issues)/(4.0*sum(decode)),
          'scorer_utilisation_per_frame':stats(floats(rows,f'{arch}_utilisation')),
          'context_wait_cycles':stats(ints(rows,f'{arch}_context_wait')),
          'fifo_max_occupancy':stats(ints(rows,f'{arch}_fifo_max')),
          'fifo_full_cycles':stats(ints(rows,f'{arch}_fifo_full_cycles')),
          'fifo_empty_cycles':stats(ints(rows,f'{arch}_fifo_empty_cycles')),
          'max_bound_waiters':stats(ints(rows,f'{arch}_max_bound_waiters')),
          'build_phases':{
             'normal_dp':stats(ints(rows,f'{arch}_build_normal_dp')),
             'right_dp':stats(ints(rows,f'{arch}_build_right_dp')),
             'expansion':stats(ints(rows,f'{arch}_build_expansion')),
             'triangular_dp':stats(ints(rows,f'{arch}_build_triangular')),
             'prefix':stats(ints(rows,f'{arch}_build_prefix')),
          },
          'cycles_x_luts':stats(ints(rows,f'{arch}_cycles_x_luts')),
        }
    nmean=result['normal']['total_cycles']['mean'];smean=result['s45']['total_cycles']['mean'];ratio=smean/nmean;delta=(smean/nmean-1)*100
    decode_gain=result['normal']['decode_cycles']['mean']-result['s45']['decode_cycles']['mean']
    build_target=result['normal']['build_cycles']['mean']+decode_gain
    hardest=sorted(rows,key=lambda r:int(r['normal_total_cycles']),reverse=True)[:100]
    hard_n=statistics.fmean(int(r['normal_total_cycles']) for r in hardest);hard_s=statistics.fmean(int(r['s45_total_cycles']) for r in hardest)
    result['comparison']={
      's45_over_normal_total_cycle_ratio':ratio,'s45_total_cycle_change_percent':delta,
      's45_frame_win_rate':sum(int(r['s45_total_cycles'])<int(r['normal_total_cycles']) for r in rows)/1000,
      's45_decode_cycle_change_percent':(result['s45']['decode_cycles']['mean']/result['normal']['decode_cycles']['mean']-1)*100,
      's45_build_cycle_change_percent':(result['s45']['build_cycles']['mean']/result['normal']['build_cycles']['mean']-1)*100,
      'hardest_10_percent':{'normal_mean_total_cycles':hard_n,'s45_mean_total_cycles':hard_s,'s45_change_percent':(hard_s/hard_n-1)*100},
      's45_break_even_build_target':build_target,'s45_current_build_mean':result['s45']['build_cycles']['mean'],
      's45_builder_reduction_required':max(0.0,result['s45']['build_cycles']['mean']-build_target),
      'cycles_x_luts_ratio':result['s45']['cycles_x_luts']['mean']/result['normal']['cycles_x_luts']['mean'],
    }
    if ratio<.95:decision='RETAIN_S45_PERFORMANCE_CANDIDATE'
    elif ratio<=1.05:decision='NORMAL_DEFAULT_S45_BRAM_PARETO'
    else:decision='KILL_S45_ACTIVE_PERFORMANCE_ARCHITECTURE'
    result['decision']=decision
    (out/'canonical_replay_summary.json').write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
    md=[]
    md+=['# Canonical saved-trace RTL replay','',f'**Decision:** `{decision}`','',f'- Frames: 1,000; mismatches: {mismatch}; tie frames: {result["tie_frames"]}',f'- S45 total-cycle change: {delta:+.3f}%',f'- S45 frame win rate: {100*result["comparison"]["s45_frame_win_rate"]:.1f}%',f'- Hardest-10% total-cycle change: {result["comparison"]["hardest_10_percent"]["s45_change_percent"]:+.3f}%','', '| Metric | Normal | S45 |','|---|---:|---:|']
    for label,key in [('Build mean','build_cycles'),('Decode mean','decode_cycles'),('Total mean','total_cycles')]:md.append(f'| {label} | {result["normal"][key]["mean"]:.3f} | {result["s45"][key]["mean"]:.3f} |')
    for label,p in [('Total p50','p50'),('Total p95','p95'),('Total p99','p99')]:md.append(f'| {label} | {result["normal"]["total_cycles"][p]:.3f} | {result["s45"]["total_cycles"][p]:.3f} |')
    md+=['',f'- Weighted scorer utilisation: normal {100*result["normal"]["scorer_utilisation_weighted"]:.3f}%, S45 {100*result["s45"]["scorer_utilisation_weighted"]:.3f}%',f'- Mean scoring issues: normal {result["normal"]["scoring_issues"]["mean"]:.3f}, S45 {result["s45"]["scoring_issues"]["mean"]:.3f}',f'- Mean prune rate: normal {100*result["normal"]["prune_rate"]["mean"]:.3f}%, S45 {100*result["s45"]["prune_rate"]["mean"]:.3f}%',f'- Cycles×LUT ratio S45/normal: {result["comparison"]["cycles_x_luts_ratio"]:.6f}',f'- S45 break-even build target: {build_target:.3f} cycles; required reduction: {result["comparison"]["s45_builder_reduction_required"]:.3f} cycles','', 'Per-frame results are preserved in `canonical_replay_per_frame.csv`.']
    (out/'canonical_replay_summary.md').write_text('\n'.join(md)+'\n')
    print(json.dumps(result['comparison'],indent=2,sort_keys=True));print('CANONICAL_REPLAY_AGGREGATE_PASS');print('DECISION='+decision)
if __name__=='__main__':main()
