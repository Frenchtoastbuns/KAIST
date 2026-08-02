# R2 physical-design publication gate

This directory contains the frozen post-route comparison between exactly two architectures:

1. `cap_r2_normal_top` — production residual-order-two normal CAP.
2. `cap_r2_split_normal_top` — tagged split-phase residual-order-two CAP.

No other CAP variant, scheduler redesign, group-width change, or PDB formulation is permitted in this gate. The only allowed timing repair is one additional pipeline stage if the routed critical path is the query adder tree; that change must be isolated and revalidated on the frozen trace.

## Required environment

The workflow is manual-only and requires a repository-level self-hosted GitHub Actions runner labelled:

- `self-hosted`
- `linux`
- `x64`
- `vivado`

The runner must expose `vivado`, `xvlog`, `xelab`, and `xsim` on `PATH` and hold a valid licence for the selected Virtex-7 part.

The exact Virtex-7 part, Vivado release, target clock period, and clock uncertainty are workflow inputs. They are intentionally not guessed: the repository and accessible public records do not identify the lab's historical part/version precisely enough for a paper-grade comparison.

## Frozen trace

The workflow regenerates and verifies the canonical 1,000-frame trace before implementation:

- eBCH(128,64), OSD-4, BPSK-AWGN, 0 dB
- seed: `5928218492399464753`
- soft-stream checksum: `11508365490867720138`
- generator checksum: `15049493467287215415`
- trace payload checksum: `332897943003122562`
- binary SHA-256: `c3cd084c4aa8c457f87d0d72efb8c81c8fc8385b635be7b91117c1a881ae2f5a`

## Identical implementation flow

Both tops use the same:

- FPGA part and Vivado version;
- out-of-context clock and uncertainty constraints;
- synthesis, optimisation, placement, physical optimisation, routing, and post-route physical-optimisation directives;
- functional post-route replay;
- SAIF activity window and power-analysis settings.

The implementation scripts emit routed checkpoints, functional netlists, SDF, timing, utilisation, DRC, route-status, power, and machine-readable summaries.

## Hard decision gates

The aggregate job fails unless all of the following hold:

- zero metric, canonical TEP, or tie mismatches over all 1,000 frames;
- WNS >= 0 and TNS = 0 for both designs at the common requested clock;
- split-phase throughput improvement >= 15%;
- split-phase p99 latency reduction >= 10%;
- split-phase dynamic energy/frame improvement >= 10%;
- routed BRAM18-equivalent count remains 41 for both;
- routed LUT growth of split phase is <= 3%;
- split/base achieved-Fmax ratio is >= 0.864.

The power comparison is activity-driven. Vectorless power is retained only as a diagnostic report and cannot satisfy the publication gate.

## Decision

A full pass emits `UNIFIED_CAP_PDB_CO_DESIGN_PAPER`. Any failed hard gate emits `FOCUSED_CAP_PDB_ALGORITHM_PAPER`. The workflow does not silently relax thresholds or launch another architecture search.
