#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import statistics
import subprocess
import sys

STAGES = [
    "reliability_prepare",
    "sort",
    "matrix_permute",
    "row_echelon",
    "systematic",
    "soft_permute",
    "initial_encode",
    "candidate_search",
    "output_permute",
]


def percentile_nearest_rank(values: list[int], percentile: float) -> int:
    ordered = sorted(values)
    rank = max(1, (len(ordered) * int(percentile * 100) + 99) // 100)
    return ordered[min(rank - 1, len(ordered) - 1)]


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: analyze_profile.py PROFILE_EXECUTABLE")

    executable = pathlib.Path(sys.argv[1]).resolve()
    completed = subprocess.run(
        [str(executable)],
        check=True,
        capture_output=True,
        text=True,
    )

    totals: list[int] = []
    stage_values: dict[str, list[int]] = {stage: [] for stage in STAGES}
    output_hashes: list[int] = []

    for line in completed.stdout.splitlines():
        fields = line.split()
        if not fields or fields[0] != "PROFILE":
            continue
        if len(fields) != 4 + len(STAGES):
            raise AssertionError(f"malformed profile line: {line}")
        totals.append(int(fields[2]))
        for stage, value in zip(STAGES, fields[3 : 3 + len(STAGES)]):
            stage_values[stage].append(int(value))
        output_hashes.append(int(fields[-1]))

    if len(totals) != 9:
        raise AssertionError(f"expected 9 measured frames, observed {len(totals)}")
    if len(set(output_hashes)) != len(output_hashes):
        raise AssertionError("cumulative output hash did not change every frame")

    total_median = int(statistics.median(totals))
    total_p95 = percentile_nearest_rank(totals, 0.95)
    print(f"PROFILE_SUMMARY total median_ns={total_median} p95_ns={total_p95}")

    for stage in STAGES:
        values = stage_values[stage]
        median_ns = int(statistics.median(values))
        p95_ns = percentile_nearest_rank(values, 0.95)
        print(
            f"PROFILE_STAGE {stage} median_ns={median_ns} p95_ns={p95_ns} "
            f"median_share={median_ns / total_median:.6f}"
        )

    unaccounted = []
    for frame, total in enumerate(totals):
        accounted = sum(stage_values[stage][frame] for stage in STAGES)
        if accounted > total:
            raise AssertionError(
                f"frame {frame} stage sum {accounted} exceeds total {total}"
            )
        unaccounted.append(total - accounted)
    print(
        "PROFILE_OVERHEAD "
        f"median_ns={int(statistics.median(unaccounted))} "
        f"p95_ns={percentile_nearest_rank(unaccounted, 0.95)}"
    )


if __name__ == "__main__":
    main()
