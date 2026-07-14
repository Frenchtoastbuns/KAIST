# Recorded Phase 0-6 results

These CSV files record the final software execution and the RTL validation
available on 2026-07-15.

Software environment:

- Linux 6.12.47, x86-64;
- AMD EPYC 9V74;
- G++ 13.3.0;
- C++17, `-O2 -W -Wall -Wextra -pedantic`;
- focused command: `make -C experiments clean test CXX='g++ -march=x86-64'`.

The profile used two warm-ups and nine measured fixed-seed BCH(127,64),
order-4 frames. The candidate-kernel benchmark used seven repeats for each of
three fixed contexts. Page timings are single sequential model executions;
they do not represent parallel hardware throughput.

RTL validation used Icarus Verilog 12.0 and Yosys 0.33 in GitHub Actions.
Both combinational and sequential engines passed 128 deterministic Python
golden-vector pages at K=5, R=10, P=4, including partial pages. Generic Yosys
synthesis—not FPGA mapping—reported:

| Architecture | P | Cycles/page | Generic cells | Peak Yosys memory |
|---|---:|---:|---:|---:|
| Combinational baseline | 8 | 0 | 252,104 | 1,167.50 MB |
| Sequential | 1 | 128 | 11,480 | 280.84 MB |
| Sequential | 8 | 128 | 21,890 | 525.73 MB |
| Sequential | 16 | 128 | 33,894 | 1,203.20 MB |

For the production K=64, R=63 parameters, the sequential latency is
K+R+1 = 128 cycles per page. These generic cell counts establish architecture
scaling only. They are not LUT/ALM, frequency, post-route, power, energy, or
silicon results.

GitHub Actions run #64 passed the full focused workflow on draft PR #1. The
workflow built the upstream OSD regression target, ran the bounded research
suite, checked both RTL implementations, and synthesized P=1,8,16. It did not
execute the original long stochastic regression binary.


## Isolated TEP delivery benchmark

Command:

```text
python3 experiments/tep_delivery_modes.py --k 64 --order 4 \
  --page-size 4096 --blocks 200 --warmup-blocks 2 --repeats 5 \
  --output-dir experiments/results
```

The benchmark consumed all 679,121 masks in every block using a lightweight
checksum. Execution order rotated between repeats. Sequence count and hash were
validated against the production-order Python reference before timing.
`tracemalloc` memory probes ran separately from timing.

| Delivery mode | Median ms/block | P95 ms/block | Median TEP/s | Live payload |
|---|---:|---:|---:|---:|
| Regenerate per block | 116.484 | 120.279 | 5.830 M | 5,432,968 B |
| Persistent full cache, warm | 19.437 | 20.834 | 34.939 M | 5,432,968 B |
| Paged streaming, P=4096 | 146.974 | 185.113 | 4.621 M | 32,768 B |
| Single-TEP streaming | 119.883 | 131.906 | 5.665 M | 8 B |

The persistent cache had a separately measured median cold-build cost of
98.490 ms and occupied 5,432,968 bytes. Warm delivery was 5.99x faster than
regeneration. Paged streaming reduced live mask payload by 99.40% but was
1.26x slower in this Python implementation. Single streaming was 1.03x slower
than regeneration while retaining only one mask payload.

These timings isolate Python TEP generation/delivery plus checksum consumption.
They do not include candidate construction, scoring, decoder integration, FPGA
execution, or energy.
