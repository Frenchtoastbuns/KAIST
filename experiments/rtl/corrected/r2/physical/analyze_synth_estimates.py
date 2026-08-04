#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path

BASE_CYCLES = 155480.942
SPLIT_CYCLES = 116848.227


def load_json(path: Path):
    return json.loads(path.read_text())


def parse_power(path: Path):
    text = path.read_text(errors="replace")
    patterns = {
        "total_on_chip_w": [
            r"Total On-Chip Power \(W\)\s*\|\s*([0-9.]+)",
            r"Total On-Chip Power.*?([0-9]+(?:\.[0-9]+)?)\s*W",
        ],
        "dynamic_w": [
            r"Dynamic \(W\)\s*\|\s*([0-9.]+)",
            r"Dynamic.*?([0-9]+(?:\.[0-9]+)?)\s*W",
        ],
        "device_static_w": [
            r"Device Static \(W\)\s*\|\s*([0-9.]+)",
            r"Device Static.*?([0-9]+(?:\.[0-9]+)?)\s*W",
        ],
    }
    out = {}
    for key, pats in patterns.items():
        for pat in pats:
            m = re.search(pat, text, flags=re.I | re.S)
            if m:
                out[key] = float(m.group(1))
                break
    return out


def metrics(design, cycles, power):
    fmax_mhz = float(design["estimated_fmax_mhz"])
    latency_s = cycles / (fmax_mhz * 1e6)
    result = {
        "cycles_per_frame": cycles,
        "estimated_fmax_mhz": fmax_mhz,
        "estimated_latency_us": latency_s * 1e6,
        "estimated_throughput_frames_s": 1.0 / latency_s,
    }
    if "dynamic_w" in power:
        result["estimated_dynamic_power_w"] = power["dynamic_w"]
        result["estimated_dynamic_energy_uj_per_frame"] = power["dynamic_w"] * latency_s * 1e6
    if "total_on_chip_w" in power:
        result["estimated_total_power_w"] = power["total_on_chip_w"]
        result["estimated_total_energy_uj_per_frame"] = power["total_on_chip_w"] * latency_s * 1e6
    return result


def pct(new, old):
    return 100.0 * (new / old - 1.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline-dir", required=True, type=Path)
    ap.add_argument("--split-dir", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    baseline = load_json(args.baseline_dir / "synth_metrics.json")
    split = load_json(args.split_dir / "synth_metrics.json")
    common_baseline = (
        baseline["part"], baseline["vivado_version"],
        baseline["target_period_ns"], baseline["clock_uncertainty_ns"],
    )
    common_split = (
        split["part"], split["vivado_version"],
        split["target_period_ns"], split["clock_uncertainty_ns"],
    )
    if common_baseline != common_split:
        raise SystemExit("baseline/split synthesis settings differ")

    baseline_power_path = args.baseline_dir / "power_saif_post_synth.rpt"
    split_power_path = args.split_dir / "power_saif_post_synth.rpt"
    baseline_power = parse_power(baseline_power_path) if baseline_power_path.exists() else {}
    split_power = parse_power(split_power_path) if split_power_path.exists() else {}
    baseline_metrics = metrics(baseline, BASE_CYCLES, baseline_power)
    split_metrics = metrics(split, SPLIT_CYCLES, split_power)

    comparison = {
        "evidence_level": "post_synthesis_estimate",
        "common_settings": {
            "part": baseline["part"],
            "vivado_version": baseline["vivado_version"],
            "target_period_ns": baseline["target_period_ns"],
            "clock_uncertainty_ns": baseline["clock_uncertainty_ns"],
        },
        "baseline": {**baseline, **baseline_metrics, **baseline_power},
        "split": {**split, **split_metrics, **split_power},
        "changes_percent": {
            "cycles": pct(SPLIT_CYCLES, BASE_CYCLES),
            "estimated_fmax": pct(split_metrics["estimated_fmax_mhz"], baseline_metrics["estimated_fmax_mhz"]),
            "estimated_latency": pct(split_metrics["estimated_latency_us"], baseline_metrics["estimated_latency_us"]),
            "estimated_throughput": pct(split_metrics["estimated_throughput_frames_s"], baseline_metrics["estimated_throughput_frames_s"]),
            "luts": pct(split["synth_luts"], baseline["synth_luts"]),
            "ffs": pct(split["synth_ffs"], baseline["synth_ffs"]),
        },
    }
    if "estimated_dynamic_energy_uj_per_frame" in baseline_metrics and "estimated_dynamic_energy_uj_per_frame" in split_metrics:
        comparison["changes_percent"]["estimated_dynamic_energy_per_frame"] = pct(
            split_metrics["estimated_dynamic_energy_uj_per_frame"],
            baseline_metrics["estimated_dynamic_energy_uj_per_frame"],
        )

    (args.out / "synthesis_estimate_comparison.json").write_text(json.dumps(comparison, indent=2) + "\n")

    changes = comparison["changes_percent"]
    rows = [
        ("Cycles/frame", BASE_CYCLES, SPLIT_CYCLES, changes["cycles"]),
        ("Estimated Fmax (MHz)", baseline_metrics["estimated_fmax_mhz"], split_metrics["estimated_fmax_mhz"], changes["estimated_fmax"]),
        ("Estimated latency (us)", baseline_metrics["estimated_latency_us"], split_metrics["estimated_latency_us"], changes["estimated_latency"]),
        ("Estimated throughput (frames/s)", baseline_metrics["estimated_throughput_frames_s"], split_metrics["estimated_throughput_frames_s"], changes["estimated_throughput"]),
        ("Synthesized LUTs", baseline["synth_luts"], split["synth_luts"], changes["luts"]),
        ("Synthesized FFs", baseline["synth_ffs"], split["synth_ffs"], changes["ffs"]),
        ("BRAM18 equivalent", baseline["bram18_equivalent"], split["bram18_equivalent"], pct(split["bram18_equivalent"], baseline["bram18_equivalent"])),
    ]
    if "estimated_dynamic_energy_uj_per_frame" in baseline_metrics and "estimated_dynamic_energy_uj_per_frame" in split_metrics:
        rows.append((
            "Estimated dynamic energy (uJ/frame)",
            baseline_metrics["estimated_dynamic_energy_uj_per_frame"],
            split_metrics["estimated_dynamic_energy_uj_per_frame"],
            changes["estimated_dynamic_energy_per_frame"],
        ))

    md = [
        "# R2 post-synthesis estimate comparison",
        "",
        "**Evidence level:** vendor post-synthesis estimate; not routed timing or measured power.",
        "",
        f"- Part: `{baseline['part']}`",
        f"- Vivado: `{baseline['vivado_version']}`",
        f"- Clock period: {baseline['target_period_ns']} ns",
        f"- Clock uncertainty: {baseline['clock_uncertainty_ns']} ns",
        "",
        "| Metric | Baseline R2 | Split R2 | Change |",
        "|---|---:|---:|---:|",
    ]
    for name, baseline_value, split_value, change in rows:
        md.append(f"| {name} | {baseline_value:.3f} | {split_value:.3f} | {change:+.3f}% |")
    md += [
        "",
        "## Interpretation",
        "",
        "Use these values as *post-synthesis estimates*. Final claims of achieved Fmax, timing closure, routed area and energy require implementation and post-route validation.",
    ]
    (args.out / "synthesis_estimate_comparison.md").write_text("\n".join(md) + "\n")
    print("R2_SYNTH_ESTIMATE_ANALYSIS_PASS")


if __name__ == "__main__":
    main()
