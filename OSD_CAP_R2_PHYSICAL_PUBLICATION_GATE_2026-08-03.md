# Post-Route Production-R2 vs Tagged Split-R2 Publication Gate Report

**Date:** 3 August 2026  
**Repository:** `Frenchtoastbuns/KAIST`  
**Branch:** `osd-nonblocking-corrected-20260731`  
**Frozen report commit:** `db45b5f63048c3509d0b317883f107610b4b15eb`  
**Status:** **PRE-ROUTE GATES PASSED; PHYSICAL GATE PENDING**

## 1. Decision

Architecture development is frozen. Only the following designs are permitted in this gate:

1. `cap_r2_normal_top` — production residual-order-two normal CAP baseline.
2. `cap_r2_split_normal_top` — tagged split-phase residual-order-two CAP.

The current evidence is sufficient to proceed to place-and-route, but it is not sufficient to claim a physical-design win. The unified algorithm–architecture paper route is therefore **not yet accepted or rejected**.

No further CAP variant, scheduler redesign, PDB change, memory-layout experiment, or dual/compact implementation is authorized. The only permitted repair after routing is one isolated pipeline stage if the query adder tree is the timing limiter, followed by complete frozen-trace revalidation.

## 2. Frozen canonical configuration

- Code: eBCH(128,64)
- OSD order: 4
- Channel: BPSK-AWGN, 0 dB
- Frames: 1,000
- Seed: `5928218492399464753`
- Soft-stream checksum: `11508365490867720138`
- Generator checksum: `15049493467287215415`
- Trace checksum: `332897943003122562`
- Frozen trace SHA-256: `c3cd084c4aa8c457f87d0d72efb8c81c8fc8385b635be7b91117c1a881ae2f5a`
- Tie frames: 8

## 3. Correctness gate

The canonical 1,000-frame RTL replay passed for both architectures:

- decoded-word / canonical TEP mismatches: **0**;
- best-metric mismatches: **0**;
- tie-status mismatches: **0**;
- frame win rate for split phase: **100%**;
- mean scoring issues were effectively unchanged: 297,029.938 versus 297,029.812.

The physical-flow lint now also passes after replacing three Icarus-incompatible part-selects on function-call results in `post_route_tb.sv`. The patch changes only testbench syntax; it does not alter either RTL design.

**Gate status:** PASS at source RTL. Post-route netlist replay remains required.

## 4. Cycle evidence

| Metric | Production R2 | Tagged split R2 | Change |
|---|---:|---:|---:|
| Build cycles, mean | 8,065.000 | 8,065.000 | 0.000% |
| Decode cycles, mean | 147,415.942 | 108,783.227 | -26.207% |
| Total cycles, mean | 155,480.942 | 116,848.227 | **-24.847%** |
| Total cycles, p50 | 166,924.500 | 126,964.000 | -23.938% |
| Total cycles, p95 | 228,476.850 | 198,014.350 | **-13.333%** |
| Total cycles, p99 | 233,881.020 | 204,883.050 | **-12.399%** |
| Maximum total cycles | 238,010 | 210,526 | -11.547% |
| Weighted scorer utilisation | 50.373% | 68.262% | +17.889 points |
| Mean context-wait cycles | 70,809.049 | 3,491.186 | -95.069% |

At equal Fmax, the cycle ratio gives:

\[
\frac{T_{\text{split}}}{T_{\text{base}}}
=\frac{155480.942}{116848.227}
=1.33062.
\]

For at least 15% real throughput improvement:

\[
\frac{F_{\text{split}}}{F_{\text{base}}}
\ge
\frac{1.15}{1.33062}
=0.86426.
\]

The split design may therefore lose no more than **13.574% Fmax**.

**Cycle-level gate status:** PASS.

## 5. Xilinx-7 synthesis evidence

The two frozen tops were synthesized with the same generic Yosys Xilinx-7 flow.

| Resource | Production R2 | Tagged split R2 | Change |
|---|---:|---:|---:|
| Estimated logic cells | 31,124 | 31,366 | **+0.778%** |
| LUT primitives | 34,589 | 34,800 | **+0.610%** |
| FFs | 13,558 | 13,510 | -0.354% |
| RAMB18E1 | 39 | 39 | 0 |
| RAMB36E1 | 1 | 1 | 0 |
| BRAM18 equivalent | **41** | **41** | 0 |
| DSP48 | 0 | 0 | 0 |
| Total mapped cells | 49,903 | 50,056 | +0.307% |

The split design is currently below the 3% LUT-growth limit and preserves the 41-BRAM target.

**Synthesis resource gate status:** PASS. Routed utilization remains required.

## 6. Hard-gate ledger

| Publication gate | Required | Current result | Status |
|---|---:|---:|---|
| Source/canonical exactness | 0 mismatches | 0 / 1,000 | PASS |
| Post-route exactness | 0 mismatches | Not executed | PENDING |
| Mean throughput improvement | >=15% | Conditional on Fmax ratio >=0.86426 | PENDING |
| p99 latency reduction | >=10% | 12.399% in cycles | CONDITIONAL PASS |
| Dynamic energy/frame improvement | >=10% | No SAIF post-route result | PENDING |
| BRAM18 equivalent | 41 both | 41 both after synthesis | CONDITIONAL PASS |
| Routed LUT growth | <=3% | 0.610% after synthesis | CONDITIONAL PASS |
| Timing closure | WNS >=0, TNS=0 | No routed timing report | PENDING |
| Fmax retention | split/base >=0.864 | No routed Fmax | PENDING |

## 7. Physical-flow readiness

The repository now contains and lint-validates:

- identical Vivado implementation flow for both tops;
- common clock and uncertainty constraints;
- routed checkpoint, netlist, SDF, timing, utilization, DRC and route-status reports;
- 1,000-frame functional post-route replay;
- SAIF generation and activity-driven power analysis;
- automatic hard-gate aggregation;
- final decisions `UNIFIED_CAP_PDB_CO_DESIGN_PAPER` or `FOCUSED_CAP_PDB_ALGORITHM_PAPER`.

The physical workflow intentionally requires four paper-grade inputs that are not present in the repository or supplied archives:

1. exact lab Virtex-7 part;
2. exact Vivado release string;
3. common target clock period;
4. clock uncertainty.

It also requires a licensed self-hosted runner labelled `self-hosted`, `linux`, `x64`, and `vivado`. The current execution environment has no Vivado installation or access to that runner. Guessing these inputs would invalidate the comparison.

## 8. Final physical decision rule

Promote tagged split-R2 as the hardware headline only if the routed run satisfies every hard gate:

- zero post-route metric, TEP and tie mismatches;
- WNS >= 0 and TNS = 0 for both designs;
- throughput improvement >=15%;
- p99 latency reduction >=10%;
- dynamic energy/frame improvement >=10%;
- BRAM18-equivalent count exactly 41 for both;
- routed LUT increase <=3%;
- split/base achieved-Fmax ratio >=0.86426.

If every gate passes, the paper route is:

`UNIFIED_CAP_PDB_CO_DESIGN_PAPER`

If any hard gate fails, the paper route is:

`FOCUSED_CAP_PDB_ALGORITHM_PAPER`

No new architecture search follows a failure.

## 9. Current conclusion

The tagged split-phase R2 design passes the source-level exactness, cycle, synthesis-area, BRAM and physical-flow-readiness gates. Its measured cycle reduction is large enough to tolerate a 13.574% routed Fmax loss while retaining the required 15% throughput gain, and its source-level p99 reduction already exceeds the 10% threshold.

The remaining uncertainty is entirely physical: timing closure, routed area, post-route equivalence and SAIF-derived energy. Until those results exist, the correct status is **PHYSICAL GATE PENDING**, not pass or fail.
