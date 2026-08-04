# Canonical saved-trace replay

This directory adds a deterministic, simulation-only bridge from the validated
C++ eBCH/OSD experiment into the corrected normal/S45 RTL pair. It does not
modify the decoder architecture and does not invoke place-and-route.

Frozen configuration:

- eBCH(128,64), OSD order 4
- BPSK-AWGN, Eb/N0 = 0 dB
- signed seven-bit soft values, scale 32
- seed `5928218492399464753` (`0x5245434f4e5f3131`)
- expected soft-stream checksum `11508365490867720138`
- 1,000 frames

`export_canonical_trace.cc` reproduces the validated reliability ordering,
reliability-ordered Gaussian elimination, systematicized matrix, order-zero
seed and exhaustive order-4 reference. The trace contains exactly the values
consumed by the corrected RTL: row effects, parity-state costs, information
costs, base states, seed result and exhaustive canonical result.

`cap_canonical_replay_pair_top.sv` is a simulation-only wrapper. It observes
build phase and FIFO state hierarchically; the corrected normal and S45 cores
are unchanged. `cap_canonical_replay_verilator.cc` writes one CSV row per frame.
`analyze_canonical_replay.py` preserves all 1,000 rows, computes distributional
statistics and applies the frozen ±5% total-cycle decision gate.
