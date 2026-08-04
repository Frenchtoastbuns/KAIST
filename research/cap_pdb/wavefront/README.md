# Bounded-stale wavefront validation

This module preserves the exact source, raw CSVs, checksums, sanitizer sample, and
report for the 2026-07-30 bounded-stale incumbent experiment.

Run a small replay from the repository root:

```bash
research/cap_pdb/wavefront/experiments/run_wavefront_cap_audit.sh \
  3 /tmp/osd-wavefront-smoke 1
```

See [REPORT.md](REPORT.md) for the frozen 1,000-frame result and interpretation.
