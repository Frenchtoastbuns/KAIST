# Research status

## Current state

- Branch: `research/osd-paged-vector`
- Baseline commit: `e6cfc5b0f71d8e82d6cba2184b1edf0486f64238`
- Current phase: Phase 5 complete — evidence frozen; RTL awaiting toolchain
- Upstream production changes: none
- Known external experiment: `experiments/tep_delivery_modes.py` was described by the researcher but is not present in this repository.

## Verified by source inspection

- The active upstream regression configuration is BCH(127,64), order 4.
- `OrderedStatisticsDecoder::flip(j)` incrementally XORs generator row `G[j]` into the current codeword.
- `metric()` scans the padded codeword for every evaluated candidate.
- The repository Makefile uses C++17 and defaults to Clang/libc++.
- The default branch contained only upstream history when this branch was created.

## Executed validation

On 2026-07-15, the exact branch versions of `osd.hh`, `bitman.hh`,
`sort.hh`, `experiments/Makefile`, and `experiments/osd_smoke_test.cc`
were fetched through the GitHub connector into an isolated Linux workspace.

Command:

```text
make -C experiments clean test CXX='g++ -march=x86-64'
```

Observed result:

- deterministic BCH(15,5), order-1 smoke test compiled with G++ and C++17;
- the decoded word matched the encoded word;
- the uniqueness result was true;
- a second call on the same decoder instance produced the same result, checking traversal restoration;
- process exit status: 0.

The first compile attempt exposed that `osd.hh` relies on standard-library
headers included transitively by its original caller. The smoke test now
includes those dependencies explicitly; the upstream header remains unchanged.

GitHub Actions workflow status has not yet been observed through the connector,
so no CI success is claimed.

## Phase 1 validation

The production decoder now has compile-time opt-in trace hooks. With tracing
undefined, the hooks preprocess to no-ops; the default public decoder API and
candidate traversal remain unchanged.

The exact branch state was built and executed locally with:

```text
make -C experiments clean test CXX='g++ -march=x86-64'
```

Results:

- K=5, order 3: 26 candidates; complete ordered mask list matched Python;
- K=5, order 3 sequence hash: `d1d09a68b627c23a`;
- K=64, order 4: 679,121 candidates;
- K=64, order 4 sequence hash: `9717451b3bb8a575`;
- all masks were within range and within the requested order;
- duplicate count: zero;
- observed unique count equalled the full combinatorial candidate count;
- compiled C++ metrics were recomputed for traced candidates and matched;
- traversal state returned to the zero mask after decoding;
- deterministic smoke and cross-language trace tests exited successfully.

Candidate-content stream hashes recorded by the compiled trace were
`c29e770002372bb8` for the small case and `a274285f16165e46` for the
BCH(127,64) case. These hashes are regression fingerprints, not proofs of
correctness on their own.

Independent GitHub Actions run #34 completed successfully on draft PR #1.
It built the upstream OSD regression target, compiled the Python experiments,
and ran the bounded research tests.

## Phase 2 validation

Separate timing and operation-count builds were used so per-candidate counters
do not contaminate the reported stage timings. The exact branch state was
rebuilt and tested with G++ 13.3.0, `-O2`, and C++17 on an AMD EPYC 9V74 Linux
host. Inputs used a fixed xorshift seed, two warm-ups, and nine measured
BCH(127,64), order-4 blocks.

Observed operation counts per production block:

- candidate/metric evaluations: 679,121;
- metric terms over padded width 128: 86,927,488;
- generator-row flip calls: 1,358,240;
- flip XOR terms over padded width 128: 173,854,720;
- candidate update decisions after the order-0 candidate: 679,120.

Observed timing summary for this host and build only:

- median total: 10,475,002 ns;
- p95 total across nine measured frames: 13,446,053 ns;
- median candidate-search time: 10,381,033 ns;
- median candidate-search share: 99.10%;
- median row-echelon time: 45,217 ns;
- median systematic-conversion time: 34,211 ns;
- median unaccounted timing overhead: 420 ns.

Every measured frame satisfied `sum(stage_times) <= total_time`. These values
establish the candidate stage as the target on this software baseline; they are
not FPGA, post-route, energy, or cross-platform results.

Independent GitHub Actions run #34 completed successfully on draft PR #1.
It built the upstream OSD regression target, compiled the Python experiments,
and ran the bounded research tests.

## Phase 3 validation

An incremental parity-only model now runs from the exact generator matrix,
permuted soft values, base codeword, and permutation prepared by the production
decoder. For every traversal flip it updates only the parity portion and adjusts
the affected systematic metric term in constant work. For every candidate it
rescans only parity positions.

Exactness coverage:

- all 32,768 hard-sign patterns for BCH(15,5), order 3;
- three fixed-seed BCH(127,64), order-4 frames;
- every candidate metric compared with the compiled production metric;
- every systematic and parity candidate bit compared;
- best metric, runner-up metric, tie/uniqueness result, winning candidate and
  unpermuted decoded word compared;
- traversal mask, parity state, and systematic metric restored after search.

BCH(127,64), order-4 parity-only counts:

- parity metric terms: 42,784,623 instead of 86,927,488 padded full-width terms;
- parity flip XOR terms: 85,569,120 instead of 173,854,720 padded full-width terms;
- systematic metric updates: one constant-size update per flip.

A separate candidate-kernel benchmark used the same compile-time recursive
traversal for full-width and parity-only engines. Seven repeats were taken for
each of three fixed contexts. Observed full-width/parity-only median latency
ratios were 1.71x, 1.73x, and 1.92x in the final full-suite run. These ratios
show that the operation reduction survives in this controlled software kernel;
they are not an integrated production-decoder speedup, an FPGA result, or an
energy result.

Independent GitHub Actions run #34 completed successfully on draft PR #1.
It built the upstream OSD regression target, compiled the Python experiments,
and ran the bounded research tests.

## Phase 4 validation

Two exact page models process the production-order TEP stream:

- independent lanes construct each parity candidate from the order-0 parity;
- prefix-delta pages derive edge deltas between adjacent masks and expand
  page-local cumulative parity deltas from a carried boundary state.

For `P = 1, 2, 4, 8, 16`, both modes processed all 679,121 candidates and
matched:

- production mask sequence hash `9717451b3bb8a575`;
- candidate-content/metric hash `c9da0a5ebdc96618`;
- best and runner-up metrics;
- tie/uniqueness result;
- winning candidate and unpermuted production decoded word;
- page count and final partial-page size.

Because 679,121 is one more than a multiple of every tested power-of-two page
width, the final page contained one candidate in all cases.

Maximum modeled live payload bytes, excluding allocator/container metadata:

| P | Independent | Prefix delta |
|---:|---:|---:|
| 1 | 71 | 260 |
| 2 | 142 | 394 |
| 4 | 284 | 662 |
| 8 | 568 | 1,198 |
| 16 | 1,136 | 2,270 |

Sequential software times varied with page overhead and do not establish
parallel speedup. In the final full-suite run, prefix-delta elapsed time fell
from 218.3 ms at P=1 to 190.7 ms at P=16, while independent construction
remained approximately 215-250 ms. These are model timings, not an integrated
production, FPGA, or energy result.

Independent GitHub Actions run #34 completed successfully on draft PR #1.
It built the upstream OSD regression target, compiled the Python experiments,
and ran the bounded research tests.

## Phase 5 completion

The final full-suite execution passed after deterministic initialization was
added for padded matrix and codeword positions. Machine-readable outputs are
stored in `experiments/results/`, and the decision record is in
`FINAL_REPORT.md`.

The evidence supports proceeding with a P=8 parity-only prefix-delta RTL
prototype and synthesizing P=1 and P=16 comparison points. It does not yet
support an FPGA, energy, or integrated production speedup claim.

## Current blocker and next actions

1. Connect this branch to a runner containing Icarus Verilog or Verilator and
   Yosys; none is installed in the current isolated execution environment.
2. Implement the P=8 RTL only after the simulator is available so every RTL
   commit can be checked against the validated software model.
3. Run the long upstream stochastic regression separately; the CI workflow
   compiled its target but intentionally ran only the bounded research suite.
