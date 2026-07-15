# Persistent pthread plus exact-stop benchmark

## Question

Does retaining the exact DFS worker threads across blocks improve latency, and
does combining the pool with the exact order-0 stop produce a robust end-to-end
decoder path?

## Implemented experiment

The experiment leaves production OSD and RTL unchanged. An opt-in search hook
compares nine modes:

1. exhaustive production baseline;
2. spawn-per-block full-state DFS with eight workers;
3. persistent full-state DFS with eight workers;
4. spawn-per-block parity DFS with eight workers;
5. persistent parity DFS with eight workers;
6. exact order-0 stop plus spawn full-state fallback;
7. exact order-0 stop plus persistent full-state fallback;
8. exact order-0 stop plus spawn parity fallback; and
9. exact order-0 stop plus persistent parity fallback.

The persistent implementation creates seven background pthreads once; the
decoder caller is the eighth compute worker. A condition-variable generation
handoff wakes the background workers for each fallback search. The 0.220 ms
native and 0.262 ms portable pool-start costs are reported separately and are
excluded from per-block latency.

## Method

- BCH(127,64), OSD order 4;
- BPSK/AWGN at Eb/N0 = 4, 6, 8, and 10 dB;
- 200 deterministic encoded frames per SNR;
- three timed repeats, giving 600 samples per mode and SNR;
- 21,600 timed decodes per compiler target;
- rotating and reversing mode order;
- p50, p95, p99, mean latency, candidate-stage latency, throughput, candidate
  count, exact-stop count, and checksum capture;
- GCC 13.3, C++17, `-O3 -pthread`;
- host-native and portable x86-64 builds.

Every experimental decode matched exhaustive output bytes, uniqueness, best
metric, earliest winning candidate, and (on non-stopped frames) runner-up
metric. Address/Undefined sanitizers, ThreadSanitizer, and the complete bounded
repository suite passed.

## Host-native result

The table shows the strongest parity modes. Times are milliseconds.

| Eb/N0 | Stop rate | Baseline p50/p95/p99 | Exact spawn parity p50/p95/p99 | Exact persistent parity p50/p95/p99 |
|---:|---:|---:|---:|---:|
| 4 dB | 0.0% | 3.920 / 4.118 / 4.503 | 0.860 / 0.986 / 1.081 | 0.755 / 0.913 / 1.027 |
| 6 dB | 3.5% | 3.925 / 4.076 / 4.330 | 0.854 / 0.930 / 1.007 | 0.764 / 0.901 / 1.086 |
| 8 dB | 46.5% | 3.930 / 4.018 / 4.124 | 0.791 / 0.924 / 0.968 | 0.688 / 0.824 / 0.892 |
| 10 dB | 92.0% | 3.916 / 3.983 / 4.047 | 0.022 / 0.853 / 0.913 | 0.023 / 0.866 / 0.971 |

At 8 dB, persistence improves the exact parity hybrid by 15.0% at p50,
12.0% at p95, and 8.6% at p99. At 10 dB, where fallback is rare, persistence
is instead 3.9%, 1.5%, and 5.9% slower at p50, p95, and p99.

## Portable x86-64 result

| Eb/N0 | Stop rate | Baseline p50/p95/p99 | Exact spawn parity p50/p95/p99 | Exact persistent parity p50/p95/p99 |
|---:|---:|---:|---:|---:|
| 4 dB | 0.0% | 8.515 / 10.238 / 11.117 | 1.211 / 1.520 / 1.737 | 1.180 / 1.544 / 1.775 |
| 6 dB | 3.5% | 8.522 / 9.765 / 10.773 | 1.105 / 1.393 / 1.564 | 1.078 / 1.385 / 1.655 |
| 8 dB | 46.5% | 8.467 / 8.913 / 10.093 | 1.021 / 1.259 / 1.384 | 0.923 / 1.220 / 1.340 |
| 10 dB | 92.0% | 8.542 / 9.168 / 9.703 | 0.020 / 1.107 / 1.244 | 0.022 / 1.065 / 1.238 |

At 8 dB, persistence improves the exact parity hybrid by 10.7% at p50,
3.2% at p95, and 3.3% at p99. At 10 dB, it loses 7.0% at p50 but gains 3.9%
at p95 and 0.5% at p99.

## Interpretation

The combined exact-stop plus pthread fallback direction passes. It preserves
exhaustive results, gives a cheap high-SNR fast path, and retains a multi-core
fallback for difficult frames. At 10 dB its parity variants reduce p95 versus
production by roughly 4.7--8.6x and p99 by roughly 4.2--7.8x in the two builds.

Persistence is not a universal win. It is strongest when fallbacks are frequent
or work arrives continuously. When exact stops create long idle intervals, the
condition-variable wake cost and OS scheduling can erase the saved thread
creation cost. Several p99 comparisons reverse sign, and the high-SNR native
spawn-on-miss parity path beats the persistent path.

Parity is again the fastest kernel in these runs, but earlier controlled native
runs found full-state and parity effectively tied. It remains a target- and
workload-selected option rather than a universal replacement.

## Decision

- Keep exact order-0 stopping plus exact pthread DFS fallback as the surviving
  integrated software direction.
- Do not make the sleeping persistent pool the unconditional default.
- Retain spawn-on-miss as the conservative high-SNR policy and persistent
  workers as an opt-in continuous-traffic policy.
- Retain full-state DFS as the correctness/performance control and select parity
  only after target benchmarking.
- Before production integration, test affinity, loaded-system p99, concurrent
  decoder instances, shared versus per-decoder pools, additional codes/orders,
  and actual traffic arrival gaps.
- Keep cached/materialized pages and the rejected subtree bound closed.
- Keep RTL unchanged.

## Reproduction

Native:

```text
g++ -I.. -std=c++17 -O3 -march=native -pthread \
  -DPERSISTENT_POOL_FRAMES=200 -DPERSISTENT_POOL_REPEATS=3 \
  -DPERSISTENT_POOL_WARMUPS=2 persistent_pthread_decoder_benchmark.cc \
  -o persistent_pool_native
./persistent_pool_native native_summary.csv native_raw.csv
```

Replace `-march=native` with `-march=x86-64` for the portable build.

## Limitations

- One host and compiler were tested.
- The 600 samples per row reuse 200 unique channel frames over three repeats.
- Pool startup and teardown are excluded from per-block latency.
- The pool uses sleeping condition variables; spin, affinity, and adaptive idle
  policies were not tested.
- The probe serializes decoder invocations through one global experiment hook;
  concurrent production integration is not implemented.
- No energy, BLER trade-off, real trace, or production API measurement was made.
