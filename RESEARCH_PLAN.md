# OSD candidate-engine research plan

## Objective

Determine whether a bit-exact, bounded-memory page-vector candidate and scoring engine can outperform the upstream scalar OSD traversal without changing candidate coverage, ranking, tie behavior, decoded output, or error-correction performance.

## Baseline facts

- Upstream commit: `e6cfc5b0f71d8e82d6cba2184b1edf0486f64238`.
- Decoder: `CODE::OrderedStatisticsDecoder<N,K,O>` in `osd.hh`.
- Reference case: BCH(127,64), order 4.
- Candidate count including order 0: 679,121.
- The upstream traversal is already delta-based: `flip(j)` XORs the current codeword with systematic generator row `G[j]`.
- The open question is therefore not whether generator-row deltas work. It is whether parity-only state, decomposed scoring, and page-level vector expansion produce a better exact architecture.

## Phase 0 — Reproducible baseline

Deliverables:

- focused upstream build;
- deterministic smoke test separate from the long stochastic regression;
- recorded compiler and flags;
- CI that builds the upstream OSD target and runs bounded research tests.

Gate: baseline compiles and deterministic tests pass without changing decoder behavior.

## Phase 1 — Production traversal trace

Deliverables:

- opt-in instrumentation of the actual compiled OSD path;
- `uint64_t` TEP state for K <= 64;
- small-case per-candidate trace;
- K=64/order-4 streaming counts and hashes;
- independent Python traversal;
- exact C++/Python comparison.

Gate: counts, order, masks, hashes, duplicates, invalid masks, and production results match.

## Phase 2 — Cost decomposition

Measure independently:

- reliability sort;
- generator-matrix permutation;
- row-echelon conversion;
- systematic conversion;
- initial encoding;
- row flips;
- metric evaluation;
- best/runner-up update handling.

Gate: component timings reconcile with total time and repeated deterministic trials are stable enough to interpret.

## Phase 3 — Parity-only candidate and metric decomposition

Evaluate:

- systematic metric computed directly from the TEP;
- candidate storage/update limited to parity positions where possible;
- exact comparison with upstream metrics and decisions;
- operation count, memory, latency, and throughput.

Gate: exact equivalence. A negative performance result is valid and must be retained.

## Phase 4 — Paged vector engine

Evaluate page widths `P = 1, 2, 4, 8, 16`:

- independent-lane construction;
- page-local delta/prefix-XOR construction;
- bounded page state and page-boundary restoration;
- exact ranking and tie behavior;
- memory and throughput scaling.

Gate: exact equivalence for deterministic exhaustive small cases and randomized BCH(127,64) frames.

## Phase 5 — Benchmark and decision

Report:

- candidate count and sequence hash;
- median and p95 latency;
- candidates per second;
- peak memory and operation counts;
- page-width scaling;
- speedup or slowdown versus the original;
- compiler and platform metadata;
- limitations and falsified hypotheses.

Gate: all original compatibility checks and research tests pass.

## Phase 6 — RTL, conditional

Proceed only if Phase 4 shows a defensible architectural benefit. Implement a synthesizable SystemVerilog candidate engine, verify it bit-for-bit against the software model, and report simulation/synthesis evidence with the correct qualification.
