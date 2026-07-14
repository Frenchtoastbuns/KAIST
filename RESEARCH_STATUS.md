# Research status

## Current state

- Branch: `research/osd-paged-vector`
- Draft PR: #1
- Baseline commit: `e6cfc5b0f71d8e82d6cba2184b1edf0486f64238`
- Current phase: Phase 6 complete — software, TEP-delivery, and RTL evidence frozen
- Upstream production integration: not started
- Latest focused CI before the TEP benchmark addition: GitHub Actions run #70, passed

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

RTL validation is encoded in `.github/workflows/osd-research.yml`. Run #64:

- built the upstream OSD regression target;
- ran the bounded C++ and Python suite;
- passed both RTL simulations;
- synthesized sequential P=1,8,16;
- reported zero Yosys problems.

Machine-readable outputs are in `experiments/results/`.

## Next gated phase

1. Integrate only the P8 sequential candidate engine behind an opt-in boundary;
   keep the upstream decoder path as the oracle.
2. Compare every integrated candidate, score, best/runner-up update, tie, and
   decoded word on fixed and randomized frames.
3. Add deterministic best/runner-up hardware reduction and backpressure tests.
4. Map P=1,8,16 to a named FPGA and report LUTs, registers, RAMs, Fmax, latency,
   and throughput.
5. Run post-route power or board measurements before any energy claim.
6. Run the long stochastic upstream regression separately before proposing a
   non-draft production PR.
