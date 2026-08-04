# Bounded-stale CAP parallelism: algorithmic validation

**Date:** 30 July 2026
**Status:** 1,000-frame exact validation passed
**Configuration:** eBCH(128,64), OSD-4, BPSK-AWGN, 0 dB
**Seed:** `5928218492399464753`
**Frozen stream checksum:** `11508365490867720138`

## 1. Question

Can CAP-PDB reduce its sequential incumbent dependency by updating the
incumbent only once per batch of scored TEPs, accepting a small increase in
evaluated TEPs in exchange for much more parallel work?

The test covers:

1. normal CAP-PDB;
2. compact two-block CAP-PDB;
3. four-update dual-compact CAP-PDB, whose final bound is
   `max(compact, dual)`.

No bound equation, code, OSD order, seed procedure, channel sample, strict
pruning rule, or fixed-list objective was changed.

## 2. Algorithmic change

The original search updates its incumbent after every scored certification
candidate. The modified search keeps two incumbent views:

- a committed incumbent used for information-cost stopping and subtree
  pruning;
- a pending best/runner-up pair updated by candidates inside the current
  batch.

After `B` certification scoring calls, pending values are reduced into the
committed incumbent.

For `B=1`, the implementation is equivalent to the original immediate-update
search. For `B>1`, all pruning decisions in a batch use a deliberately stale
incumbent.

### Exactness

Let the current exact incumbent be `U` and the committed stale incumbent be
`\bar U`. Since minimization can only improve the incumbent,

`\bar U >= U`.

CAP prunes only when an admissible descendant lower bound `L` satisfies

`L > \bar U`.

Therefore any subtree pruned using `\bar U` also satisfies `L > U`. A stale
incumbent may preserve extra subtrees, but cannot create an unsafe prune.
Strict equality is retained, so tie certification is unchanged.

## 3. Canonical 1,000-frame result

The selected synchronization batch was `B=4096`.

| Decoder | Immediate-update TEPs/frame | Batch-4096 TEPs/frame | Added TEPs/frame | TEP overhead | Reduction vs classical | Batch syncs/frame |
|---|---:|---:|---:|---:|---:|---:|
| Normal CAP | 354,339.465 | **354,436.866** | 97.401 | **0.0275%** | 47.809% | 86.638 |
| Compact CAP | 286,642.377 | **286,815.344** | 172.967 | **0.0603%** | 57.767% | 70.028 |
| Dual-compact CAP | 119,889.055 | **120,231.949** | 342.894 | **0.2860%** | 82.296% | 28.877 |

Classical OSD evaluates 679,121 TEPs/frame.

### Hardware-relevant scoring calls

| Decoder | Immediate scoring calls/frame | Batch-4096 scoring calls/frame | Added calls/frame |
|---|---:|---:|---:|
| Normal CAP | 359,253.583 | 359,351.052 | 97.469 |
| Compact CAP | 291,145.797 | 291,318.811 | 173.014 |
| Dual-compact CAP | 122,419.347 | 122,762.403 | 343.056 |

### Bound work

| Decoder | Immediate bound work/frame | Batch-4096 bound work/frame | Change |
|---|---:|---:|---:|
| Normal CAP | 422,468.943 PDB reads | 422,511.596 PDB reads | +0.0101% |
| Compact CAP | 2,933,046.052 compact reads | 2,933,108.308 compact reads | +0.0021% |
| Dual-compact CAP | 30,297.998 dual queries | 30,311.315 dual queries | +0.0440% |
| Dual sparse solver | 145,698,268.443 operations | 145,707,721.666 operations | +0.0065% |

The recovered executable source uses the validated sparse/dense dual kernel.
The later 45.15M-operation cardinality-specialized solver was previously
proved decision-equivalent and therefore has the same TEP result, but its
batch-4096 operation count was not rerun here.

## 4. Exactness result

Across all 1,000 matched frames and all three CAP variants:

- decoded-word mismatches: 0;
- best-metric mismatches: 0;
- tie-status mismatches: 0;
- frame checksum mismatch: 0.

An additional three-frame AddressSanitizer and UndefinedBehaviorSanitizer run
passed with no reported memory or undefined-behavior error. LeakSanitizer was
disabled because the execution container blocks its `/proc` thread scan.

## 5. Batch-width screen

A separate 50-frame matched screen located the work/span knee.

| Batch | Normal TEP overhead | Compact TEP overhead | Dual-compact TEP overhead | Mean syncs/frame: normal / compact / dual |
|---:|---:|---:|---:|---:|
| 512 | 0.0013% | 0.0031% | 0.0311% | 676.0 / 551.3 / 226.6 |
| 1,024 | 0.0032% | 0.0090% | 0.0421% | 338.3 / 275.9 / 113.6 |
| 2,048 | 0.0053% | 0.0152% | 0.1150% | 169.5 / 138.2 / 57.0 |
| 4,096 | 0.0117% | 0.0377% | 0.2592% | 85.0 / 69.3 / 28.8 |
| 16,384 | 0.0632% | 0.1563% | 1.0149% | 21.6 / 17.7 / 7.7 |
| One batch/frame | 5.8059% | 10.6403% | 22.5013% | 1 / 1 / 1 |

The exact 1,000-frame overhead is slightly larger than the 50-frame estimate
but preserves the same conclusion. Batch 4,096 is a strong algorithmic point;
batch 512 or 1,024 may be a better first RTL point because it needs a much
smaller frontier buffer.

## 6. Work/span interpretation

The test removes the fine-grained incumbent synchronization chain:

- normal CAP: about 353k certification scores become 86.6 synchronization
  rounds;
- compact CAP: about 285k become 70.0 rounds;
- dual-compact CAP: about 116k become 28.9 rounds.

This is approximately a 4,000-fold reduction in incumbent-dependent
synchronization, not a 4,000-fold latency claim.

`B=4096` does not require 4,096 physical scorers. A hardware design may
time-multiplex the batch:

- 64 scorer lanes: 64 issue cycles per full batch;
- 128 scorer lanes: 32 issue cycles per full batch;
- one reduction and incumbent commit after the batch.

The software implementation still generates nodes in DFS order. It proves
that bounded incumbent staleness is cheap; it does not yet implement or
measure a complete explicit frontier scheduler, bank conflicts, queue
pressure, or RTL control stalls.

## 7. RTL-worthiness verdict

### Normal and compact CAP: yes

The result is strong enough to justify an RTL architecture study:

- exactness is preserved;
- TEP overhead is below 0.1% for normal and compact at batch 4,096;
- fine-grained incumbent feedback is no longer required;
- normal and compact bound-query counts barely change;
- batches can be mapped onto a practical number of time-multiplexed lanes.

### Dual-compact CAP: conditional

The search schedule is RTL-worthy, and dual-compact still evaluates by far the
fewest TEPs. Its bound engine remains the obstacle:

- four multiplier updates are sequential;
- parity groups and group solves are parallel;
- even the specialized software solver has substantial work;
- an RTL implementation should first cycle-model a selective or parallel
  dual engine rather than synthesize the software recurrence directly.

## 8. Recommended next hardware experiment

Build an explicit frontier/candidate architecture with configurable:

- batch size `B = {512, 1024, 4096}`;
- physical scoring lanes `P = {16, 32, 64, 128}`;
- normal CAP and compact CAP-PDB bound engines;
- strict `L > U` pruning and a batch-end best/runner-up reduction.

Measure:

1. exact replay agreement on the saved stream;
2. queue occupancy and descriptor memory;
3. cycles/frame, including table construction, bounds, scoring, control, and
   stalls;
4. LUT/FF/BRAM/DSP and Fmax after place-and-route;
5. mean/p95/p99 cycles/frame.

Only add dual-compact RTL if a cycle model shows that its saved scoring calls
outweigh the parallel dual-bound work.

## 9. Reproduction

Modified implementation:

- `experiments/parity_window_end_to_end_benchmark.cc`
- `experiments/reconstructed_cap_pdb_audit.cc`

Runner and analysis:

- `experiments/run_wavefront_cap_audit.sh`
- `experiments/analyze_wavefront_cap_audit.py`

Canonical results:

- `experiments/results/wavefront_cap_final_2026-07-30/raw_batch_1.csv`
- `experiments/results/wavefront_cap_final_2026-07-30/raw_batch_4096.csv`
- `experiments/results/wavefront_cap_final_2026-07-30/aggregate.csv`

The batch-4096 raw-result SHA-256 is:

`cdf4c30995ed19512ae910343598af09f4121991e5ab056075f82f0a816fc2de`
