#!/usr/bin/env python3

import argparse
import csv
from pathlib import Path

VARIANTS = ["specialized", "fused_specialized", "fused_w1", "fused_w1w2"]
EXPECTED_CHECKSUM = '11508365490867720138'
EXPECTED_GENERATOR_HASH = '15049493467287215415'
EXPECTED_SEED = '5928218492399464753'


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Verify the frozen R3 CSV set")
    parser.add_argument(
        "--results-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "results",
    )
    parser.add_argument("--frames", type=int, default=1000)
    parser.add_argument("--expected-structural-checks", type=int, default=2560)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    frame_hashes = None
    for variant in VARIANTS:
        path = args.results_dir / f"{variant}_{args.frames}.csv"
        with path.open(newline="") as stream:
            rows = list(csv.DictReader(stream))
        assert len(rows) == args.frames, (variant, len(rows))
        assert [int(row["frame"]) for row in rows] == list(range(args.frames)), variant
        if args.frames == 1000:
            assert rows[-1]["stream_checksum"] == EXPECTED_CHECKSUM, (
                variant,
                rows[-1]["stream_checksum"],
            )
        assert sum(int(row["word_mismatch"]) for row in rows) == 0
        assert sum(int(row["metric_mismatch"]) for row in rows) == 0
        assert sum(int(row["tie_mismatch"]) for row in rows) == 0
        hashes = [row["frame_hash"] for row in rows]
        if frame_hashes is None:
            frame_hashes = hashes
        else:
            assert hashes == frame_hashes, variant
        assert max(int(row["structural_checks"]) for row in rows) == (
            args.expected_structural_checks
        )
        print(
            f"PASS {variant}: frames={args.frames} "
            f"checksum={rows[-1]['stream_checksum']} mismatches=0/0/0"
        )
    print(f"PASS common frame hashes: {len(frame_hashes or [])}")
    print(f"Frozen seed={EXPECTED_SEED} generator_hash={EXPECTED_GENERATOR_HASH}")


if __name__ == "__main__":
    main()
