# Native parity and pthread DFS findings

## Scope

This experiment changes only the software candidate-search stage. It leaves RTL
out and uses the production `OrderedStatisticsDecoder<127,64,4>` preprocessing
and output path through the opt-in search hook in `osd.hh`.

The benchmark compares the unmodified depth-first OSD search against:

- a single-thread stateful parity-only traversal; and
- dynamically scheduled pthread workers over independent depth-two DFS
  subtrees.

The parallel implementation does not cache or page TEP masks. Each candidate is
assigned its original production traversal index so that merging workers keeps
the production tie-breaking rule.

## Correctness

All modes passed exact comparison on nine deterministic BCH(127,64), order-4
frames. Each mode visited 679,121 candidates. Validation compared:

- decoded bytes and uniqueness;
- best and runner-up metrics; and
- the complete winning permuted candidate, including the earliest winner when
  metrics tie.

The pthread path used 1,358,176 parity-row applications per block. Thread
creation and joining are included in the measured decoder latency. Address and
Undefined Behavior Sanitizers passed with leak detection disabled because the
container runs under ptrace. ThreadSanitizer completed without a reported data
race.

## Benchmark setup

- CPU: AMD EPYC 9V74, nine logical CPUs exposed to the container
- Compiler: GCC 13.3.0
- Workload: nine fixed soft-input frames per repeat
- Warm-up: two rounds
- Repeats: nine, with mode order reversed on alternating repeats
- Statistic: median and p95 across the nine per-repeat mean ns/block values

### Portable x86-64 build

Compiler flags: `-O3 -march=x86-64 -pthread`

| Mode | Threads | Median total | p95 total | Blocks/s | Speedup |
|---|---:|---:|---:|---:|---:|
| Baseline | 1 | 8.365 ms | 8.490 ms | 119.5 | 1.00x |
| Native parity | 1 | 7.211 ms | 7.363 ms | 138.7 | 1.16x |
| pthread | 2 | 2.920 ms | 3.001 ms | 342.5 | 2.87x |
| pthread | 4 | 1.584 ms | 1.633 ms | 631.4 | 5.28x |
| pthread | 8 | 0.973 ms | 1.062 ms | 1,027.9 | **8.60x** |
| pthread | 16 | 1.033 ms | 1.203 ms | 967.8 | 8.10x |

### Host-native build

Compiler flags: `-O3 -march=native -pthread`

| Mode | Threads | Median total | p95 total | Blocks/s | Speedup |
|---|---:|---:|---:|---:|---:|
| Baseline | 1 | 3.932 ms | 3.969 ms | 254.3 | 1.00x |
| Native parity | 1 | 5.336 ms | 5.428 ms | 187.4 | 0.74x |
| pthread | 2 | 1.969 ms | 2.121 ms | 507.8 | 2.00x |
| pthread | 4 | 1.119 ms | 1.162 ms | 893.4 | 3.51x |
| pthread | 8 | 0.748 ms | 0.816 ms | 1,336.6 | **5.26x** |
| pthread | 16 | 0.844 ms | 0.993 ms | 1,185.0 | 4.66x |

## Interpretation

The pthread direction remains viable. Eight workers are the best tested point
on a nine-CPU allocation. Sixteen workers oversubscribe the allocation and lose
performance.

Stateful parity by itself is not a robust optimization. It wins by 16% in the
portable build but loses by 36% relative to the host-native baseline because
native ISA optimization accelerates the original 128-byte candidate loop more
strongly. The parallel result remains positive under both compiler targets.

These measurements do not rescue the earlier cached/paged combined design: this
experiment deliberately removes TEP-cache and page overhead. It establishes a
promising independent software path, not a final production result.

## Limits and next checks

The result is from one host, one code/order, and synthetic deterministic soft
inputs. The experiment creates workers for every decoded block and does not pin
threads. Before production integration, test a persistent worker pool, CPU
affinity, realistic channel traces, additional code lengths/orders, throughput
under multiple concurrent decoder instances, and tail latency under system
load.
