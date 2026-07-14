#!/usr/bin/env python3

from __future__ import annotations

import argparse
import random
from pathlib import Path

K = 5
R = 10
P = 4
LLR_W = 8
SCORE_W = 24
CASES = 128


def pack(values: list[int], width: int) -> int:
    result = 0
    mask = (1 << width) - 1
    for index, value in enumerate(values):
        result |= (value & mask) << (index * width)
    return result


def parity_delta(rows: list[int], selected: int) -> int:
    result = 0
    for index, row in enumerate(rows):
        if (selected >> index) & 1:
            result ^= row
    return result


def signed_bits(value: int, width: int) -> int:
    return value & ((1 << width) - 1)


def generate_case(rng: random.Random) -> list[int]:
    boundary_mask = rng.randrange(1 << K)
    boundary_parity = rng.randrange(1 << R)
    rows = [rng.randrange(1 << R) for _ in range(K)]
    masks = [rng.randrange(1 << K) for _ in range(P)]

    candidates: list[int] = []
    previous_mask = boundary_mask
    prefix_delta = 0
    for mask in masks:
        edge_mask = previous_mask ^ mask
        prefix_delta ^= parity_delta(rows, edge_mask)
        candidates.append(boundary_parity ^ prefix_delta)
        previous_mask = mask

    next_mask = masks[-1]
    next_parity = candidates[-1]

    parity_llrs = [
        rng.choice([value for value in range(-127, 128) if value])
        for _ in range(R)
    ]
    systematic_base = rng.randint(-1000, 1000)
    systematic_deltas = [rng.randint(-254, 254) for _ in range(K)]

    scores: list[int] = []
    for mask, candidate in zip(masks, candidates):
        score = systematic_base
        for index, delta in enumerate(systematic_deltas):
            if (mask >> index) & 1:
                score += delta
        for index, llr in enumerate(parity_llrs):
            score += -llr if (candidate >> index) & 1 else llr
        scores.append(score)

    return [
        boundary_mask,
        boundary_parity,
        pack(rows, R),
        pack(masks, K),
        pack(candidates, R),
        next_mask,
        next_parity,
        pack([signed_bits(value, LLR_W) for value in parity_llrs], LLR_W),
        signed_bits(systematic_base, SCORE_W),
        pack(
            [signed_bits(value, SCORE_W) for value in systematic_deltas],
            SCORE_W,
        ),
        pack([signed_bits(value, SCORE_W) for value in scores], SCORE_W),
    ]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    rng = random.Random(0x4B414953545F5254)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="ascii") as output:
        for _ in range(CASES):
            output.write(" ".join(f"{value:x}" for value in generate_case(rng)))
            output.write("\n")


if __name__ == "__main__":
    main()
