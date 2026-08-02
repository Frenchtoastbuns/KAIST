#!/usr/bin/env python3
"""Aggregate the frozen baseline-R2 versus split-R2 physical-design gate."""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
from pathlib import Path
from statistics import mean


def percentile(values: list[float], q: float) -> float:
    if not values:
        raise ValueError("empty percentile input")
    ordered = sorted(values)
    position = (len(ordered) - 1) * q
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def load_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    if len(rows) != 1000:
        raise SystemExit(f"{path}: expected 1000 rows, found {len(rows)}")
    frames = [int(row["frame"]) for row in rows]
    if sorted(frames) != list(range(1000)) or len(set(frames)) != 1000:
        raise SystemExit(f"{path}: frame coverage is not exactly 0..999")
    if any(int(row["errors"]) != 0 for row in rows):
        raise SystemExit(f"{path}: post-route replay mismatch")
    return rows


def parse_dynamic_power(path: Path) -> float:
    text = path.read_text(errors="replace")
    patterns = [
        r"\|\s*Dynamic\s*\(W\)\s*\|\s*([0-9]+(?:\.[0-9]+)?)",
        r"Dynamic\s*\(W\)\s*[:|]?\s*([0-9]+(?:\.[0-9]+)?)",
        r"Total Dynamic Power\s*\(W\)\s*[:|]?\s*([0-9]+(?:\.[0-9]+)?)",
    ]
    for pattern in patterns:
        match = re.search(pattern, text, flags=re.IGNORECASE)
        if match:
            value = float(match.group(1))
            if value <= 0.0:
                raise SystemExit(f"{path}: non-positive dynamic power")
            return value
    raise SystemExit(f"{path}: could not parse SAIF-annotated dynamic power")


def architecture(route_path: Path, csv_path: Path, power_path: Path) -> dict:
    route = json.loads(route_path.read_text())
    rows = load_csv(csv_path)
    cycles = [float(row["total_cycles"]) for row in rows]
    build = [float(row["build_cycles"]) for row in rows]
    decode = [float(row["decode_cycles"]) for row in rows]
    fmax_mhz = float(route["estimated_fmax_mhz"])
    target_period_ns = float(route["target_period_ns"])
    dynamic_w = parse_dynamic_power(power_path)
    mean_cycles = mean(cycles)
    p99_cycles = percentile(cycles, 0.99)
    frames_per_second = fmax_mhz * 1.0e6 / mean_cycles
    mean_latency_us = mean_cycles / fmax_mhz
    p99_latency_us = p99_cycles / fmax_mhz
    # SAIF power is reported at the identical constrained clock retained in
    # each routed checkpoint. Compare energy at that common feasible clock;
    # do not mix common-clock power with each design's estimated Fmax.
    common_clock_latency_us = mean_cycles * target_period_ns / 1000.0
    energy_uj = dynamic_w * common_clock_latency_us
    return {
        "route": route,
        "mismatch_frames": 0,
        "mean_build_cycles": mean(build),
        "mean_decode_cycles": mean(decode),
        "mean_total_cycles": mean_cycles,
        "p50_total_cycles": percentile(cycles, 0.50),
        "p95_total_cycles": percentile(cycles, 0.95),
        "p99_total_cycles": p99_cycles,
        "max_total_cycles": max(cycles),
        "fmax_mhz": fmax_mhz,
        "mean_latency_us_at_fmax": mean_latency_us,
        "p99_latency_us_at_fmax": p99_latency_us,
        "frames_per_second_at_fmax": frames_per_second,
        "common_clock_mean_latency_us": common_clock_latency_us,
        "dynamic_power_w_at_common_clock": dynamic_w,
        "dynamic_energy_per_frame_uj_at_common_clock": energy_uj,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline-dir", type=Path, required=True)
    parser.add_argument("--split-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    baseline = architecture(
        args.baseline_dir / "route_metrics.json",
        args.baseline_dir / "postroute_sim/postroute_1000.csv",
        args.baseline_dir / "power_saif.rpt",
    )
    split = architecture(
        args.split_dir / "route_metrics.json",
        args.split_dir / "postroute_sim/postroute_1000.csv",
        args.split_dir / "power_saif.rpt",
    )

    b_route = baseline["route"]
    s_route = split["route"]
    if b_route["part"] != s_route["part"] or b_route["vivado_version"] != s_route["vivado_version"]:
        raise SystemExit("baseline and split did not use the same part/Vivado version")
    if b_route["target_period_ns"] != s_route["target_period_ns"] or b_route["clock_uncertainty_ns"] != s_route["clock_uncertainty_ns"]:
        raise SystemExit("baseline and split did not use identical clock constraints")

    throughput_gain = split["frames_per_second_at_fmax"] / baseline["frames_per_second_at_fmax"] - 1.0
    p99_reduction = 1.0 - split["p99_latency_us_at_fmax"] / baseline["p99_latency_us_at_fmax"]
    energy_reduction = 1.0 - split["dynamic_energy_per_frame_uj_at_common_clock"] / baseline["dynamic_energy_per_frame_uj_at_common_clock"]
    lut_growth = float(s_route["routed_luts"]) / float(b_route["routed_luts"]) - 1.0
    fmax_ratio = split["fmax_mhz"] / baseline["fmax_mhz"]

    gates = {
        "zero_mismatches": baseline["mismatch_frames"] == 0 and split["mismatch_frames"] == 0,
        "baseline_timing_closed": float(b_route["wns_ns"]) >= 0.0 and abs(float(b_route["tns_ns"])) < 1e-9,
        "split_timing_closed": float(s_route["wns_ns"]) >= 0.0 and abs(float(s_route["tns_ns"])) < 1e-9,
        "throughput_gain_ge_15_percent": throughput_gain >= 0.15,
        "p99_reduction_ge_10_percent": p99_reduction >= 0.10,
        "energy_reduction_ge_10_percent": energy_reduction >= 0.10,
        "baseline_bram_is_41": int(b_route["bram18_equivalent"]) == 41,
        "split_bram_is_41": int(s_route["bram18_equivalent"]) == 41,
        "split_lut_growth_le_3_percent": lut_growth <= 0.03,
        "split_fmax_ratio_ge_0_864": fmax_ratio >= 0.864,
    }
    passed = all(gates.values())
    decision = "UNIFIED_CAP_PDB_CO_DESIGN_PAPER" if passed else "FOCUSED_CAP_PDB_ALGORITHM_PAPER"

    result = {
        "baseline": baseline,
        "split_phase": split,
        "comparisons": {
            "throughput_gain_fraction": throughput_gain,
            "p99_latency_reduction_fraction": p99_reduction,
            "dynamic_energy_reduction_fraction_at_common_clock": energy_reduction,
            "routed_lut_growth_fraction": lut_growth,
            "fmax_ratio": fmax_ratio,
        },
        "gates": gates,
        "all_hard_gates_pass": passed,
        "decision": decision,
    }

    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "physical_gate.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    lines = [
        "# Baseline R2 versus tagged split-phase R2 physical gate",
        "",
        f"- Part: `{b_route['part']}`",
        f"- Vivado: `{b_route['vivado_version']}`",
        f"- Common target period: {b_route['target_period_ns']:.4f} ns",
        f"- Decision: `{decision}`",
        "",
        "| Metric | Baseline R2 | Split-phase R2 |",
        "|---|---:|---:|",
        f"| Fmax (MHz) | {baseline['fmax_mhz']:.3f} | {split['fmax_mhz']:.3f} |",
        f"| WNS (ns) | {float(b_route['wns_ns']):.4f} | {float(s_route['wns_ns']):.4f} |",
        f"| TNS (ns) | {float(b_route['tns_ns']):.4f} | {float(s_route['tns_ns']):.4f} |",
        f"| Routed LUTs | {b_route['routed_luts']} | {s_route['routed_luts']} |",
        f"| Routed FFs | {b_route['routed_ffs']} | {s_route['routed_ffs']} |",
        f"| BRAM18 equivalent | {b_route['bram18_equivalent']} | {s_route['bram18_equivalent']} |",
        f"| Mean total cycles | {baseline['mean_total_cycles']:.3f} | {split['mean_total_cycles']:.3f} |",
        f"| Mean latency at Fmax (us) | {baseline['mean_latency_us_at_fmax']:.3f} | {split['mean_latency_us_at_fmax']:.3f} |",
        f"| p99 latency at Fmax (us) | {baseline['p99_latency_us_at_fmax']:.3f} | {split['p99_latency_us_at_fmax']:.3f} |",
        f"| Frames/s at Fmax | {baseline['frames_per_second_at_fmax']:.3f} | {split['frames_per_second_at_fmax']:.3f} |",
        f"| Dynamic power at common clock (W) | {baseline['dynamic_power_w_at_common_clock']:.6f} | {split['dynamic_power_w_at_common_clock']:.6f} |",
        f"| Dynamic energy/frame at common clock (uJ) | {baseline['dynamic_energy_per_frame_uj_at_common_clock']:.6f} | {split['dynamic_energy_per_frame_uj_at_common_clock']:.6f} |",
        "",
        f"Throughput gain: **{throughput_gain*100:.3f}%**  ",
        f"p99 latency reduction: **{p99_reduction*100:.3f}%**  ",
        f"Dynamic energy/frame reduction at the common clock: **{energy_reduction*100:.3f}%**  ",
        f"Routed LUT growth: **{lut_growth*100:.3f}%**  ",
        f"Fmax ratio: **{fmax_ratio:.6f}**",
        "",
        "## Hard gates",
        "",
    ]
    lines.extend(f"- {'PASS' if value else 'FAIL'} — `{name}`" for name, value in gates.items())
    (args.out / "physical_gate.md").write_text("\n".join(lines) + "\n")

    print(json.dumps(result["comparisons"], indent=2, sort_keys=True))
    for name, value in gates.items():
        print(f"GATE {'PASS' if value else 'FAIL'} {name}")
    print(f"DECISION={decision}")
    if not passed:
        raise SystemExit(2)
    print("R2_PHYSICAL_GATE_PASS")


if __name__ == "__main__":
    main()
