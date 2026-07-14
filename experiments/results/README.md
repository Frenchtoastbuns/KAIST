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
