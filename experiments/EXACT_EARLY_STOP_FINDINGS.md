# Exact early-stop viability probe

## Question

Does a provably safe early-stop condition occur often enough to justify further
work on ordered/speculative parallel OSD?

## Minimal mechanism

After normal production preprocessing and order-0 encoding, the probe compares
the order-0 metric with the theoretical maximum `sum(abs(LLR))`. Equality means
the candidate matches every nonzero hard decision. No other codeword can exceed
it, and with nonzero LLRs no different codeword can tie it, so the exhaustive
order-4 search can be skipped without changing decoded output or uniqueness.

If equality is not reached, the probe runs the unmodified exhaustive production
search. This is an exact order-0 precheck, not yet a parallel cancellation
scheduler or a new stopping rule.

## Method

- BCH(127,64), OSD order 4
- random encoded messages over BPSK/AWGN
- Eb/N0 = 4, 6, 8, and 10 dB
- 500 deterministic frames per SNR
- nonzero signed 8-bit soft values
- alternating baseline/probe execution order
- GCC 13.3, `-O3 -march=native`
- full comparison of decoded bytes, uniqueness, best metric, and winner

## Result

| Eb/N0 | Exact stops | Mean candidates | Mean speedup | Baseline p95 | Probe p95 |
|---:|---:|---:|---:|---:|---:|
| 4 dB | 0.0% | 679,121 | 1.00x | 4.519 ms | 4.477 ms |
| 6 dB | 5.2% | 643,807 | 1.05x | 4.567 ms | 4.595 ms |
| 8 dB | 50.8% | 334,128 | 2.01x | 4.473 ms | 4.439 ms |
| 10 dB | 92.4% | 51,614 | 12.07x | 4.408 ms | 4.293 ms |

All 2,000 probe outputs matched exhaustive production decoding.

## Decision

The opportunity is viable for high-SNR average work and throughput: the exact
precheck halves candidate work at 8 dB and removes more than 92% at 10 dB.

It is not sufficient for a tail-latency or publication claim. Even at 10 dB,
7.6% of frames miss the stop, so p95 still contains an exhaustive decode and is
essentially unchanged. The condition is also a straightforward absolute-bound
check rather than a novel algorithm.

Proceed only to a second gate that develops a stronger safe subtree bound or a
carefully quantified probabilistic stopping rule. Combine it with the pthread
fallback and require p95/p99 improvement, broader codes/SNRs, and comparison
with published OSD stopping and discarding methods.
