# Bit-exact paged OSD candidate engine: Phase 0-5 report

## Decision

Proceed to an RTL prototype of a parity-only, page-vector candidate and scoring
engine, beginning with page width P=8 and retaining P=16 as the throughput-area
comparison point.

The software evidence supports the architecture strongly enough to justify RTL,
but it does not yet support FPGA, energy, or end-to-end decoder speedup claims.

## Research question

Can an OSD implementation preserve the exact upstream candidate set, order,
metrics, tie behavior, and decoded output while reducing candidate-state work
and bounding the memory required for parallel candidate processing?

The reference is the upstream BCH(127,64), order-4
`CODE::OrderedStatisticsDecoder`.

## Important baseline correction

The upstream decoder already uses incremental generator-row updates.
`flip(j)` XORs row `G[j]` into the current candidate and the nested traversal
restores state while backtracking.

Therefore, generic "parity-delta caching" is not a new contribution relative to
this baseline. The defensible contribution is narrower:

> a bit-exact parity-only scoring decomposition combined with bounded page
> vectorisation and page-local prefix-delta expansion.

## What was established

### 1. Production traversal

Opt-in hooks were added to the actual compiled decoder. With hooks undefined,
the public API and traversal remain unchanged.

The independent Python reference matched the compiled path:

| Case | Candidates | Production/Python sequence hash |
|---|---:|---|
| K=5, O=3 | 26 | `d1d09a68b627c23a` |
| K=64, O=4 | 679,121 | `9717451b3bb8a575` |

The complete small-case ordered mask list matched exactly. The production case
had no duplicates or invalid masks and covered the full combinatorial count.

### 2. Measured bottleneck

On the recorded EPYC/G++ environment, candidate search consumed 99.0% of the
median end-to-end software time for the instrumented decoder.

For one BCH(127,64), order-4 block, the upstream candidate stage performed:

- 679,121 metric evaluations;
- 86,927,488 padded metric terms;
- 1,358,240 generator-row flips;
- 173,854,720 padded flip XOR terms.

This result makes TEP delivery alone a secondary optimization target.

### 3. Parity-only state and scoring

The experimental model:

- stores and updates only the 63 parity positions;
- maintains the systematic contribution by updating one affected term per
  traversal flip;
- scans only parity positions during candidate evaluation;
- preserves the upstream strict best/runner-up comparison.

Exactness checks covered:

- all 32,768 hard-sign patterns for BCH(15,5), order 3;
- three fixed-seed BCH(127,64), order-4 frames;
- every candidate metric and candidate bit;
- best metric, runner-up metric, ties, uniqueness, winning candidate and
  unpermuted decoded word.

For BCH(127,64), order 4, the model reduced:

- metric scan terms from 86,927,488 to 42,784,623;
- flip XOR terms from 173,854,720 to 85,569,120.

In a controlled candidate-kernel comparison using identical traversal control,
the parity-only variant was 1.88x-1.91x faster across the final three recorded
contexts. This is not an integrated production-decoder speedup.

### 4. Bounded pages

Independent-lane construction and prefix-delta expansion were evaluated at
P=1,2,4,8,16.

Every configuration matched:

- all 679,121 candidates;
- production-order mask hash `9717451b3bb8a575`;
- candidate/metric content hash `c9da0a5ebdc96618`;
- best and runner-up metrics;
- tie and uniqueness behavior;
- winning candidate and production decoded word;
- page counts and final-page boundaries.

Modeled maximum live payload, excluding container metadata:

| P | Independent bytes | Prefix-delta bytes |
|---:|---:|---:|
| 1 | 71 | 260 |
| 2 | 142 | 394 |
| 4 | 284 | 662 |
| 8 | 568 | 1,198 |
| 16 | 1,136 | 2,270 |

P=8 is the recommended first RTL point because it exposes meaningful lane
parallelism while keeping modeled prefix-delta state near 1.2 KiB. P=16 should
be synthesized as the area/throughput comparison.

### 5. Padding correctness

Source isolation exposed an uninitialized-read risk: `metric()` and `flip()`
operate over padded width `W`, while the padding of `G` and `codeword` was
not initialized. The branch now zero-initializes only positions `N..W-1` in
both OSD decoder classes. A targeted regression verifies zero matrix, candidate,
and soft padding. All candidate hashes and exactness gates remained stable.

## Reproduction

Focused validation:

```text
make -C experiments clean test CXX='g++ -march=x86-64'
```

Recorded environment:

- Linux 6.12.47, x86-64;
- AMD EPYC 9V74;
- G++ 13.3.0;
- C++17 and `-O2`.

Machine-readable results are under `experiments/results/`.

## RTL architecture to implement next

The initial P=8 design should contain:

1. a bounded TEP page input buffer;
2. adjacent-mask XOR generation;
3. parity-row delta lookup;
4. an eight-lane prefix-XOR expansion network;
5. a page-boundary parity register;
6. direct systematic metric generation from each TEP;
7. eight parity metric reduction lanes;
8. a deterministic best/runner-up and tie reduction;
9. an output record containing best mask, best parity and score.

The RTL testbench must compare every lane against the validated software model,
including partial final pages and ties. Synthesis should compare P=1,8,16 and
report frequency, LUT/ALM use, registers, memory blocks, cycles per page and
estimated throughput.

## Limitations

- Results come from one software host and compiler configuration.
- Fixed-seed integer soft values were used for architecture validation; a later
  benchmark should include reproducible BPSK-AWGN frames and multiple SNRs.
- Page-model timings are sequential software timings, not parallel hardware
  throughput.
- No power or energy measurements were made.
- No Verilog simulator or synthesizer was available in the current execution
  environment.
- The long stochastic upstream `tests/osd_regression_test.cc` was not executed
  in the isolated partial checkout.
- GitHub Actions success was not observed through the connector.

## Claim boundary

The evidence currently supports:

> exact software validation of parity-only metric decomposition and bounded
> prefix-delta pages for the upstream OSD traversal.

It does not yet support:

- a novel OSD decoding algorithm;
- novelty of incremental generator-row flipping;
- FPGA acceleration;
- end-to-end production speedup;
- energy improvement;
- post-route or silicon results.
