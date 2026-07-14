#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import subprocess
import sys

from tep_reference import summarize


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: compare_tep_trace.py TEP_TRACE_EXECUTABLE")

    executable = pathlib.Path(sys.argv[1]).resolve()
    completed = subprocess.run(
        [str(executable)],
        check=True,
        capture_output=True,
        text=True,
    )

    observed: dict[tuple[int, int], tuple[int, int]] = {}
    for line in completed.stdout.splitlines():
        fields = line.split()
        if not fields or fields[0] != "TRACE":
            continue
        if len(fields) != 6:
            raise AssertionError(f"malformed C++ trace summary: {line}")
        k = int(fields[1])
        order = int(fields[2])
        count = int(fields[3])
        mask_hash = int(fields[4], 16)
        observed[k, order] = count, mask_hash

    expected_cases = {(5, 3), (64, 4)}
    if set(observed) != expected_cases:
        raise AssertionError(
            f"expected summaries for {expected_cases}, observed {set(observed)}"
        )

    for k, order in sorted(expected_cases):
        reference = summarize(k, order)
        count, mask_hash = observed[k, order]
        if count != reference.count:
            raise AssertionError(
                f"count mismatch for k={k}, order={order}: "
                f"C++={count}, Python={reference.count}"
            )
        if mask_hash != reference.mask_hash:
            raise AssertionError(
                f"sequence hash mismatch for k={k}, order={order}: "
                f"C++={mask_hash:016x}, Python={reference.mask_hash:016x}"
            )
        print(
            f"PASS k={k} order={order} count={count} "
            f"sequence_hash={mask_hash:016x}"
        )


if __name__ == "__main__":
    main()
