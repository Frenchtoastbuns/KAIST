# Research status

## Current state

- Branch: `research/osd-paged-vector`
- Baseline commit: `e6cfc5b0f71d8e82d6cba2184b1edf0486f64238`
- Current phase: Phase 0 — reproducible baseline
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

## Immediate next actions

1. Add opt-in instrumentation to the compiled production traversal.
2. Add an independent Python traversal and exact cross-check.
3. Validate small exhaustive configurations and K=64/order-4 streaming counts.
4. Import or replace the unpushed Python delivery-mode experiment only after
   production trace validation.
