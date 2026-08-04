#!/usr/bin/env python3
"""Analyze the canonical compact-CAP parallel RTL trace and synthesis sweep."""

from __future__ import annotations

import csv
import math
import re
import statistics
from pathlib import Path


ROOT = Path(__file__).resolve().parent
RESULTS = ROOT / "results" / "cap_pdb_parallel_rtl_2026-07-30"
TRACE = RESULTS / "compact_parallel_trace_1000.csv"
LANES = (1, 2, 4, 8, 16)
SCORER_IIS = (1, 2, 4, 8, 16)


def nearest_rank(values: list[int], fraction: float) -> int:
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, math.ceil(fraction * len(ordered)) - 1))
    return ordered[index]


def distribution(values: list[int]) -> dict[str, float | int]:
    return {
        "mean": statistics.mean(values),
        "median": statistics.median(values),
        "p95": nearest_rank(values, 0.95),
        "p99": nearest_rank(values, 0.99),
        "maximum": max(values),
    }


def final_hierarchy_block(text: str) -> str:
    marker = "=== design hierarchy ==="
    if marker not in text:
        raise ValueError("missing final design hierarchy")
    return text.rsplit(marker, 1)[1].split("Warnings:", 1)[0]


def resource_count(block: str, cell: str) -> int:
    matches = re.findall(rf"^\s*(\d+)\s+{re.escape(cell)}\s*$", block, re.M)
    return int(matches[-1]) if matches else 0


def parse_synthesis_log(path: Path) -> dict[str, int]:
    text = path.read_text()
    if "Found and reported 0 problems." not in text:
        raise ValueError(f"structural check did not pass in {path}")
    lcs = re.findall(r"Estimated number of LCs:\s*(\d+)", text)
    if not lcs:
        raise ValueError(f"missing LC estimate in {path}")
    block = final_hierarchy_block(text)
    luts = sum(resource_count(block, f"LUT{width}") for width in range(1, 7))
    ffs = sum(
        resource_count(block, cell)
        for cell in ("FDRE", "FDSE", "FDCE", "FDPE", "FDRSE", "FDCPE")
    )
    return {
        "estimated_lcs": int(lcs[-1]),
        "luts": luts,
        "ffs": ffs,
        "ramb18e1": resource_count(block, "RAMB18E1"),
        "dsp48e1": resource_count(block, "DSP48E1"),
        "structural_errors": 0,
    }


def write_csv(path: Path, fieldnames: list[str], rows: list[dict]) -> None:
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    rows = list(csv.DictReader(TRACE.open()))
    if len(rows) != 1000:
        raise ValueError(f"expected 1000 trace frames, got {len(rows)}")
    if any(
        int(row["word_mismatch"])
        or int(row["metric_mismatch"])
        or int(row["tie_mismatch"])
        for row in rows
    ):
        raise ValueError("trace contains an exactness mismatch")

    normal_bound = [int(row["normal_bound_cycles"]) for row in rows]
    normal_scoring = [int(row["normal_scoring_calls"]) for row in rows]
    compact_scoring = [int(row["compact_scoring_calls"]) for row in rows]
    allocations = [int(row["compact_allocations"]) for row in rows]
    scoring_savings = [
        normal - compact
        for normal, compact in zip(normal_scoring, compact_scoring)
    ]

    cycle_rows: list[dict] = []
    normal_stats = distribution(normal_bound)
    cycle_rows.append(
        {
            "architecture": "normal_cap_narrow",
            "lanes": 1,
            **normal_stats,
            "mean_extra_vs_normal": 0,
            "aggregate_break_even_scorer_ii": 0,
        }
    )
    for lanes in LANES:
        compact_bound = [int(row[f"cycles_p{lanes}"]) for row in rows]
        stats = distribution(compact_bound)
        extra = [
            compact - normal
            for compact, normal in zip(compact_bound, normal_bound)
        ]
        cycle_rows.append(
            {
                "architecture": "compact_state_wide",
                "lanes": lanes,
                **stats,
                "mean_extra_vs_normal": statistics.mean(extra),
                "aggregate_break_even_scorer_ii": sum(extra)
                / sum(scoring_savings),
            }
        )

    write_csv(
        RESULTS / "compact_parallel_cycle_summary.csv",
        [
            "architecture",
            "lanes",
            "mean",
            "median",
            "p95",
            "p99",
            "maximum",
            "mean_extra_vs_normal",
            "aggregate_break_even_scorer_ii",
        ],
        cycle_rows,
    )

    model_rows: list[dict] = []
    for scorer_ii in SCORER_IIS:
        normal_total = [
            bound + scorer_ii * scoring
            for bound, scoring in zip(normal_bound, normal_scoring)
        ]
        normal_mean = statistics.mean(normal_total)
        model_rows.append(
            {
                "architecture": "normal_cap_narrow",
                "lanes": 1,
                "scorer_ii": scorer_ii,
                **distribution(normal_total),
                "mean_reduction_vs_normal_percent": 0.0,
                "frame_win_rate_percent": 0.0,
                "search_core_frames_per_second_at_100mhz": 100e6
                / normal_mean,
            }
        )
        for lanes in LANES:
            compact_bound = [int(row[f"cycles_p{lanes}"]) for row in rows]
            compact_total = [
                bound + scorer_ii * scoring
                for bound, scoring in zip(compact_bound, compact_scoring)
            ]
            compact_mean = statistics.mean(compact_total)
            model_rows.append(
                {
                    "architecture": "compact_state_wide",
                    "lanes": lanes,
                    "scorer_ii": scorer_ii,
                    **distribution(compact_total),
                    "mean_reduction_vs_normal_percent": 100.0
                    * (normal_mean - compact_mean)
                    / normal_mean,
                    "frame_win_rate_percent": 100.0
                    * sum(
                        compact < normal
                        for compact, normal in zip(compact_total, normal_total)
                    )
                    / len(rows),
                    "search_core_frames_per_second_at_100mhz": 100e6
                    / compact_mean,
                }
            )

    write_csv(
        RESULTS / "compact_parallel_end_to_end_model.csv",
        [
            "architecture",
            "lanes",
            "scorer_ii",
            "mean",
            "median",
            "p95",
            "p99",
            "maximum",
            "mean_reduction_vs_normal_percent",
            "frame_win_rate_percent",
            "search_core_frames_per_second_at_100mhz",
        ],
        model_rows,
    )

    synthesis_rows: list[dict] = []
    phase1 = ROOT / "results" / "cap_pdb_rtl_synthesis_phase1_2026-07-30.csv"
    with phase1.open() as stream:
        for row in csv.DictReader(stream):
            synthesis_rows.append(
                {
                    "architecture": row["engine"],
                    "lanes": 1,
                    "estimated_lcs": int(row["estimated_lcs"]),
                    "luts": int(row["luts"]),
                    "ffs": int(row["ffs"]),
                    "ramb18e1": int(row["ramb18e1"]),
                    "dsp48e1": int(row["dsp48e1"]),
                    "structural_errors": int(row["structural_errors"]),
                    "memory_organization": "narrow_state_addressed",
                }
            )
    for lanes in LANES:
        resources = parse_synthesis_log(
            RESULTS / f"synth_state_wide_p{lanes}.log"
        )
        synthesis_rows.append(
            {
                "architecture": "compact_state_wide",
                "lanes": lanes,
                **resources,
                "memory_organization": "325x512_per_group",
            }
        )
    dual_port = parse_synthesis_log(RESULTS / "synth_dual_port_p2.log")
    synthesis_rows.append(
        {
            "architecture": "compact_dual_read_yosys",
            "lanes": 2,
            **dual_port,
            "memory_organization": "two_read_narrow_yosys_replicated",
        }
    )

    write_csv(
        RESULTS / "compact_parallel_synthesis_summary.csv",
        [
            "architecture",
            "lanes",
            "estimated_lcs",
            "luts",
            "ffs",
            "ramb18e1",
            "dsp48e1",
            "structural_errors",
            "memory_organization",
        ],
        synthesis_rows,
    )

    print(
        "frames=1000",
        f"mean_allocations={statistics.mean(allocations):.3f}",
        f"mean_scoring_savings={statistics.mean(scoring_savings):.3f}",
        f"normal_mean_bound={normal_stats['mean']:.3f}",
    )
    for lanes in LANES:
        cycle = next(
            row
            for row in cycle_rows
            if row["architecture"] == "compact_state_wide"
            and row["lanes"] == lanes
        )
        ii1 = next(
            row
            for row in model_rows
            if row["architecture"] == "compact_state_wide"
            and row["lanes"] == lanes
            and row["scorer_ii"] == 1
        )
        print(
            f"P={lanes}",
            f"mean_bound={cycle['mean']:.3f}",
            f"break_even_ii={cycle['aggregate_break_even_scorer_ii']:.3f}",
            f"ii1_reduction={ii1['mean_reduction_vs_normal_percent']:.3f}%",
            f"ii1_win_rate={ii1['frame_win_rate_percent']:.1f}%",
        )


if __name__ == "__main__":
    main()
