# Recorded Phase 0-4 results

These CSV files record the final full-suite execution on 2026-07-15.

Environment:

- Linux 6.12.47, x86-64;
- AMD EPYC 9V74;
- G++ 13.3.0;
- C++17, `-O2 -W -Wall -Wextra -pedantic`;
- focused command: `make -C experiments clean test CXX='g++ -march=x86-64'`.

The profile used two warm-ups and nine measured fixed-seed BCH(127,64),
order-4 frames. The candidate-kernel benchmark used seven repeats for each of
three fixed contexts. Page timings are single sequential model executions;
they do not represent parallel hardware throughput.

The values are host- and implementation-specific. They are evidence for this
software prototype, not FPGA post-route, silicon, energy, or cross-platform
results. GitHub Actions success was not observed through the connector, and
the original long stochastic upstream regression was not executed in this
isolated partial checkout.
