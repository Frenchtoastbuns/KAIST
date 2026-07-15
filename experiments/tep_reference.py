#!/usr/bin/env python3

from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from typing import Iterator

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1


def iter_teps(k: int, order: int) -> Iterator[int]:
    if k < 0:
        raise ValueError("k must be non-negative")
    if not 0 <= order <= k:
        raise ValueError("order must satisfy 0 <= order <= k")

    yield 0

    def visit(start: int, depth: int, mask: int) -> Iterator[int]:
        for index in range(start, k):
            candidate = mask | (1 << index)
            yield candidate
            if depth + 1 < order:
                yield from visit(index + 1, depth + 1, candidate)

    if order:
        yield from visit(0, 0, 0)


def mix_u64(hash_value: int, value: int) -> int:
    for byte in range(8):
        hash_value ^= (value >> (8 * byte)) & 0xFF
        hash_value = (hash_value * FNV_PRIME) & MASK64
    return hash_value


@dataclass(frozen=True)
class Summary:
    k: int
    order: int
    count: int
    mask_hash: int


def summarize(k: int, order: int) -> Summary:
    count = 0
    mask_hash = FNV_OFFSET
    seen: set[int] = set()
    for mask in iter_teps(k, order):
        if mask >> k:
            raise AssertionError(f"invalid mask {mask:#x} for k={k}")
        if mask.bit_count() > order:
            raise AssertionError(
                f"mask {mask:#x} exceeds order {order}"
            )
        if mask in seen:
            raise AssertionError(f"duplicate mask {mask:#x}")
        seen.add(mask)
        count += 1
        mask_hash = mix_u64(mask_hash, mask)
    expected = sum(math.comb(k, weight) for weight in range(order + 1))
    if count != expected:
        raise AssertionError(
            f"missing masks for k={k}, order={order}: "
            f"observed {count}, expected {expected}"
        )
    if len(seen) != expected:
        raise AssertionError(
            f"unique-mask count {len(seen)} does not equal {expected}"
        )
    return Summary(k=k, order=order, count=count, mask_hash=mask_hash)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--k", type=int, required=True)
    parser.add_argument("--order", type=int, required=True)
    args = parser.parse_args()
    result = summarize(args.k, args.order)
    print(
        f"TRACE {result.k} {result.order} {result.count} "
        f"{result.mask_hash:016x}"
    )


if __name__ == "__main__":
    main()
