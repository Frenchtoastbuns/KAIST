# Research status

## Current state

- Branch: `research/osd-paged-vector`
- Baseline commit: `e6cfc5b0f71d8e82d6cba2184b1edf0486f64238`
- Current phase: Phase 0 — repository and validation scaffolding
- Upstream production changes: none
- Executed tests: none in the connected GitHub session
- Known external experiment: `experiments/tep_delivery_modes.py` was described by the researcher but is not present in this repository.

## Verified by source inspection

- The active upstream regression configuration is BCH(127,64), order 4.
- `OrderedStatisticsDecoder::flip(j)` incrementally XORs generator row `G[j]` into the current codeword.
- `metric()` scans the padded codeword for every evaluated candidate.
- The repository Makefile uses C++17 and defaults to Clang/libc++.
- The default branch contains only the upstream history at the time this branch was created.

## Immediate next actions

1. Establish CI compilation of the focused OSD regression target.
2. Add a fast deterministic smoke test.
3. Add opt-in production traversal instrumentation.
4. Add an independent Python traversal and exact cross-check.
5. Import or replace the unpushed Python delivery-mode experiment after production trace validation.
