# Bit-exact paged OSD candidate engine: Phase 0-10 report

## Decision

Keep the naïve cached/materialized P8 software architecture rejected. It is
bit-exact but 10.72x slower than the production decoder.

Dynamic pthread DFS-subtree scheduling is the robust software result. A direct
A/B with identical scheduling measured 4.59x host-native full-decoder speedup
for full 128-byte state and 4.54x for parity-only state at eight workers. In a
portable x86-64 build, the respective results were 5.52x and 7.80x.

Across three complete benchmark processes per compiler target, parity-only state
made the portable pthread kernel about 40% faster but was effectively tied with
full-state under the host-native build. Therefore parity-delta is an optional,
target-benchmarked kernel, not the general source of the pthread speedup.

Do not replace the production loop yet. The next gate is a persistent worker
pool supporting both kernels, followed by affinity, broader workloads,
concurrent instances, and loaded-system tail latency. RTL remains unchanged.

## Research question

Can the upstream BCH(127,64), order-4 OSD preserve its exact candidate set,
order, metric, tie behavior, and decoded output while reducing candidate-state
work and bounding the storage used for page-parallel processing?

## Baseline correction

The upstream `CODE::OrderedStatisticsDecoder` already performs incremental
candidate updates. `flip(j)` XORs generator row `G[j]` into the current
candidate, and recursive backtracking restores state.

Generic parity-delta caching is therefore not a new contribution relative to
this baseline. The defensible result is:

> bit-exact parity-only metric decomposition combined with bounded page
> vectorisation, page-local prefix-delta expansion, and a parameterized
> multi-cycle RTL realization.

## Findings

### 1. Production TEP traversal is now captured directly

Compile-time opt-in hooks observe the actual C++ decoder. With hooks disabled,
they compile to no-ops and leave the API unchanged.

| Case | Candidate count | C++/Python sequence hash |
|---|---:|---|
| K=5, order 3 | 26 | `d1d09a68b627c23a` |
| K=64, order 4 | 679,121 | `9717451b3bb8a575` |

The independent Python traversal matched the complete small sequence and the
production count/order fingerprint. No duplicate, missing, overweight, or
out-of-range mask was observed, and state returned to zero.

### 1a. Isolated TEP delivery has a clear speed-memory trade-off

The recreated delivery benchmark used 200 BCH(127,64), order-4 blocks, two
warm-up blocks, five repeats, rotating mode order, and checksum consumption of
every TEP. Sequence validation preceded timing and memory probes were separate.

| Mode | Median ms/block | Median TEP/s | Live payload |
|---|---:|---:|---:|
| Regenerate | 116.484 | 5.830 M | 5,432,968 B |
| Persistent cache, warm | 19.437 | 34.939 M | 5,432,968 B |
| Paged P4096 | 146.974 | 4.621 M | 32,768 B |
| Single TEP | 119.883 | 5.665 M | 8 B |

The warm full cache was 5.99x faster than regeneration after a separately
measured 98.490 ms median cold build. Paged streaming cut live mask payload by
99.40% but was 1.26x slower in this Python implementation. Single streaming
was 1.03x slower than regeneration.

This result does not overturn the bottleneck finding below: the production
decoder already generates and consumes candidates inside its compiled loop.
The TEP benchmark isolates delivery policy and is most useful for software
cache decisions and bounded hardware-interface design.

### 2. Candidate search is the measured software bottleneck

One production block performs:

- 679,121 metric evaluations;
- 86,927,488 padded metric terms;
- 1,358,240 row flips;
- 173,854,720 padded flip XOR terms.

Across nine fixed-seed frames on the recorded EPYC/G++ host, candidate search
had an 11,161,850 ns median and occupied 99.014% of the 11,273,005 ns median
decoder time. TEP delivery by itself is therefore a secondary target.

### 3. Parity-only scoring is bit-exact in the tested scope

The model stores and updates the 63 parity positions, updates the affected
systematic metric contribution in constant work per flip, and scans only parity
positions per candidate.

Exactness covered:

- all 32,768 hard-sign patterns for BCH(15,5), order 3;
- three fixed BCH(127,64), order-4 frames;
- every candidate metric and bit;
- best and runner-up metrics, ties, uniqueness, winner, and decoded output;
- restored mask, parity, and systematic-metric state.

It reduced metric terms from 86,927,488 to 42,784,623 and parity flip terms from
173,854,720 to 85,569,120. Controlled full-width/parity-only median kernel
ratios were 1.90958x, 1.89930x, and 1.88252x. This is not an integrated decoder
speedup.

### 4. Bounded pages preserve the production result

Independent-lane and prefix-delta models were tested at P=1,2,4,8,16. Every
mode processed all 679,121 candidates and matched:

- mask sequence hash `9717451b3bb8a575`;
- candidate/metric hash `c9da0a5ebdc96618`;
- best and runner-up values;
- tie and uniqueness behavior;
- winning candidate and decoded output;
- page count and the one-candidate final partial page.

| P | Independent payload | Prefix-delta payload |
|---:|---:|---:|
| 1 | 71 B | 260 B |
| 2 | 142 B | 394 B |
| 4 | 284 B | 662 B |
| 8 | 568 B | 1,198 B |
| 16 | 1,136 B | 2,270 B |

Payload excludes allocator/container metadata.

### 5. Padding was hardened

`metric()` and `flip()` operate over padded width W. The branch now
zero-initializes positions N..W-1 in `G` and `codeword` for both OSD classes.
A targeted regression checks the padding, and all hashes and exactness gates
remained stable.

### 6. RTL exposed and resolved an area failure

The first RTL implemented the whole P=8 prefix expansion and scoring page
combinationally. It passed golden vectors but synthesized to 252,104 generic
cells. This is an important negative result: direct full-page combinational
realization is not a credible baseline.

A multi-cycle design now:

1. builds adjacent-mask parity deltas over K cycles;
2. expands the page from the carried boundary;
3. scores one parity position per cycle across P lanes;
4. returns the final valid lane as the next boundary;
5. masks invalid lanes on partial pages.

Both implementations passed 128 deterministic Python golden-vector pages at
K=5, R=10, P=4, including partial-page cases. The sequential test observed
K+R+1 = 16 cycles/page.

Generic Yosys 0.33 synthesis at K=64, R=63:

| Architecture | P | Cycles/page | Cells | Peak Yosys memory |
|---|---:|---:|---:|---:|
| Combinational baseline | 8 | 0 | 252,104 | 1,167.50 MB |
| Sequential | 1 | 128 | 11,480 | 280.84 MB |
| Sequential | 8 | 128 | 21,890 | 525.73 MB |
| Sequential | 16 | 128 | 33,894 | 1,203.20 MB |

Sequential P8 is about 11.5x smaller than combinational P8. Nominal issue rate
is P/128 candidates per cycle, giving 0.0625 for P8 and 0.125 for P16. P16
doubles nominal lane throughput for 1.55x the generic cells, but no mapped
frequency is available. P8 is consequently the safer first integration point,
not a proven final optimum.

### 7. Combining every component produced a negative result

The opt-in combined experiment retained the real production reliability sort,
matrix permutation, row-echelon conversion, systematic conversion, encoding
and output permutation. Only candidate search was replaced by:

- the persistent 679,121-mask production-order cache;
- P=8 bounded page processing;
- adjacent-mask prefix parity deltas;
- parity-only candidate state and scoring;
- exact strict best/runner-up and tie handling.

Nine fixed frames matched decoded bytes, uniqueness, best and runner-up scores,
and the winning permuted candidate. Timing used two warm-up rounds, nine frames
per repeat, nine repeats and alternating execution order.

| Mode | Median ms/block | P95 ms/block | Candidate ms | Blocks/s |
|---|---:|---:|---:|---:|
| Production baseline | 10.104 | 11.035 | 10.014 | 98.972 |
| Combined cached P8 parity | 108.315 | 122.776 | 108.226 | 9.232 |

The combined path delivered only 0.0933x baseline throughput. It was 10.72x
slower. This disproves the earlier informal idea that the isolated 5.99x TEP
cache result and approximately 1.9x parity-only kernel result could be
multiplied into an end-to-end gain.

The underlying reason is architectural: the native DFS loop changes state
in-place and is highly compiler-friendly. Reconstructing adjacent parity
deltas and materializing page candidates duplicates transition work and adds
short-loop/page overhead. This is a useful systems result, not a failed
correctness test.

### 8. Native DFS subtree parallelism produced a positive result

The Phase 8 experiment removed the rejected TEP cache and materialized pages.
It retained production preprocessing/output and split the exact DFS traversal
into dynamically scheduled depth-two subtrees. Each candidate kept its original
production sequence index for deterministic tie resolution.

Nine fixed BCH(127,64), order-4 frames matched decoded bytes, uniqueness, best,
runner-up and the complete winning candidate. With GCC 13.3 and
`-O3 -march=native -pthread`, median full-decoder latency changed from
3.932 ms at baseline to 0.748 ms with eight workers, a 5.26x speedup. A portable
`-march=x86-64` build changed from 8.365 ms to 0.973 ms, an 8.60x speedup.
Sixteen workers were slower than eight on the nine-CPU allocation.

Stateful parity alone was compiler-sensitive and is not retained as an
independent speed claim. The pthread result remained positive under both
compiler targets. Thread creation/join was included in every decoded block.
Address/Undefined sanitizers and ThreadSanitizer passed within the documented
container limitations.

Full methodology, limitations, and machine-readable results are in
`experiments/NATIVE_PARITY_PTHREAD_FINDINGS.md` and
`experiments/results/native_parity_pthread_*_summary.csv`.

### 9. Full-state pthread DFS isolated the source of the gain

The Phase 9 control held subtree partitioning, dynamic scheduling, thread
counts, candidate indices and result reduction constant while changing only the
worker state representation.

At eight workers, the portable build measured 1.528 ms/block for full-state DFS
and 1.081 ms/block for parity-only DFS, versus 8.438 ms baseline. The host-native
build measured 0.864 ms full-state and 0.873 ms parity-only, versus 3.966 ms
baseline. All paths remained bit-exact.

Three full process runs per compiler target gave median full/parity ratios of
1.398 for portable x86-64 and 0.998 for host-native. Thus pthread DFS is
independently valuable; parity-only state is strongly useful in the portable
build but neutral under the native target.

The full-state path passed Address/Undefined sanitizers, ThreadSanitizer and the
complete bounded repository suite. Detailed results are in
`experiments/results/pthread_dfs_ab_*.csv`.

### 10. Exact order-0 stopping is viable only for high-SNR mean work

A 2,000-frame BPSK/AWGN probe skipped exhaustive search only when the order-0
metric reached the absolute `sum(abs(LLR))` bound. This condition is exact for
nonzero LLRs.

Stop rates were 0.0%, 5.2%, 50.8%, and 92.4% at 4, 6, 8, and 10 dB. Mean
end-to-end speedups were 1.00x, 1.05x, 2.01x, and 12.07x. All decoded results
matched exhaustive production decoding.

The result does not improve p95 materially: at 10 dB the remaining 7.6% of
frames still exceed the p95 boundary and execute the full search. It supports a
high-SNR average-throughput direction, but the absolute-bound check is
straightforward and is not itself a publishable novelty.

## Reproduction

Software:

```text
make -C experiments clean test CXX='g++ -march=x86-64'
```

Recorded software environment:

- Linux 6.12.47, x86-64;
- AMD EPYC 9V74;
- G++ 13.3.0;
- C++17 and `-O2`.

GitHub Actions run #64 used Icarus Verilog 12.0 and Yosys 0.33. It built the
upstream regression target, passed the bounded software suite, passed both RTL
simulations, and synthesized P=1,8,16 with zero reported Yosys problems.

Full TEP-delivery reproduction:

```text
make -C experiments benchmark-tep
```

Machine-readable results are under `experiments/results/`.

## Next gated experiment

Combine the exact order-0 precheck with a persistent pthread fallback, then test
a stronger safe subtree bound or a probabilistic stopping rule with quantified
BLER loss. Require p95/p99 gains across realistic SNRs and broader workloads,
and compare against published stopping/discarding OSD methods. Keep RTL
unchanged during this phase.

## Limitations

- Software timing comes from one host and compiler configuration.
- TEP delivery timings are Python implementation measurements and should not be
  presented as compiled-decoder or hardware throughput.
- Fixed-seed integer soft values validate architecture; they are not an
  end-to-end BPSK-AWGN performance study.
- RTL simulation used K=5, R=10, P=4 vectors; production parameters were
  elaborated by generic synthesis but not exhaustively simulated.
- Yosys generic cells are not FPGA LUTs, ASIC area, or post-route timing.
- No power or energy measurement was made.
- The RTL produces per-lane candidates and scores; integrated best/runner-up
  reduction and decoder control are not yet implemented.
- The long stochastic upstream regression binary was built but not executed by
  the bounded workflow.

## Supported claim

> For the tested upstream traversal, parity-only scoring and bounded
> prefix-delta pages preserve candidate order, metrics, ties, and decoded output;
> a multi-cycle parameterized RTL page engine matches deterministic golden pages
> and reduces generic P8 synthesis cells by about 11.5x versus the direct
> combinational realization. A naïve software composition of persistent cache,
> materialized P8 pages and parity-only scoring is bit-exact but 10.72x slower
> than the production decoder and is rejected.

This does not claim a new OSD algorithm, novelty of generator-row flipping,
FPGA acceleration, end-to-end speedup, energy improvement, post-route results,
or silicon results.
