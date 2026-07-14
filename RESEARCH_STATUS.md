# Research status

## Current state

- Branch: `research/osd-paged-vector`
- Baseline commit: `e6cfc5b0f71d8e82d6cba2184b1edf0486f64238`
- Current phase: Phase 1 complete — production TEP traversal validated
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

## Immediate next actions

1. Add stage and operation-count instrumentation for Phase 2.
2. Establish deterministic benchmark inputs and warm-up/repeat policy.
3. Measure preprocessing, flip, metric, and update costs separately.
4. Import or replace the unpushed delivery-mode experiment only where it
   remains relevant after production profiling.
