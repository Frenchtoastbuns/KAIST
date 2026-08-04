# CAP-PDB research status

**Updated:** 2026-08-04

## Confirmed

Canonical configuration: eBCH(128,64), OSD-4, BPSK-AWGN, 0 dB, 1,000 frozen
frames.

| Result | Confirmed value |
|---|---:|
| Exhaustive fixed-order OSD | 679,121 TEPs/frame |
| Normal CAP-PDB | 354,339 TEPs/frame |
| Compact CAP-PDB | 286,642 TEPs/frame |
| Dual-compact CAP-PDB | 119,889 TEPs/frame |
| Fused-specialized dual work | 25.27M operations/frame |
| Fused-specialized shared work | 62.98M operations/frame |
| Fused-specialized p99 dual work | 33.15M operations/frame |

The decisive fused-specialized replay used the same seed, generator hash, stream
checksum, and per-frame hash sequence for all four variants. It produced zero
decoded-word, winning-metric, or tie mismatches. Relative to the reconstructed
specialized baseline, fused control reduced dual work by 47.96% and shared work by
28.96% without worsening TEP pruning.

The historical 45.15M counter source is missing. The search-equivalent
reconstruction reports 48.57M, so the fused comparison is valid under one recovered
counter but is not a literal rerun of the historical executable.

Bounded-stale execution is also confirmed: batch-4096 incumbent synchronization
added 0.0275% normal, 0.0603% compact, and 0.2860% dual TEP work.

Normal split-phase R2 passed simulation and synthesis. It reduced modeled mean
cycles from 155,481 to 116,848 with 0.61% more LUTs and no additional BRAM.

## Not yet confirmed

- Routed Fmax, power, energy/frame, and post-route equivalence for R2.
- Any fused-dual RTL, synthesis, or place-and-route result.
- The W1/W2 oracle's assumed score throughput, ports, routing, and LUT cost.
- A hardware win for compact or dual over normal split-phase R2.
- Final multi-code, multi-SNR, FER/BER/BLER, and iso-BLER comparison evidence.

Fused early-witness exits are threshold decisions. Only `PRUNE` or `KEEP` is safe
to consume; their early-exit number is not always a reusable admissible bound.

## Next gates

1. Route baseline normal R2 and tagged split-phase R2 under the same Virtex-7,
   Vivado, XDC, clock, and implementation settings.
2. Build an equal-resource cycle/memory model for fused-specialized dual. Include
   group-solver lanes, multiplier update latency, witness scoring, BRAM ports,
   routing, queues, and captured-incumbent tags.
3. Keep W1/W2 oracle modelling-only unless four lanes sustain at most 16
   cycles/check and retain at least 15% throughput/BRAM gain after all overheads.
4. If resources permit one additional algorithm screen, test selective certificate
   escalation (normal → threshold compact → fused dual) on traces only. Do not build
   RTL unless it lowers shared work by at least 15% and preserves exact decisions.
5. Route only the best challenger against split-phase R2, then freeze architecture.
6. Complete cross-code/order/SNR validation and the final FER/BER/BLER and fair
   iso-BLER baselines for the paper.

The immediate paper blocker is step 1. The immediate R3 task is step 2; further
dual-bound redesign is out of scope until that hardware model exists.
