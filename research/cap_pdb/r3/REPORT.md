# R3 decisive 1,000-frame gate

**Date:** 2026-08-03

**Scope:** one controlled R3 experiment; no new redesigns and no RTL implementation.

## Decision

**Fused per-cardinality control passes the requested algorithmic gate.**

The fused specialized solver averaged **25,274,706.067 operations/frame**, with p95 **31,213,154.050**, p99 **33,154,714.710**, and maximum **34,205,262.000**. It therefore satisfies the requested **≤35M mean operations/frame** gate; even its p99 and maximum remain below 35M.

Its evaluated TEP count was not worsened: **119,882.676 TEPs/frame** versus **119,889.055** for specialized dual, a difference of **-6.379 TEPs/frame** in favor of fused control.

The correct next action is therefore:

> Proceed to R3 hardware modelling for fused-specialized dual, while retaining normal split-R2 place-and-route as the frozen physical baseline.

The W1-only oracle is rejected. The W1/W2 oracle remains a **conditional hardware-model candidate**, not the R3 winner and not ready for RTL.

## Critical provenance limitation

The archived project history reports **45.15M operations/frame** for the optimized specialized dual solver, but the original executable/source revision and exact counter definitions are not present in the available checkpoint or GitHub branches.

I reconstructed the documented exact specializations and historical control policy:

- direct residual-weight-1 solver;
- exact residual-weight-2 pair solver;
- exact incremental residual-weight-3 XOR-pair solver;
- four updates with the historical prune-only per-update stop.

This reconstruction exactly reproduces the archived canonical search result of **119,889.055 TEPs/frame**, but its identical shared counter reports **48.572M operations/frame**, not 45.15M. The 7.6% difference is not silently rescaled. Consequently, this experiment proves the fused contribution against a search-equivalent reconstructed specialized solver under one identical counter, but it is not a literal rerun of the missing historical 45.15M binary.

## Frozen experiment

- Code: eBCH(128,64)
- OSD order: 4
- Channel: BPSK-AWGN, 0 dB
- Frames: 1,000 deterministic canonical frames
- Seed: `5928218492399464753`
- Generator hash: `15049493467287215415`
- Final stream checksum: `11508365490867720138`
- Frame hashes: identical across all four variants
- Word mismatches: 0 for every variant
- Winning-metric mismatches: 0 for every variant
- Tie mismatches: 0 for every variant

## Operation results

“Dual solver operations” are the identical counters inside the residual-weight-specialized group solvers. “Total primitive operations” use the shared accounting system defined below.

| Variant | Dual mean | Dual p95 | Dual p99 | Total mean | Total p95 | Total p99 |
|---|---:|---:|---:|---:|---:|---:|
| Specialized dual (reconstructed old control) | 48,571,934.021 | 62,164,653.850 | 64,061,479.820 | 88,654,544.908 | 140,379,512.600 | 153,439,976.680 |
| Fused + specialized dual | 25,274,706.067 | 31,213,154.050 | 33,154,714.710 | 62,980,679.434 | 101,895,819.550 | 113,655,936.830 |
| Fused + exact W1 oracle | 31,949,368.529 | 34,912,641.400 | 34,984,644.630 | 60,606,388.904 | 80,493,353.750 | 83,609,346.490 |
| Fused + exact W1/W2 oracle | 4,629,514.292 | 5,124,964.000 | 5,127,876.000 | 50,863,072.198 | 55,390,113.850 | 55,774,477.720 |

### Main comparisons

- Fused-specialized versus specialized:
  - dual solver operations: **47.96% lower**;
  - shared total primitive operations: **28.96% lower**;
  - TEPs: **0.01% lower**, effectively unchanged.
- Fused-W1 versus fused-specialized:
  - shared total primitive operations: only **3.77% lower**;
  - therefore it fails the 15% oracle gate.
- Fused-W1/W2 versus fused-specialized:
  - shared total primitive operations: **19.24% lower**;
  - it passes the scalar 15% gate, but only after explicitly charging its internal exact enumeration.

## TEPs and internal oracle checks

| Variant | TEP mean | TEP p95 | TEP p99 | Oracle checks mean | Oracle checks p95 | Oracle checks p99 |
|---|---:|---:|---:|---:|---:|---:|
| Specialized dual | 119,889.055 | 255,747.350 | 303,600.510 | 0.000 | 0.000 | 0.000 |
| Fused + specialized | 119,882.676 | 255,747.350 | 303,600.510 | 0.000 | 0.000 | 0.000 |
| Fused + W1 | 41,151.210 | 41,820.650 | 42,259.400 | 277,942.593 | 506,402.050 | 541,157.540 |
| Fused + W1/W2 | 7,453.413 | 2,368.000 | 2,615.330 | 649,995.113 | 714,221.550 | 716,969.600 |

The W1/W2 result must not be described as “only 7,453 candidates/frame.” It evaluates approximately **649,995.113 exact low-cardinality completions internally per frame**, including **30,811.870 W1 checks** and **619,183.243 W2 checks**. It largely relocates exhaustive pair work into the bound engine.

The rare maximum of 679,121 external TEPs is retained in the tail statistics; it was not removed as an outlier.

## Shared primitive-operation accounting

Every variant uses the same accounting expression:

```text
total = specialized dual-solver operations
      + compact-table lookups
      + exact-oracle primitive operations
      + dual-witness primitive operations
      + 64 × external scoring calls
      + bound checks
```

An exact residual candidate is charged for:

- selected-row information additions;
- packed exact-state XORs;
- 64 parity metric terms;
- final comparison.

Dual witnesses are charged with the same exact-score formula. This prevents W1/W2 enumeration from disappearing behind a low external TEP count.

## Structural admissibility validation

The archived deterministic gate performed **2,560 unique checks**:

- 512 randomized specialized-solver checks against the brute-force sparse group solver;
- 256 randomized subtree states × four configurations × two threshold cases = 2,048 bound checks;
- zero-sum multiplier perturbations were included in group-solver trials;
- returned witnesses were checked against their computed objectives.

All checks passed.

### Post-gate audit correction

Fused early-witness exits are safe threshold decisions, but the returned number is
not always a reusable numerical lower bound. On a witness exit the controller means
`KEEP`; it does not mean that every unprocessed residual cardinality is bounded by
the returned value. Hardware and downstream code must therefore consume the
decision (`PRUNE` or `KEEP`) together with the captured incumbent, not cache the
early-exit number as an admissible bound.

The repository version expands the structural suite to **4,096 checks**:

- the original 512 mixed W1/W2/W3 group-solver comparisons;
- 512 additional large-residual W3 comparisons with 25–64 remaining rows;
- 256 subtree states × four configurations × three incumbent cases = 3,072
  threshold-decision checks.

The corrected safety assertion is:

```text
PRUNE implies exact subtree minimum > captured incumbent
```

The non-fused numerical path is still separately checked for ordinary lower-bound
admissibility.

## Cycle-and-memory-aware W1/W2 gate

The exact W2 oracle requires all 2,016 pair signatures:

- raw signature capacity: **129,024 bits = 16,128 bytes**;
- conservative minimum banking allowance: **8 additional RAMB18 equivalents**.

The sensitivity model uses 13 group lanes, four exact-score lanes, the same measured per-frame counters, and explicit exact-score latency. It compares throughput per BRAM against fused-specialized using the conservative 41-BRAM R2 baseline.

The latency column below is a sustained-throughput assumption for four exact-score
lanes, not a measured RTL latency.

| Packed exact score cycles/check | Mean-cycle reduction | Throughput/BRAM gain | 15% gate |
|---:|---:|---:|---:|
| 8 | 46.32% | 55.87% | Pass |
| 16 | 28.72% | 17.38% | Pass |
| 32 | 10.91% | -6.08% | Fail |

Therefore W1/W2 is worth carrying into hardware modelling only under this hard condition:

> A packed exact pair score must sustain approximately **≤16 cycles/check** with four scorer lanes while fitting within the +8-BRAM18 allowance and retaining at least 15% throughput-per-BRAM improvement.

At 32 cycles/check it fails the area-normalized gate. No oracle RTL should be built before the hardware model confirms the ≤16-cycle point.

## Variant decisions

| Variant | Decision | Reason |
|---|---|---|
| Specialized dual | Control | Search-equivalent reconstructed baseline; 48.57M under the recovered counter |
| Fused + specialized dual | **Proceed** | 25.27M mean, 33.15M p99, unchanged TEPs, exact |
| Fused + exact W1 | **Reject** | Only 3.77% shared-work gain; not a robust 15% win |
| Fused + exact W1/W2 | **Conditional modelling only** | 19.24% scalar gain, but ~650k internal checks and +16KB/+8 BRAM18 |

## Build, sanitizer and CI status

Passed locally for the archived gate:

- warning-clean C++17 `-O2 -Wall -Wextra -pedantic` build;
- four-mode local CI smoke with the same seed/hash and 2,560 structural checks;
- ASan + UBSan build and one canonical frame for every mode;
- leak detection and halt-on-error enabled;
- no sanitizer findings.

A remote GitHub Actions run had **not** been performed when this report was
generated. The repository now contains a dedicated smoke workflow and portable
reproduction scripts; remote CI status must still be checked on the publishing
commit before any RTL claim.

## Final answer to the narrow question

> **Yes. Fused per-cardinality control materially improves the real specialized low-cardinality algorithmic structure, not merely the obsolete generic solver.**

Under the recovered identical counter it reduces mean specialized solver work from **48.57M to 25.27M operations/frame**, with zero output differences and no TEP degradation. The requested ≤35M decision gate passes decisively.

The historical 45.15M binary itself could not be rerun because it is missing. That provenance gap should remain explicit in any paper or project-history update.
