# Research status

## Current state

- Branch: `research/osd-paged-vector`
- Baseline commit: `e6cfc5b0f71d8e82d6cba2184b1edf0486f64238`
- Current phase: Phase 2 complete — candidate search bottleneck measured
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

GitHub Actions workflow status has still not been observed through the
connector, so no CI success is claimed.

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

GitHub Actions workflow status has still not been observed through the
connector, so no CI success is claimed.

## Immediate next actions

1. Implement an exact parity-only candidate/scoring reference alongside the
   unchanged upstream baseline.
2. Validate every candidate metric, winning candidate, tie result, and decoded
   word on exhaustive small cases and deterministic BCH(127,64) frames.
3. Measure operation reduction before implementing page widths greater than 1.
4. Retain the external TEP delivery-mode experiment only as a secondary memory
   study; production profiling shows delivery alone is not the main cost.
