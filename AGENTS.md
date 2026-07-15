# OSD research agent instructions

## Scope and safety

- Treat `master` as the untouched upstream baseline.
- Perform research work only on `research/osd-paged-vector` or a descendant branch.
- Keep production behavior unchanged unless a phase explicitly requires a tested change.
- Prefer reversible, isolated experiment code under `experiments/`.
- Never report a command, test, benchmark, or synthesis result unless it actually ran.

## Baseline

- Primary implementation: `osd.hh`.
- Primary upstream test: `tests/osd_regression_test.cc`.
- Active reference configuration: BCH(127,64), OSD order 4.
- The upstream decoder already updates candidates incrementally through `flip(j)`, which XORs generator row `G[j]`. Do not claim generic parity-delta caching as a new contribution.
- A complete order-4 traversal at K=64 contains 679,121 candidates including the order-0 candidate.

## Build and verification

- Build the focused upstream decoder with:
  `make -C tests osd_regression_test CXX="g++ -march=x86-64"`
- The original regression is stochastic and expensive. Keep it for compatibility, but create deterministic, bounded research tests.
- Run experiment tests through `make -C experiments test` once that target exists.
- Use fixed random seeds and record compiler, flags, host, repeat count, warm-ups, and exact commands.
- For K <= 64, use `uint64_t` or an arbitrary-width Python integer for TEP masks. Never use `uint32_t` for BCH(127,64).
- Validate candidate count, order, uniqueness, masks, sequence hashes, metrics, ties, winning candidate, decoded word, and page boundaries as applicable.
- Preserve raw machine-readable results. Do not commit large generated traces or binaries.

## Research gates

Work sequentially:

1. Establish a reproducible baseline and compiled production trace.
2. Profile preprocessing, flip, metric, and update costs.
3. Evaluate parity-only state and decomposed scoring against the existing incremental baseline.
4. Evaluate bounded-memory page widths 1, 2, 4, 8, and 16.
5. Proceed to RTL only after a bit-exact software model demonstrates a defensible benefit.

A phase passes only when its stated exactness checks and regression checks pass. If an optimization is slower or invalid, preserve the negative result, explain it in `RESEARCH_STATUS.md`, and continue only where the research plan already defines a safe fallback.

## Working record

- Update `RESEARCH_STATUS.md` with the current phase, evidence, commands, results, failures, and next action.
- Keep methodology and decisions in `RESEARCH_PLAN.md`.
- Use a separate commit for each completed and validated research phase.
- Review the final diff for unsupported novelty or performance claims.
