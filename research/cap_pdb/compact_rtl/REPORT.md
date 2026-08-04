# Parallel compact CAP-PDB RTL study — phase 2

**Date:** 2026-07-30
**Status:** canonical 1,000-frame trace-derived cycle audit complete; five
state-wide RTL variants and one dual-read alternative synthesized; reduced and
randomized RTL simulation passed; place-and-route and complete decoder
integration not yet performed.

## 1. Result

Effect-level parallelism can make compact CAP-PDB reduce mean search-core
cycles, even with a one-TEP-per-cycle scorer. It does **not** make compact an
unconditional FPGA winner.

At canonical eBCH(128,64), OSD-4, 0 dB:

| Architecture | Mean bound cycles/frame | Mean total cycles, scorer II=1 | Change vs normal | Frame win rate |
|---|---:|---:|---:|---:|
| Normal CAP | 64,995 | 424,249 | — | — |
| Compact, \(P=4\) | 145,272 | 436,418 | **2.87% worse** | 35.9% |
| Compact, \(P=8\) | 119,012 | 410,158 | **3.32% better** | 64.1% |
| Compact, \(P=16\) | 106,553 | 397,699 | **6.26% better** | 74.4% |

The cost is large:

| Query engine | Estimated LCs | LUT primitives | FFs | RAMB18 | DSP |
|---|---:|---:|---:|---:|---:|
| Normal CAP, phase 1 | **1,807** | **1,878** | 100 | **143** | 1 |
| Compact state-wide, \(P=4\) | 17,757 | 20,974 | 670 | 195 | 0 |
| Compact state-wide, \(P=8\) | 18,551 | 23,107 | 670 | 195 | 0 |
| Compact state-wide, \(P=16\) | 20,371 | 27,758 | 670 | 195 | 0 |

Thus \(P=8\) costs about 10.3 times the estimated logic cells and 1.36 times
the BRAM of normal CAP for a 3.32% mean cycle reduction at scorer initiation
interval one.

## 2. Architecture

### 2.1 Why the phase-1 scanner did not scale

The phase-1 compact kernel issued one state-addressed CAP read per group,
alternating issue and accumulation cycles. Its lower-bound cycle estimate was
at least 451,238 cycles/frame.

A first parallel attempt instantiated one independent 32:1 state selector per
effect lane. It synthesized to:

- \(P=1\): 8,472 estimated LCs and 195 RAMB18;
- \(P=2\): 18,309 estimated LCs and 195 RAMB18.

The second lane almost doubled the selector network. This architecture was
discarded.

### 2.2 State-wide mask-reduction engine

The retained architecture stores, per parity group, one complete 32-state CAP
vector for each \((\text{suffix},\text{cardinality})\) row:

\[
325 \times (32\cdot16)\ \text{bits per group}.
\]

One synchronous read exposes all 32 costs. The engine then:

1. selects the lowest \(P\) set effects from the reachable mask;
2. XOR-permutes the 32-bit mask by the current five-bit group state;
3. applies a balanced, masked 32-way minimum tree to the state-wide cost word;
4. processes all 13 parity groups in parallel.

The exact allocation latency is:

\[
C_{\text{allocation}}(P)
=1+\left\lceil\frac{\max_g |R_g|}{P}\right\rceil.
\]

The first term is the synchronous table read. The second is the number of mask
batches. No effect-lane bank conflict occurs because every state cost is in the
same wide word and every parity group has an independent bank.

### 2.3 Memory consequence

The payload remains 2.1632 Mbit, but 325×512 organization is width-inefficient
on RAMB18 primitives. Yosys maps each group to 15 RAMB18s:

\[
13\cdot 15=195\ \text{RAMB18s}.
\]

This corrects the earlier optimistic estimate of approximately 156 RAMB18s.

### 2.4 Narrow dual-read alternative

A pipelined two-effect engine retaining state-addressed tables was also built.
It passed simulation and synthesized to 5,855 estimated LCs. Yosys duplicated
the payload to implement two reads, producing 286 RAMB18s. A vendor-specific
true-dual-port primitive may avoid that duplication, but this study does not
claim the unsynthesized 143-RAMB18 result.

## 3. Canonical trace experiment

Configuration:

- eBCH(128,64,22);
- OSD order 4;
- BPSK over AWGN;
- \(E_b/N_0=0\) dB;
- seed `5928218492399464753`;
- 1,000 matched frames;
- stream checksum `11508365490867720138`.

The exact compact decoder was instrumented inside `compact_lower_bound()`.
For every chronological compact allocation, it measured the maximum reachable
population across the 13 groups and accumulated the exact cycle formula for
\(P\in\{1,2,4,8,16\}\). Normal CAP cycles use its exact residual-cardinality
query count.

The audit reproduced:

- normal CAP: 354,339.465 unique TEPs/frame and 359,253.583 scoring calls/frame;
- compact CAP: 286,642.377 unique TEPs/frame and 291,145.797 scoring calls/frame;
- compact saving: 68,107.786 scoring calls/frame;
- zero decoded-word, best-metric, or tie mismatches;
- the frozen stream checksum.

## 4. Bound-cycle distribution

| Engine | Mean | Median | p95 | p99 | Maximum |
|---|---:|---:|---:|---:|---:|
| Normal CAP | **64,995** | **73,633** | **82,298** | **83,522** | **84,710** |
| Compact \(P=1\) | 304,266 | 325,433 | 338,532 | 342,306 | 345,581 |
| Compact \(P=2\) | 198,182 | 214,408 | 224,633 | 226,929 | 228,987 |
| Compact \(P=4\) | 145,272 | 158,985 | 168,142 | 169,834 | 171,557 |
| Compact \(P=8\) | 119,012 | 131,490 | 140,303 | 141,634 | 143,033 |
| Compact \(P=16\) | 106,553 | 118,453 | 127,182 | 128,597 | 130,054 |

Compared with the phase-1 serial lower bound of 451,238 cycles/frame:

- \(P=8\) reduces compact bound cycles by 73.63%;
- \(P=16\) reduces them by 76.39%.

## 5. Allocation floor and diminishing returns

Compact executes 35,215.798 valid left/right allocations per frame on average.
Even with all 32 effects processed together, every allocation requires at
least a table-read cycle and one reduction cycle:

\[
C_{\text{compact}}\ge 2(35{,}215.798)=70{,}431.596
\]

before residual normal-mode queries and outer control.

This floor already exceeds normal CAP's mean 64,995 bound cycles. Therefore
effect parallelism alone cannot make the compact **bound engine** cheaper than
normal CAP. Compact can only win end to end because it avoids scoring calls.

The aggregate scorer break-even initiation interval is:

| Compact lanes | Break-even scorer II |
|---:|---:|
| 1 | 3.513 |
| 2 | 1.956 |
| 4 | 1.179 |
| 8 | **0.793** |
| 16 | **0.610** |

The first tested design that wins the mean at a one-call-per-cycle scorer is
\(P=8\).

## 6. End-to-end search-core model

The model is:

\[
C_{\text{search core}}
=C_{\text{bound}}+\mathrm{II}_{\text{score}}N_{\text{score}}.
\]

Common table-build cycles, DFS control, and any overlap between scoring and
bound processing are excluded.

### Mean reduction versus normal CAP

| Scorer II | \(P=4\) | \(P=8\) | \(P=16\) |
|---:|---:|---:|---:|
| 1 | −2.87% | **3.32%** | **6.26%** |
| 2 | **7.14%** | **10.49%** | **12.08%** |
| 4 | **12.79%** | **14.54%** | **15.37%** |
| 8 | **15.81%** | **16.70%** | **17.12%** |
| 16 | **17.37%** | **17.82%** | **18.03%** |

### Tail behavior at scorer II=1

| Engine | Mean | p95 | p99 | Maximum |
|---|---:|---:|---:|---:|
| Normal CAP | 424,249 | **620,631** | **646,503** | **1,330,155** |
| Compact \(P=8\) | 410,158 | 652,898 | 693,564 | 1,379,431 |
| Compact \(P=16\) | **397,699** | 639,787 | 680,653 | 1,366,577 |

Compact improves the mean but worsens the tail with a fully pipelined scorer.
At scorer II=4, \(P=16\) improves mean by 15.37% and p95 by about 1.8%, while
p99 and maximum are approximately tied.

## 7. Synthesis sweep

All retained state-wide variants use the same 195 RAMB18 payload:

| \(P\) | Estimated LCs | LUTs | FFs | RAMB18 | Structural errors |
|---:|---:|---:|---:|---:|---:|
| 1 | 17,012 | 19,619 | 670 | 195 | 0 |
| 2 | 17,179 | 19,796 | 670 | 195 | 0 |
| 4 | 17,757 | 20,974 | 670 | 195 | 0 |
| 8 | 18,551 | 23,107 | 670 | 195 | 0 |
| 16 | 20,371 | 27,758 | 670 | 195 | 0 |

\(P=16\) improves the II=1 mean by only another 3.04% relative to \(P=8\),
while adding 9.81% estimated LCs and lengthening the unpipelined selection
path. \(P=8\) is therefore the current architectural knee.

## 8. Verification

Completed gates:

- 1,000 canonical frames;
- zero word, metric, or tie mismatches;
- frozen stream checksum reproduced;
- five deterministic state-wide RTL simulations passed;
- 64 randomized allocations per lane count, 320 total, passed exact sum and
  exact cycle checks;
- dual-read \(P=2\) RTL smoke test passed;
- all six synthesized engines passed Yosys structural checks with zero
  reported problems.

## 9. What is and is not proved

Proved by this phase:

- the parallel allocation kernels are functionally correct on the completed
  RTL tests;
- trace-derived cycle counts use exact reachable masks from the canonical
  decoder;
- state-wide \(P=8/16\) can beat normal CAP in mean search-core cycles under
  the stated non-overlapped scoring model;
- effect-level parallelism has an allocation-count floor;
- the area cost of the state-wide implementation is substantial.

Not yet proved:

- routed Fmax or timing closure;
- actual board throughput or power;
- complete RTL equivalence for every canonical bound value;
- the cost of the state-wide CAP table builder;
- cycles for the outer allocation controller and DFS controller;
- overlap between bound and scoring pipelines;
- a hardware win on a named FPGA part.

## 10. Decision

Normal CAP remains the best demonstrated area-efficient architecture.

If hardware area is the primary constraint, build normal CAP. If the candidate
scorer is serialized to II≥2 and extra BRAM/LUT cost is acceptable, compact
becomes increasingly attractive. If continuing the compact path, place and
route \(P=8\) and \(P=16\) against normal CAP on the same named part; \(P=8\)
is the default candidate and \(P=16\) is the latency frontier.

Physical board testing is still premature. The next decisive gate is complete
normal/P8/P16 RTL integration with the same scorer and controller, followed by
place-and-route timing.

## 11. Reproduction files

- `experiments/compact_parallel_trace_audit.cc`
- `experiments/analyze_compact_parallel_rtl.py`
- `experiments/rtl/cap_pdb_parallel_compact_engine.sv`
- `experiments/rtl/cap_pdb_parallel_compact_tb.sv`
- `experiments/rtl/cap_pdb_parallel_compact_random_tb.sv`
- `experiments/rtl/cap_pdb_dual_port_compact_engine.sv`
- `experiments/rtl/cap_pdb_dual_port_compact_tb.sv`
- `experiments/rtl/run_parallel_compact_synthesis.mjs`
- `experiments/rtl/run_dual_port_compact_synthesis.mjs`
- `experiments/results/cap_pdb_parallel_rtl_2026-07-30/`
