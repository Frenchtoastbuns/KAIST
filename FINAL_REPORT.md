# Bit-exact paged OSD candidate engine: Phase 0-6 report

## Decision

Proceed with an opt-in integration experiment using the sequential P=8
parity-only prefix-delta engine. Keep P=16 as the mandatory throughput/area
comparison and the upstream decoder as the bit-exact oracle.

Do not replace the production candidate loop yet. The current evidence supports
software exactness, bounded state, RTL functional equivalence on golden pages,
and generic synthesis feasibility. It does not support FPGA timing, energy, or
end-to-end decoder acceleration claims.

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

Machine-readable results are under `experiments/results/`.

## Next gated experiment

The next implementation should integrate only sequential P8 behind an opt-in
interface and compare it candidate-by-candidate with the production loop. It
must add deterministic best/runner-up/tie reduction, ready/valid backpressure,
reset/restart checks, and randomized page boundaries. Only after that gate
passes should P=1,8,16 be mapped to a named FPGA and compared using LUTs,
registers, RAMs, Fmax, latency, and throughput.

## Limitations

- Software timing comes from one host and compiler configuration.
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
> combinational realization.

This does not claim a new OSD algorithm, novelty of generator-row flipping,
FPGA acceleration, end-to-end speedup, energy improvement, post-route results,
or silicon results.
