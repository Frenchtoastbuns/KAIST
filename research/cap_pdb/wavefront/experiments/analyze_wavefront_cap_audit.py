#!/usr/bin/env python3
import argparse
import csv
import glob
import math
import os


def batch_from_name(path):
    return int(os.path.basename(path).removeprefix("raw_batch_").removesuffix(".csv"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    rows = []
    baseline = {}
    for path in sorted(args.inputs, key=batch_from_name):
        batch = batch_from_name(path)
        with open(path, newline="") as handle:
            data = [line for line in handle if not line.startswith("#")]
        for row in csv.DictReader(data):
            method = row["method"]
            if method == "classical_osd":
                continue
            teps = float(row["unique_teps_per_frame"])
            calls = float(row["scoring_calls_per_frame"])
            # The frozen batch-1 CSV predates explicit synchronization
            # instrumentation.  In that immediate-update implementation every
            # scoring call is conservatively counted as one incumbent-dependent
            # step.
            syncs = float(row.get("incumbent_syncs_per_frame") or calls)
            if batch == 1:
                baseline[method] = teps
            rows.append({
                "batch": batch,
                "method": method,
                "frames": int(row["frames"]),
                "unique_teps_per_frame": teps,
                "scoring_calls_per_frame": calls,
                "bound_checks_per_frame": float(row["bound_checks_per_frame"]),
                "pdb_lookups_per_frame": float(row["pdb_lookups_per_frame"]),
                "compact_lookups_per_frame": float(row["compact_lookups_per_frame"]),
                "dual_queries_per_frame": float(row["dual_queries_per_frame"]),
                "dual_dp_transitions_per_frame": float(
                    row["dual_dp_transitions_per_frame"]
                ),
                "incumbent_syncs_per_frame": syncs,
                "candidate_span_proxy": syncs + 4,
                "tep_overhead_vs_batch1_percent": 0.0,
                "word_mismatches": int(row["word_mismatches"]),
                "metric_mismatches": int(row["metric_mismatches"]),
                "tie_mismatches": int(row["tie_mismatches"]),
            })

    for row in rows:
        base = baseline[row["method"]]
        row["tep_overhead_vs_batch1_percent"] = 100.0 * (
            row["unique_teps_per_frame"] / base - 1.0
        )

    fields = list(rows[0])
    with open(args.output, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
