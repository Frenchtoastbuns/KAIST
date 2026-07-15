# Native parity and pthread DFS findings

## Scope

This experiment changes only the software candidate-search stage. It leaves RTL
out and uses the production `OrderedStatisticsDecoder<127,64,4>` preprocessing
and output path through the opt-in search hook in `osd.hh`.

The benchmark compares the unmodified depth-first OSD search against:

- a single-thread stateful parity-only traversal; and
- dynamically scheduled pthread workers over independent depth-two DFS
  subtrees, using either the full 128-byte candidate state or parity-only
  delta state.

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

Both pthread paths used 1,358,176 generator-row applications per block. The
full-state path processed 128 terms per candidate/update; parity-only processed
64. Thread creation and joining are included in measured decoder latency.
Address and Undefined Behavior Sanitizers passed with leak detection disabled
because the container runs under ptrace. ThreadSanitizer completed without a
reported data race.

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
| Baseline | 1 | 8.438 ms | 8.742 ms | 118.5 | 1.00x |
| Native parity | 1 | 7.354 ms | 8.029 ms | 136.0 | 1.15x |
| Full-state pthread | 2 | 5.017 ms | 6.079 ms | 199.3 | 1.68x |
| Full-state pthread | 4 | 2.658 ms | 3.096 ms | 376.2 | 3.17x |
| Full-state pthread | 8 | 1.528 ms | 1.768 ms | 654.3 | **5.52x** |
| Full-state pthread | 16 | 1.571 ms | 1.700 ms | 636.5 | 5.37x |
| Parity pthread | 2 | 3.185 ms | 4.085 ms | 314.0 | 2.65x |
| Parity pthread | 4 | 1.812 ms | 2.053 ms | 552.0 | 4.66x |
| Parity pthread | 8 | 1.081 ms | 1.232 ms | 924.8 | **7.80x** |
| Parity pthread | 16 | 1.103 ms | 1.237 ms | 906.3 | 7.65x |

### Host-native build

Compiler flags: `-O3 -march=native -pthread`

| Mode | Threads | Median total | p95 total | Blocks/s | Speedup |
|---|---:|---:|---:|---:|---:|
| Baseline | 1 | 3.966 ms | 4.108 ms | 252.1 | 1.00x |
| Native parity | 1 | 5.394 ms | 5.704 ms | 185.4 | 0.74x |
| Full-state pthread | 2 | 2.256 ms | 2.524 ms | 443.2 | 1.76x |
| Full-state pthread | 4 | 1.248 ms | 1.410 ms | 801.1 | 3.18x |
| Full-state pthread | 8 | 0.864 ms | 0.909 ms | 1,157.0 | **4.59x** |
| Full-state pthread | 16 | 1.010 ms | 1.209 ms | 990.1 | 3.93x |
| Parity pthread | 2 | 2.223 ms | 2.366 ms | 449.8 | 1.78x |
| Parity pthread | 4 | 1.317 ms | 1.367 ms | 759.5 | 3.01x |
| Parity pthread | 8 | 0.873 ms | 1.071 ms | 1,145.5 | **4.54x** |
| Parity pthread | 16 | 0.965 ms | 1.111 ms | 1,036.6 | 4.11x |

The eight-worker A/B was repeated in three full benchmark processes. The median
full-state/parity latency ratio was 1.398 in the portable build, meaning parity
was about 40% faster. The host-native median ratio was 0.998, meaning the two
kernels were effectively tied. Individual native ratios ranged from 0.990 to
1.054.

## Interpretation

The pthread DFS direction remains viable independently of parity-only state.
Eight workers are the best tested point on a nine-CPU allocation. Sixteen
workers oversubscribe the allocation and lose performance.

Stateful parity is not a robust cross-compiler optimization. It wins clearly in
the portable pthread build but provides no repeatable eight-worker benefit in
the host-native build. Native ISA optimization accelerates the full 128-byte
loop enough to erase the parity-only work reduction. The defensible result is
pthread DFS acceleration; parity-only state should remain an optional,
target-benchmarked kernel rather than the default source of the speedup claim.

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
