# Research status

## Current state

- Branch: `research/osd-paged-vector`
- Draft PR: #1
- Baseline commit: `e6cfc5b0f71d8e82d6cba2184b1edf0486f64238`
- Current phase: Phase 12 complete — exact-stop plus pthread fallback validated; persistence conditional
- Upstream production integration: not started; opt-in experiment only
- Latest focused CI: Phase 11 GitHub Actions run #109 passed; Phase 12 local suite and sanitizers passed

## Claim boundary

Source inspection changed the novelty claim. The upstream decoder already
updates candidates incrementally: `OrderedStatisticsDecoder::flip(j)` XORs
generator row `G[j]` into the current word. Generic parity-delta caching is
therefore not novel relative to this code.

The supported contribution is narrower:

> bit-exact parity-only scoring with bounded page delivery and page-local
> prefix-delta expansion, plus a parameterized multi-cycle RTL baseline.

No FPGA acceleration, mapped timing, energy, end-to-end production speedup, or
new decoding-algorithm claim is made.

## Phase summary

| Phase | Gate | Result |
|---|---|---|
| 0 | Reproducible baseline | Pass |
| 1 | Production TEP order captured and cross-checked | Pass |
| 2 | Candidate-stage workload measured | Pass |
| 3 | Parity-only exactness and kernel benchmark | Pass |
| 4 | Paged and prefix-delta software models | Pass |
| 5 | Evidence freeze and padding regression | Pass |
| 6 | RTL golden vectors and generic synthesis | Pass |
| 7 | Combined cached P8 parity-only software decoder | Correct but 10.72x slower; reject |
| 8 | Native parity plus pthread DFS subtrees | Exact; pthread direction passes |
| 9 | Full-state versus parity pthread DFS A/B | Exact; pthread robust, parity target-dependent |
| 10 | Exact absolute-bound early-stop probe | High-SNR mean benefit; p95 unchanged |
| 11 | Generator-aware exact subtree-bound gate | Saves <0.006% work even with oracle thresholds; reject |
| 12 | Persistent pthread plus exact-stop hybrid | Exact hybrid passes; persistence traffic/target-dependent |

## Production traversal

Compile-time opt-in hooks trace the actual C++ decoder. With hooks disabled,
they preprocess to no-ops and do not change the public API.

| Case | Candidates | C++/Python sequence hash |
|---|---:|---|
| K=5, order 3 | 26 | `d1d09a68b627c23a` |
| K=64, order 4 | 679,121 | `9717451b3bb8a575` |

The complete K=5 sequence matched the independent Python reference. The
production case had no duplicate, missing, overweight, or out-of-range masks,
and traversal state returned to zero.

## Isolated TEP delivery

A standalone Python benchmark now compares four delivery policies at K=64,
order 4. It used 200 blocks, two warm-up blocks, five rotating-order repeats,
and a checksum that consumed all 679,121 masks per block. Memory probes were
separate from timing.

| Mode | Median ms/block | P95 ms/block | Median TEP/s | Live payload |
|---|---:|---:|---:|---:|
| Regenerate per block | 116.484 | 120.279 | 5.830 M | 5,432,968 B |
| Persistent full cache, warm | 19.437 | 20.834 | 34.939 M | 5,432,968 B |
| Paged streaming, P=4096 | 146.974 | 185.113 | 4.621 M | 32,768 B |
| Single-TEP streaming | 119.883 | 131.906 | 5.665 M | 8 B |

The cache had a 98.490 ms median cold-build cost and was 5.99x faster than
regeneration once warm. Paged delivery reduced live mask payload by 99.40% but
was 1.26x slower in this Python implementation. Single streaming was 1.03x
slower than regeneration.

These are delivery-plus-checksum timings, not decoder or hardware speedups.
The result makes full caching attractive for software when 5.18 MiB of
persistent mask storage is acceptable, and bounded streaming attractive when
memory or hardware interfaces dominate.

## Candidate-stage workload

For one BCH(127,64), order-4 block:

- metric evaluations: 679,121;
- padded metric terms: 86,927,488;
- generator-row flips: 1,358,240;
- padded flip XOR terms: 173,854,720.

In the final fixed-seed profile, candidate search had a median of 11,161,850 ns
and occupied 99.014% of the 11,273,005 ns median decoder time. The nine-frame
p95 total was 15,828,626 ns. These are single-host software measurements.

## Parity-only exactness

The model stores the 63 parity positions, maintains the systematic metric with
one constant-size update per traversal flip, and scans only parity positions.

Coverage:

- all 32,768 hard-sign patterns for BCH(15,5), order 3;
- three fixed BCH(127,64), order-4 frames;
- every candidate metric and bit;
- best and runner-up scores, ties, uniqueness, winning candidate, and decoded
  output;
- restored traversal mask, parity state, and systematic metric.

For BCH(127,64), order 4:

- parity metric terms: 42,784,623;
- parity flip terms: 85,569,120.

Controlled full-width/parity-only median kernel ratios were 1.90958x, 1.89930x,
and 1.88252x. This is not an integrated decoder speedup.

## Paged software models

Independent and prefix-delta modes both processed the complete production
stream at P=1,2,4,8,16. All modes matched sequence hash
`9717451b3bb8a575`, content hash `c9da0a5ebdc96618`, scores, ties, winner,
decoded output, page counts, and the one-candidate final partial page.

| P | Independent bytes | Prefix-delta bytes |
|---:|---:|---:|
| 1 | 71 | 260 |
| 2 | 142 | 394 |
| 4 | 284 | 662 |
| 8 | 568 | 1,198 |
| 16 | 1,136 | 2,270 |

Payload figures exclude allocator and container metadata.

## Combined end-to-end experiment

An opt-in search override kept the real production preprocessing and output
path while replacing only candidate search with all proposed software features:
a persistent production-order cache, P=8 pages, adjacent-mask prefix deltas,
parity-only state/scoring, and exact best/runner-up reduction.

Correctness across nine fixed BCH(127,64), order-4 frames:

- all 679,121 candidates and 84,891 pages processed;
- final partial page contained one lane;
- decoded bytes and uniqueness matched production;
- best score, runner-up score and winning permuted candidate matched;
- cache sequence hash remained `9717451b3bb8a575`.

End-to-end timing used two warm-up rounds, nine frames per repeat, nine repeats
and alternating execution order:

| Mode | Median ms/block | P95 ms/block | Candidate ms | Blocks/s |
|---|---:|---:|---:|---:|
| Production baseline | 10.104 | 11.035 | 10.014 | 98.972 |
| Combined cached P8 parity | 108.315 | 122.776 | 108.226 | 9.232 |

The combined path is 10.72x slower, not faster. The component improvements are
not multiplicative. Cached candidate masks plus materialized prefix pages
duplicate state transition work that the compiler-friendly in-place DFS loop
already performs efficiently.

Decision: reject this naïve combined software architecture. Preserve the result
as a systems finding. For software, test parity-only state while retaining the
native traversal. Treat page vectorisation as a hardware architecture whose
benefit requires mapped parallel timing.

## Native parity and pthread DFS experiment

The Phase 8 software experiment retained production preprocessing and output,
removed the TEP cache and materialized pages, and compared the native candidate
loop with stateful parity scoring and pthread DFS subtrees. RTL was not changed.

Every tested mode matched decoded bytes, uniqueness, best and runner-up metrics,
and the complete winning permuted candidate across nine fixed BCH(127,64),
order-4 frames. Original production traversal indices preserve the earliest
winner when metrics tie. Thread creation and joining are included per block.

Portable `-O3 -march=x86-64` results:

| Mode | Median ms/block | P95 ms/block | Blocks/s | Speedup |
|---|---:|---:|---:|---:|
| Production baseline | 8.365 | 8.490 | 119.5 | 1.00x |
| Stateful parity, one thread | 7.211 | 7.363 | 138.7 | 1.16x |
| pthread, 2 workers | 2.920 | 3.001 | 342.5 | 2.87x |
| pthread, 4 workers | 1.584 | 1.633 | 631.4 | 5.28x |
| pthread, 8 workers | 0.973 | 1.062 | 1,027.9 | **8.60x** |
| pthread, 16 workers | 1.033 | 1.203 | 967.8 | 8.10x |

Host-native `-O3 -march=native` results:

| Mode | Median ms/block | P95 ms/block | Blocks/s | Speedup |
|---|---:|---:|---:|---:|
| Production baseline | 3.932 | 3.969 | 254.3 | 1.00x |
| Stateful parity, one thread | 5.336 | 5.428 | 187.4 | 0.74x |
| pthread, 2 workers | 1.969 | 2.121 | 507.8 | 2.00x |
| pthread, 4 workers | 1.119 | 1.162 | 893.4 | 3.51x |
| pthread, 8 workers | 0.748 | 0.816 | 1,336.6 | **5.26x** |
| pthread, 16 workers | 0.844 | 0.993 | 1,185.0 | 4.66x |

Eight workers are optimal on the nine-CPU allocation. Stateful parity alone is
not robust across compiler targets, but pthread subtree parallelism remains
positive in both builds. Address/Undefined sanitizers passed with leak detection
disabled because of ptrace, and ThreadSanitizer reported no race.

This is an opt-in experiment, not production integration. It has not yet tested
persistent workers, affinity, real channel traces, other code/order settings,
concurrent decoder instances, energy, or loaded-system tail latency.

## Full-state versus parity pthread DFS A/B

Phase 9 ran the missing attribution control: identical depth-two subtree
partitioning, dynamic work queue, thread counts, candidate ordering and
best/runner-up merge, with either full 128-byte candidate state or parity-only
delta state. RTL remained unchanged.

All modes again matched production decoded bytes, uniqueness, best, runner-up,
and the complete earliest winning candidate on nine fixed BCH(127,64), order-4
frames.

First controlled runs at eight workers:

| Build | Full-state pthread | Parity pthread | Full speedup | Parity speedup |
|---|---:|---:|---:|---:|
| `-O3 -march=x86-64` | 1.528 ms | 1.081 ms | 5.52x | 7.80x |
| `-O3 -march=native` | 0.864 ms | 0.873 ms | 4.59x | 4.54x |

The full eight-worker benchmark was executed three times per compiler target.
The median full/parity latency ratio was 1.398 for portable x86-64, so
parity-only state was about 40% faster there. The host-native median ratio was
0.998, with individual ratios from 0.990 to 1.054; the kernels are effectively
tied there.

Conclusion: DFS subtree parallelism is the robust optimization. Parity-only
state is a target-dependent optional kernel and must not receive sole credit for
the threaded speedup. Keep the full-state implementation as the control and
select parity only after target-specific benchmarking.

The new full-state path passed Address/Undefined sanitizers, ThreadSanitizer,
and the complete bounded repository suite. Thread creation/join remains included
per decoded block.

## Exact early-stop viability probe

Phase 10 tested the smallest provably safe early stop. After normal production
preprocessing, exhaustive order-4 search is skipped only when the order-0
candidate metric equals `sum(abs(LLR))`. With nonzero LLRs, no different
codeword can equal or exceed that metric. Otherwise the unmodified exhaustive
search runs.

The test used 500 random encoded BCH(127,64) BPSK/AWGN frames per SNR:

| Eb/N0 | Exact stops | Mean candidates | Mean speedup | Baseline p95 | Probe p95 |
|---:|---:|---:|---:|---:|---:|
| 4 dB | 0.0% | 679,121 | 1.00x | 4.519 ms | 4.477 ms |
| 6 dB | 5.2% | 643,807 | 1.05x | 4.567 ms | 4.595 ms |
| 8 dB | 50.8% | 334,128 | 2.01x | 4.473 ms | 4.439 ms |
| 10 dB | 92.4% | 51,614 | 12.07x | 4.408 ms | 4.293 ms |

All 2,000 outputs matched exhaustive decoding. The result establishes a
high-SNR average-work opportunity, but not a tail-latency contribution: even at
10 dB the 7.6% miss rate leaves p95 on the exhaustive path. The check is also
straightforward and is not claimed as novel.

## Generator-aware exact subtree-bound gate

Phase 11 tested the proposed stronger safe bound before building a cancellation
scheduler. For each depth-two DFS prefix, the probe combined the exact current
systematic metric, the two largest positive systematic gains still available,
and a parity upper bound derived from the remaining generator-row support.
Parity positions unreachable by all remaining rows kept their current metric
contribution; reachable positions used `abs(LLR)`.

The completed exhaustive search supplied the final best and runner-up metrics as
oracle thresholds. Bound-evaluation cost was excluded. The result is therefore
optimistic for this bound.

| Eb/N0 | Bound mean candidates | Bound reduction | Bound p95 | Combined mean | Combined p95 |
|---:|---:|---:|---:|---:|---:|
| 4 dB | 679,086.402 | 0.0051% | 679,115 | 679,086.402 | 679,115 |
| 6 dB | 679,092.604 | 0.0042% | 679,119 | 643,779.476 | 679,119 |
| 8 dB | 679,096.814 | 0.0036% | 679,119 | 334,115.762 | 679,115 |
| 10 dB | 679,101.002 | 0.0029% | 679,120 | 51,612.268 | 679,087 |

The safe bound saves only 20--35 candidates per 679,121-candidate frame. Its
p95 work is effectively exhaustive, and any real bookkeeping would outweigh
the saved work. Reject this formulation and do not build a scheduler around it.
This does not prove that every possible exact bound must fail; it establishes
that this generator-reachability formulation has no practical headroom.

## Persistent pthread plus exact-stop hybrid

Phase 12 compared production, spawn-per-block pthread DFS, sleeping persistent
pthread DFS, and the exact order-0 stop in front of both full-state and
parity-only fallbacks. Seven background pthreads plus the decoder caller form
eight compute workers. Pool startup was measured separately and excluded from
per-block latency.

The BPSK/AWGN benchmark used 200 BCH(127,64), order-4 frames per SNR, three
repeats, and both `-march=native` and `-march=x86-64`. Each row therefore has
600 latency samples. All paths matched exhaustive decoded bytes, uniqueness,
best metric, earliest winner, and non-stopped runner-up.

Parity-hybrid tail results:

| Build, Eb/N0 | Stop rate | Baseline p95/p99 | Exact spawn p95/p99 | Exact persistent p95/p99 |
|---|---:|---:|---:|---:|
| Native, 4 dB | 0.0% | 4.118 / 4.503 ms | 0.986 / 1.081 ms | 0.913 / 1.027 ms |
| Native, 8 dB | 46.5% | 4.018 / 4.124 ms | 0.924 / 0.968 ms | 0.824 / 0.892 ms |
| Native, 10 dB | 92.0% | 3.983 / 4.047 ms | 0.853 / 0.913 ms | 0.866 / 0.971 ms |
| x86-64, 4 dB | 0.0% | 10.238 / 11.117 ms | 1.520 / 1.737 ms | 1.544 / 1.775 ms |
| x86-64, 8 dB | 46.5% | 8.913 / 10.093 ms | 1.259 / 1.384 ms | 1.220 / 1.340 ms |
| x86-64, 10 dB | 92.0% | 9.168 / 9.703 ms | 1.107 / 1.244 ms | 1.065 / 1.238 ms |

The integrated exact-stop plus pthread fallback direction passes. Persistence
helps most when fallback work is frequent or continuous. It is not an
unconditional tail win: after long exact-stop idle intervals, condition-variable
wake and scheduling costs can erase thread-creation savings. At native 10 dB,
spawn-on-miss parity beats persistent parity at p95 and p99; portable 10 dB
slightly favors persistence at those percentiles.

Keep spawn-on-miss as the conservative high-SNR policy and persistent workers as
an opt-in continuous-traffic policy. Keep full-state DFS as the control and
select parity only after target/workload benchmarking. Address/Undefined
sanitizers, ThreadSanitizer, and the complete bounded repository suite passed.

## RTL evidence

Two parameterized implementations are present:

- a fully combinational page expander/scorer retained as a correctness and area
  baseline;
- a sequential engine that builds deltas over K cycles, expands one page, and
  scores parity positions over R cycles.

Both passed 128 deterministic Python golden-vector pages at K=5, R=10, P=4,
including `valid_lanes` partial-page cases. The sequential test observed
K+R+1 = 16 cycles per page.

Generic Yosys 0.33 synthesis for production K=64, R=63 reported:

| Architecture | P | Cycles/page | Generic cells | Peak memory |
|---|---:|---:|---:|---:|
| Combinational baseline | 8 | 0 | 252,104 | 1,167.50 MB |
| Sequential | 1 | 128 | 11,480 | 280.84 MB |
| Sequential | 8 | 128 | 21,890 | 525.73 MB |
| Sequential | 16 | 128 | 33,894 | 1,203.20 MB |

The sequential P8 design is about 11.5x smaller than the combinational P8
baseline. Nominal candidate issue rates are P/128 candidates per cycle:
0.0078125, 0.0625, and 0.125 for P=1,8,16. P16 doubles nominal lane throughput
over P8 for 1.55x the generic cells, but mapped frequency is unknown.

P8 remains the safer first integration point; P16 is the required
throughput/area comparison. Generic Yosys cells are not FPGA LUTs or ASIC area.

## Correctness hardening

The branch zero-initializes padded positions N..W-1 in `G` and `codeword`
for both OSD classes. A targeted regression checks matrix, candidate, and soft
padding. Candidate hashes and exactness gates remained stable.

## Reproduction

Software:

```text
make -C experiments clean test CXX='g++ -march=x86-64'
```

RTL validation is encoded in `.github/workflows/osd-research.yml`. Phase 11 run #109:

- built the upstream OSD regression target;
- ran the bounded C++ and Python suite;
- passed both RTL simulations;
- synthesized sequential P=1,8,16;
- reported zero Yosys problems.

Machine-readable outputs are in `experiments/results/`.

## Next gated phase

1. Keep the exact order-0 stop plus exact pthread DFS fallback as the surviving
   integrated CPU direction.
2. Keep spawn-on-miss as the conservative high-SNR policy.
3. Treat sleeping persistent workers as an opt-in continuous-traffic policy,
   not the unconditional default.
4. Retain full-state DFS as the control and choose parity only after target and
   workload benchmarking.
5. Before production integration, test affinity, loaded-system p99, concurrent
   decoders, shared versus per-decoder pools, real arrival gaps, and additional
   codes/orders.
6. Keep cached/materialized pages and the tested subtree bound rejected.
7. Leave RTL unchanged.
