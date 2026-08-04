# Fused-specialized dual R3 gate

This directory contains the reconstructed source, four stitched 1,000-frame CSVs,
structural and sanitizer evidence, portable analysis scripts, cycle/memory
sensitivity tables, and the audited decision report.

## Result

- Fused-specialized: PASS the ≤35M operations/frame gate.
- W1-only: reject.
- W1/W2: conditional hardware modelling only; no RTL claim.

## Provenance warning

The historical 45.15M executable and exact counter source were unavailable. The supplied reconstructed specialized solver reproduces the canonical 119,889.055 TEPs/frame result and reports 48.57M operations under its identical four-way counter.

The fused controller is a threshold-decision engine. An early witness means `KEEP`;
its returned value must not be reused as a numerical lower bound. The repository
suite includes the audit correction and large-residual W3 coverage.

## Reproduce

Run a one-frame smoke test:

```bash
./run_full.sh 1 /tmp/osd-r3-smoke
python3 scripts/verify_decisive.py \
  --results-dir /tmp/osd-r3-smoke \
  --frames 1 \
  --expected-structural-checks 4096
```

Run the full four-variant replay:

```bash
./run_full.sh
```

The full replay is intentionally not part of normal CI. CI runs the deterministic
smoke gate; the committed 1,000-frame CSVs can be checked with:

```bash
python3 scripts/verify_decisive.py
```

See [REPORT.md](REPORT.md) for the full interpretation and limitations.
