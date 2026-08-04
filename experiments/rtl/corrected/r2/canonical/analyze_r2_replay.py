#!/usr/bin/env python3
import argparse
import csv
import glob
import json
import math
import statistics
from pathlib import Path


def quantile(values, p):
    values = sorted(values)
    if not values:
        return 0.0
    x = (len(values) - 1) * p
    i = int(math.floor(x))
    j = int(math.ceil(x))
    return float(values[i] if i == j else values[i] + (values[j] - values[i]) * (x - i))


def stats(values):
    return {
        "mean": statistics.fmean(values),
        "p50": quantile(values, 0.50),
        "p95": quantile(values, 0.95),
        "p99": quantile(values, 0.99),
        "min": min(values),
        "max": max(values),
    }


def integer(rows, name):
    return [int(row[name], 0) for row in rows]


def floating(rows, name):
    return [float(row[name]) for row in rows]


def renamed_row(row):
    result = {}
    for key, value in row.items():
        if key.startswith("s45_"):
            result["split_r2_" + key[4:]] = value
        else:
            result[key] = value
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--glob", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    rows = []
    for path in sorted(glob.glob(args.glob)):
        with open(path, newline="") as handle:
            rows.extend(csv.DictReader(handle))
    rows.sort(key=lambda row: int(row["frame"]))

    frames = [int(row["frame"]) for row in rows]
    if frames != list(range(1000)):
        raise SystemExit(f"frame coverage mismatch: {len(rows)} rows")
    mismatch_frames = sum(int(row["errors"]) != 0 for row in rows)
    if mismatch_frames:
        raise SystemExit(f"{mismatch_frames} mismatch frames")

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    renamed = [renamed_row(row) for row in rows]
    with (out / "r2_replay_per_frame.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(renamed[0]))
        writer.writeheader()
        writer.writerows(renamed)

    result = {
        "frames": 1000,
        "mismatch_frames": mismatch_frames,
        "tie_frames": sum(int(row["expected_tie"]) for row in rows),
        "architectures": {},
    }

    for label, prefix in (("baseline_r2", "normal"), ("split_phase_r2", "s45")):
        build = integer(rows, f"{prefix}_build_cycles")
        decode = integer(rows, f"{prefix}_decode_cycles")
        total = integer(rows, f"{prefix}_total_cycles")
        issues = integer(rows, f"{prefix}_issues")
        result["architectures"][label] = {
            "build_cycles": stats(build),
            "decode_cycles": stats(decode),
            "total_cycles": stats(total),
            "scoring_issues": stats(issues),
            "bound_rows": stats(integer(rows, f"{prefix}_bound_rows")),
            "prune_rate": stats(floating(rows, f"{prefix}_prune_rate")),
            "scorer_utilisation_weighted": sum(issues) / (4.0 * sum(decode)),
            "scorer_utilisation_per_frame": stats(floating(rows, f"{prefix}_utilisation")),
            "context_wait_cycles": stats(integer(rows, f"{prefix}_context_wait")),
            "fifo_max_occupancy": stats(integer(rows, f"{prefix}_fifo_max")),
            "fifo_full_cycles": stats(integer(rows, f"{prefix}_fifo_full_cycles")),
            "fifo_empty_cycles": stats(integer(rows, f"{prefix}_fifo_empty_cycles")),
            "max_bound_waiters": stats(integer(rows, f"{prefix}_max_bound_waiters")),
            "build_phases": {
                "normal_dp": stats(integer(rows, f"{prefix}_build_normal_dp")),
                "right_dp": stats(integer(rows, f"{prefix}_build_right_dp")),
                "expansion": stats(integer(rows, f"{prefix}_build_expansion")),
                "triangular_dp": stats(integer(rows, f"{prefix}_build_triangular")),
                "prefix": stats(integer(rows, f"{prefix}_build_prefix")),
            },
        }

    baseline = result["architectures"]["baseline_r2"]
    split = result["architectures"]["split_phase_r2"]
    baseline_total = baseline["total_cycles"]["mean"]
    split_total = split["total_cycles"]["mean"]
    hardest = sorted(rows, key=lambda row: int(row["normal_total_cycles"]), reverse=True)[:100]
    hard_baseline = statistics.fmean(int(row["normal_total_cycles"]) for row in hardest)
    hard_split = statistics.fmean(int(row["s45_total_cycles"]) for row in hardest)

    result["comparison"] = {
        "split_over_baseline_total_cycle_ratio": split_total / baseline_total,
        "split_total_cycle_change_percent": (split_total / baseline_total - 1.0) * 100.0,
        "split_decode_cycle_change_percent": (
            split["decode_cycles"]["mean"] / baseline["decode_cycles"]["mean"] - 1.0
        ) * 100.0,
        "split_build_cycle_change_percent": (
            split["build_cycles"]["mean"] / baseline["build_cycles"]["mean"] - 1.0
        ) * 100.0,
        "split_frame_win_rate": sum(
            int(row["s45_total_cycles"]) < int(row["normal_total_cycles"]) for row in rows
        ) / 1000.0,
        "hardest_10_percent": {
            "baseline_mean_total_cycles": hard_baseline,
            "split_mean_total_cycles": hard_split,
            "split_change_percent": (hard_split / hard_baseline - 1.0) * 100.0,
        },
    }

    # The experiment changes only the query engine. Both sides must retain the
    # frozen production R2 builder schedule.
    if any(
        int(row["normal_build_cycles"]) != 8065 or
        int(row["s45_build_cycles"]) != 8065
        for row in rows
    ):
        raise SystemExit("split-phase experiment changed the frozen 8,065-cycle R2 builder")

    utilisation = split["scorer_utilisation_weighted"]
    if split_total <= 125000 and utilisation >= 0.65:
        decision = "SPLIT_PHASE_STRONG"
    elif split_total <= 132000 and utilisation >= 0.65:
        decision = "SPLIT_PHASE_PASS"
    elif split_total <= 139500:
        decision = "SPLIT_PHASE_MARGINAL"
    else:
        decision = "KILL_SPLIT_PHASE_UNDER_10_PERCENT_GAIN"
    result["decision"] = decision
    (out / "r2_split_phase_replay_summary.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n"
    )

    lines = [
        "# Tagged split-phase R2 CAP canonical replay",
        "",
        f"**Decision:** `{result['decision']}`",
        "",
        f"- Frames: 1,000; mismatches: {mismatch_frames}; tie frames: {result['tie_frames']}",
        f"- Split-phase total-cycle change: {result['comparison']['split_total_cycle_change_percent']:+.3f}%",
        f"- Split-phase decode-cycle change: {result['comparison']['split_decode_cycle_change_percent']:+.3f}%",
        f"- Split-phase frame win rate: {100.0 * result['comparison']['split_frame_win_rate']:.1f}%",
        "",
        "| Metric | Baseline R2 | Split-phase R2 |",
        "|---|---:|---:|",
    ]
    for label, key in (
        ("Build mean", "build_cycles"),
        ("Decode mean", "decode_cycles"),
        ("Total mean", "total_cycles"),
    ):
        lines.append(
            f"| {label} | {baseline[key]['mean']:.3f} | {split[key]['mean']:.3f} |"
        )
    for label, percentile in (("Total p50", "p50"), ("Total p95", "p95"), ("Total p99", "p99")):
        lines.append(
            f"| {label} | {baseline['total_cycles'][percentile]:.3f} | {split['total_cycles'][percentile]:.3f} |"
        )
    lines += [
        "",
        f"- Weighted scorer utilisation: baseline {100.0 * baseline['scorer_utilisation_weighted']:.3f}%, split-phase {100.0 * split['scorer_utilisation_weighted']:.3f}%",
        f"- Mean scoring issues: baseline {baseline['scoring_issues']['mean']:.3f}, split-phase {split['scoring_issues']['mean']:.3f}",
        f"- Mean bound rows: baseline {baseline['bound_rows']['mean']:.3f}, split-phase {split['bound_rows']['mean']:.3f}",
        f"- Hardest-10% total-cycle change: {result['comparison']['hardest_10_percent']['split_change_percent']:+.3f}%",
        "",
        "Per-frame results are preserved in `r2_replay_per_frame.csv`.",
    ]
    (out / "r2_split_phase_replay_summary.md").write_text("\n".join(lines) + "\n")

    print(json.dumps(result["comparison"], indent=2, sort_keys=True))
    print("R2_CANONICAL_REPLAY_AGGREGATE_PASS")


if __name__ == "__main__":
    main()
