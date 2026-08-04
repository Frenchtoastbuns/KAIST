# Parallel compact CAP-PDB RTL study

This module preserves the 2026-07-30 state-wide and dual-port compact engines,
testbenches, trace-derived cycle model, synthesis logs, and final report.

The study is evidence, not the production design: the P8 compact engine gained only
3.32% modeled cycles while using substantially more logic and BRAM than normal CAP.

## Software trace audit

```bash
./run_trace_audit.sh 3 /tmp/compact-trace-smoke.csv
```

## RTL synthesis

```bash
npm install
npm run synth -- 8
npm run synth:dual-port
```

YoWASP/Yosys requires a Node release that supports
`--experimental-wasm-exnref`. The historical JavaScript simulation runner is
preserved under `rtl/`, but the current pinned YoWASP package does not expose the
`sim` command. Use the frozen simulation logs or an Icarus/Verilator installation
for replay. See [REPORT.md](REPORT.md) for the frozen results.
