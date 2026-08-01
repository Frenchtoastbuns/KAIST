#!/usr/bin/env python3
import argparse
import csv
import glob
import json
import math
import statistics
from pathlib import Path


def q(values, p):
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
        "p50": q(values, 0.5),
        "p95": q(values, 0.95),
        "p99": q(values, 0.99),
        "min": min(values),
        "max": max(values),
    }


def ints(rows, name):
    return [int(row[name], 0) for row in rows]


def floats(rows, name):
    return [float(row[name]) for row in rows]


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
    mismatch = sum(int(row["errors"]) != 0 for row in rows)
    if mismatch:
        raise SystemExit(f"{mismatch} mismatch frames")

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with (out / "canonical_replay_per_frame.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    result = {
        "frames": 1000,
        "mismatch_frames": mismatch,
        "tie_frames": sum(int(row["expected_tie"]) for row in rows),
        "resource_note": (
            "No BRAM or area comparison is attached to this replay. The earlier "
            "normal synthesis retained an unused compact table and is invalid for "
            "production-memory comparison."
        ),
    }
    for arch in ("normal", "s45"):
        build = ints(rows, f"{arch}_build_cycles")
        decode = ints(rows, f"{arch}_decode_cycles")
        total = ints(rows, f"{arch}_total_cycles")
        issues = ints(rows, f"{arch}_issues")
        result[arch] = {
            "build_cycles": stats(build),
            "decode_cycles": stats(decode),
            "total_cycles": stats(total),
            "scoring_issues": stats(issues),
            "bound_rows": stats(ints(rows, f"{arch}_bound_rows")),
            "prune_rate": stats(floats(rows, f"{arch}_prune_rate")),
            "scorer_utilisation_weighted": sum(issues) / (4.0 * sum(decode)),
            "scorer_utilisation_per_frame": stats(floats(rows, f"{arch}_utilisation")),
            "context_wait_cycles": stats(ints(rows, f"{arch}_context_wait")),
            "fifo_max_occupancy": stats(ints(rows, f"{arch}_fifo_max")),
            "fifo_full_cycles": stats(ints(rows, f"{arch}_fifo_full_cycles")),
            "fifo_empty_cycles": stats(ints(rows, f"{arch}_fifo_empty_cycles")),
            "max_bound_waiters": stats(ints(rows, f"{arch}_max_bound_waiters")),
            "build_phases": {
                "normal_dp": stats(ints(rows, f"{arch}_build_normal_dp")),
                "right_dp": stats(ints(rows, f"{arch}_build_right_dp")),
                "expansion": stats(ints(rows, f"{arch}_build_expansion")),
                "triangular_dp": stats(ints(rows, f"{arch}_build_triangular")),
                "prefix": stats(ints(rows, f"{arch}_build_prefix")),
            },
        }

    nmean = result["normal"]["total_cycles"]["mean"]
    smean = result["s45"]["total_cycles"]["mean"]
    ratio = smean / nmean
    delta = (ratio - 1.0) * 100.0
    decode_gain = (
        result["normal"]["decode_cycles"]["mean"]
        - result["s45"]["decode_cycles"]["mean"]
    )
    build_target = result["normal"]["build_cycles"]["mean"] + decode_gain
    hardest = sorted(rows, key=lambda row: int(row["normal_total_cycles"]), reverse=True)[:100]
    hard_n = statistics.fmean(int(row["normal_total_cycles"]) for row in hardest)
    hard_s = statistics.fmean(int(row["s45_total_cycles"]) for row in hardest)
    result["comparison"] = {
        "s45_over_normal_total_cycle_ratio": ratio,
        "s45_total_cycle_change_percent": delta,
        "s45_frame_win_rate": sum(
            int(row["s45_total_cycles"]) < int(row["normal_total_cycles"])
            for row in rows
        ) / 1000.0,
        "s45_decode_cycle_change_percent": (
            result["s45"]["decode_cycles"]["mean"]
            / result["normal"]["decode_cycles"]["mean"]
            - 1.0
        ) * 100.0,
        "s45_build_cycle_change_percent": (
            result["s45"]["build_cycles"]["mean"]
            / result["normal"]["build_cycles"]["mean"]
            - 1.0
        ) * 100.0,
        "hardest_10_percent": {
            "normal_mean_total_cycles": hard_n,
            "s45_mean_total_cycles": hard_s,
            "s45_change_percent": (hard_s / hard_n - 1.0) * 100.0,
        },
        "s45_break_even_build_target": build_target,
        "s45_current_build_mean": result["s45"]["build_cycles"]["mean"],
        "s45_builder_reduction_required": max(
            0.0, result["s45"]["build_cycles"]["mean"] - build_target
        ),
    }
    if ratio < 0.95:
        decision = "RETAIN_S45_PERFORMANCE_CANDIDATE"
    elif ratio <= 1.05:
        decision = "NORMAL_DEFAULT_S45_COMPILED_BOUND_ONLY"
    else:
        decision = "KILL_S45_ACTIVE_PERFORMANCE_ARCHITECTURE"
    result["decision"] = decision

    (out / "canonical_replay_summary.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n"
    )
    lines = [
        "# Canonical saved-trace RTL replay",
        "",
        f"**Decision:** `{decision}`",
        "",
        f"- Frames: 1,000; mismatches: {mismatch}; tie frames: {result['tie_frames']}",
        f"- S45 total-cycle change: {delta:+.3f}%",
        f"- S45 frame win rate: {100.0 * result['comparison']['s45_frame_win_rate']:.1f}%",
        f"- Hardest-10% total-cycle change: {result['comparison']['hardest_10_percent']['s45_change_percent']:+.3f}%",
        "- Resource comparison intentionally omitted: the earlier normal top retained unused compact memory.",
        "",
        "| Metric | Normal | S45 |",
        "|---|---:|---:|",
    ]
    for label, key in (
        ("Build mean", "build_cycles"),
        ("Decode mean", "decode_cycles"),
        ("Total mean", "total_cycles"),
    ):
        lines.append(
            f"| {label} | {result['normal'][key]['mean']:.3f} | {result['s45'][key]['mean']:.3f} |"
        )
    for label, percentile in (("Total p50", "p50"), ("Total p95", "p95"), ("Total p99", "p99")):
        lines.append(
            f"| {label} | {result['normal']['total_cycles'][percentile]:.3f} | {result['s45']['total_cycles'][percentile]:.3f} |"
        )
    lines += [
        "",
        f"- Weighted scorer utilisation: normal {100.0 * result['normal']['scorer_utilisation_weighted']:.3f}%, S45 {100.0 * result['s45']['scorer_utilisation_weighted']:.3f}%",
        f"- Mean scoring issues: normal {result['normal']['scoring_issues']['mean']:.3f}, S45 {result['s45']['scoring_issues']['mean']:.3f}",
        f"- Mean prune rate: normal {100.0 * result['normal']['prune_rate']['mean']:.3f}%, S45 {100.0 * result['s45']['prune_rate']['mean']:.3f}%",
        f"- S45 break-even build target: {build_target:.3f} cycles; required reduction: {result['comparison']['s45_builder_reduction_required']:.3f} cycles",
        "",
        "Per-frame results are preserved in `canonical_replay_per_frame.csv`.",
    ]
    (out / "canonical_replay_summary.md").write_text("\n".join(lines) + "\n")
    print(json.dumps(result["comparison"], indent=2, sort_keys=True))
    print("CANONICAL_REPLAY_AGGREGATE_PASS")
    print("DECISION=" + decision)


if __name__ == "__main__":
    main()
