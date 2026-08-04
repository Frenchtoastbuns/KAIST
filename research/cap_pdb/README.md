# CAP-PDB research

This directory is the reproducible research record for exact CAP-PDB pruning and
its hardware mapping. It keeps historical experiments separate from the production
R2 RTL under [`experiments/rtl/corrected/r2`](../../experiments/rtl/corrected/r2).

## Current decision

- **Production physical baseline:** normal CAP split-phase R2.
- **Strongest algorithmic challenger:** fused-specialized dual R3.
- **Not selected:** W1-only oracle.
- **Model only:** W1/W2 oracle, gated on sustained four-lane score throughput.

See [STATUS.md](STATUS.md) for the confirmed results, limitations, and next gates.

## Directory map

| Path | Contents |
|---|---|
| [`docs`](docs) | Complete technical history and architecture notes |
| [`wavefront`](wavefront) | Bounded-stale/wavefront source, raw results, and report |
| [`compact_rtl`](compact_rtl) | Parallel compact RTL, trace model, synthesis evidence, and report |
| [`r3`](r3) | Fused-specialized decisive source, 1,000-frame CSVs, audit scripts, and report |

Generated binaries and reproduced output directories are intentionally ignored.
The original upload ZIPs are not committed because their source and evidence are
stored here directly.
