#!/usr/bin/env python3

"""Validate and benchmark test-error-pattern (TEP) delivery policies.

The benchmark intentionally measures delivery only.  It consumes every mask
with a lightweight checksum but does not construct or score OSD candidates.
The default production case is BCH(127,64), OSD order 4: 679,121 masks/block.
"""

from __future__ import annotations

import argparse
import csv
import gc
import math
import os
import platform
import re
import statistics
import sys
import time
import tracemalloc
from array import array
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Iterator, Sequence

from tep_reference import FNV_OFFSET, iter_teps, mix_u64, summarize

MASK64 = (1 << 64) - 1
AGGREGATE_PRIME = 1099511628211


def mask_typecode(k: int) -> str:
    if not 0 <= k <= 64:
        raise ValueError("compact mask storage supports 0 <= k <= 64")
    return "I" if k <= 32 else "Q"


def mask_to_indices(mask: int) -> tuple[int, ...]:
    indices: list[int] = []
    while mask:
        least = mask & -mask
        indices.append(least.bit_length() - 1)
        mask ^= least
    return tuple(indices)


def build_schedule(k: int, order: int) -> array:
    return array(mask_typecode(k), iter_teps(k, order))


def iter_tep_pages(
    k: int, order: int, page_size: int
) -> Iterator[array]:
    """Yield newly owned, bounded pages in production DFS-prefix order."""
    if page_size <= 0:
        raise ValueError("page_size must be positive")
    typecode = mask_typecode(k)
    page = array(typecode)
    for mask in iter_teps(k, order):
        page.append(mask)
        if len(page) == page_size:
            yield page
            page = array(typecode)
    if page:
        yield page


def sequence_fingerprint(values: Iterable[int]) -> tuple[int, int]:
    count = 0
    digest = FNV_OFFSET
    for value in values:
        digest = mix_u64(digest, value)
        count += 1
    return count, digest


def consume_chunk(values: Sequence[int]) -> tuple[int, int]:
    """Consume every item; sum is deliberately lightweight and repeatable."""
    return len(values), sum(values) & MASK64


def consume_single(values: Iterable[int]) -> tuple[int, int]:
    count = 0
    checksum = 0
    subtotal = 0
    for value in values:
        subtotal += value
        count += 1
        if not count & 4095:
            checksum = (checksum + subtotal) & MASK64
            subtotal = 0
    return count, (checksum + subtotal) & MASK64


def aggregate_block(
    aggregate: int, block_count: int, block_checksum: int
) -> int:
    return (
        (aggregate * AGGREGATE_PRIME) ^ block_checksum ^ block_count
    ) & MASK64


@dataclass
class CacheCounters:
    hits: int = 0
    misses: int = 0
    builds: int = 0
    build_ns: int = 0
    last_build_ns: int = 0


class FullScheduleCache:
    def __init__(self, k: int, order: int):
        self.k = k
        self.order = order
        self._storage: array | None = None
        self._readonly: memoryview | None = None
        self.counters = CacheCounters()

    def get(self) -> memoryview:
        if self._readonly is not None:
            self.counters.hits += 1
            return self._readonly
        self.counters.misses += 1
        started = time.perf_counter_ns()
        self._storage = build_schedule(self.k, self.order)
        self._readonly = memoryview(self._storage).toreadonly()
        self.counters.last_build_ns = time.perf_counter_ns() - started
        self.counters.build_ns += self.counters.last_build_ns
        self.counters.builds += 1
        return self._readonly

    @property
    def nbytes(self) -> int:
        return 0 if self._storage is None else len(self._storage) * self._storage.itemsize

    def clear(self) -> None:
        if self._readonly is not None:
            self._readonly.release()
        self._readonly = None
        self._storage = None
        gc.collect()


@dataclass(frozen=True)
class RunResult:
    mode: str
    blocks: int
    candidate_count: int
    aggregate_checksum: int
    elapsed_ns: int
    largest_payload_bytes: int


@dataclass(frozen=True)
class MemoryResult:
    mode: str
    peak_tracemalloc_bytes: int
    largest_payload_bytes: int


class DeliveryBenchmark:
    def __init__(self, k: int, order: int, page_size: int):
        self.k = k
        self.order = order
        self.page_size = page_size
        self.expected_count = sum(
            math.comb(k, weight) for weight in range(order + 1)
        )
        reference = build_schedule(k, order)
        self.itemsize = reference.itemsize
        count, checksum = consume_chunk(reference)
        if count != self.expected_count:
            raise AssertionError("reference schedule count mismatch")
        self.expected_block_checksum = checksum
        self.cache = FullScheduleCache(k, order)
        del reference

    def _check_block(self, count: int, checksum: int) -> None:
        if count != self.expected_count:
            raise AssertionError(
                f"candidate count {count} != {self.expected_count}"
            )
        if checksum != self.expected_block_checksum:
            raise AssertionError(
                f"checksum {checksum:016x} != "
                f"{self.expected_block_checksum:016x}"
            )

    def regenerate(self, blocks: int) -> RunResult:
        started = time.perf_counter_ns()
        aggregate = 0
        largest = 0
        for _ in range(blocks):
            schedule = build_schedule(self.k, self.order)
            largest = max(largest, len(schedule) * schedule.itemsize)
            count, checksum = consume_chunk(schedule)
            self._check_block(count, checksum)
            aggregate = aggregate_block(aggregate, count, checksum)
        elapsed = time.perf_counter_ns() - started
        return RunResult(
            "regenerate_per_block",
            blocks,
            blocks * self.expected_count,
            aggregate,
            elapsed,
            largest,
        )

    def persistent(self, blocks: int) -> RunResult:
        schedule = self.cache.get()
        started = time.perf_counter_ns()
        aggregate = 0
        for _ in range(blocks):
            count, checksum = consume_chunk(schedule)
            self._check_block(count, checksum)
            aggregate = aggregate_block(aggregate, count, checksum)
        elapsed = time.perf_counter_ns() - started
        return RunResult(
            "persistent_full_cache",
            blocks,
            blocks * self.expected_count,
            aggregate,
            elapsed,
            self.cache.nbytes,
        )

    def paged(self, blocks: int) -> RunResult:
        started = time.perf_counter_ns()
        aggregate = 0
        largest = 0
        for _ in range(blocks):
            block_count = 0
            block_checksum = 0
            for page in iter_tep_pages(self.k, self.order, self.page_size):
                largest = max(largest, len(page) * page.itemsize)
                count, checksum = consume_chunk(page)
                block_count += count
                block_checksum = (block_checksum + checksum) & MASK64
            self._check_block(block_count, block_checksum)
            aggregate = aggregate_block(
                aggregate, block_count, block_checksum
            )
        elapsed = time.perf_counter_ns() - started
        return RunResult(
            f"paged_streaming_p{self.page_size}",
            blocks,
            blocks * self.expected_count,
            aggregate,
            elapsed,
            largest,
        )

    def single(self, blocks: int) -> RunResult:
        started = time.perf_counter_ns()
        aggregate = 0
        for _ in range(blocks):
            count, checksum = consume_single(iter_teps(self.k, self.order))
            self._check_block(count, checksum)
            aggregate = aggregate_block(aggregate, count, checksum)
        elapsed = time.perf_counter_ns() - started
        return RunResult(
            "single_tep_streaming",
            blocks,
            blocks * self.expected_count,
            aggregate,
            elapsed,
            self.itemsize,
        )

    def runners(self) -> dict[str, Callable[[int], RunResult]]:
        return {
            "regenerate_per_block": self.regenerate,
            "persistent_full_cache": self.persistent,
            f"paged_streaming_p{self.page_size}": self.paged,
            "single_tep_streaming": self.single,
        }


def inspect_osd_source(path: Path) -> int:
    source = path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"for\s*\(int\s+a\s*=\s*0;\s*O\s*>=\s*1\s*&&\s*a\s*<\s*K;"
    )
    loops = len(pattern.findall(source))
    if loops != 2:
        raise AssertionError(
            f"expected two inline OSD TEP loops in {path}, found {loops}"
        )
    return loops


def validate_modes(
    benchmark: DeliveryBenchmark,
) -> list[dict[str, str | int]]:
    reference = summarize(benchmark.k, benchmark.order)
    expected_hash = reference.mask_hash
    rows: list[dict[str, str | int]] = []

    deliveries: list[tuple[str, Iterable[int]]] = [
        ("regenerate_per_block", iter_teps(benchmark.k, benchmark.order)),
        (
            "persistent_full_cache",
            benchmark.cache.get(),
        ),
        (
            f"paged_streaming_p{benchmark.page_size}",
            (
                mask
                for page in iter_tep_pages(
                    benchmark.k, benchmark.order, benchmark.page_size
                )
                for mask in page
            ),
        ),
        ("single_tep_streaming", iter_teps(benchmark.k, benchmark.order)),
    ]
    for mode, values in deliveries:
        count, digest = sequence_fingerprint(values)
        if count != reference.count or digest != expected_hash:
            raise AssertionError(f"sequence validation failed for {mode}")
        rows.append(
            {
                "mode": mode,
                "k": benchmark.k,
                "order": benchmark.order,
                "candidate_count": count,
                "sequence_hash": f"{digest:016x}",
                "duplicates": 0,
                "missing_masks": 0,
                "invalid_masks": 0,
                "result": "pass",
            }
        )
    return rows


def memory_probe(
    benchmark: DeliveryBenchmark,
) -> list[MemoryResult]:
    results: list[MemoryResult] = []
    for mode, runner in benchmark.runners().items():
        if mode == "persistent_full_cache":
            benchmark.cache.clear()
        gc.collect()
        tracemalloc.start()
        result = runner(1)
        _, peak = tracemalloc.get_traced_memory()
        tracemalloc.stop()
        results.append(
            MemoryResult(mode, peak, result.largest_payload_bytes)
        )
    return results


def percentile(values: Sequence[float], fraction: float) -> float:
    ordered = sorted(values)
    index = max(0, math.ceil(len(ordered) * fraction) - 1)
    return ordered[index]


def cpu_model() -> str:
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"


def write_csv(path: Path, fieldnames: Sequence[str], rows: Sequence[dict]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--k", type=int, default=64)
    parser.add_argument("--order", type=int, default=4)
    parser.add_argument("--page-size", type=int, default=4096)
    parser.add_argument("--blocks", type=int, default=200)
    parser.add_argument("--warmup-blocks", type=int, default=2)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--cache-build-repeats", type=int, default=5)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path(__file__).resolve().parent / "results",
    )
    parser.add_argument(
        "--osd-source",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "osd.hh",
    )
    args = parser.parse_args()

    if (
        args.blocks <= 0
        or args.repeats <= 0
        or args.cache_build_repeats <= 0
        or args.warmup_blocks < 0
    ):
        parser.error("blocks/repeats must be positive and warmup non-negative")
    if not 0 <= args.order <= args.k <= 64:
        parser.error("require 0 <= order <= k <= 64")

    loops = inspect_osd_source(args.osd_source)
    benchmark = DeliveryBenchmark(args.k, args.order, args.page_size)
    validation_rows = validate_modes(benchmark)
    memory_rows = memory_probe(benchmark)
    memory_by_mode = {row.mode: row for row in memory_rows}

    # Rebuild once after the cold memory probe.  The timed cache mode is warm;
    # its one-time construction cost is reported separately.
    cache_build_samples: list[int] = []
    for _ in range(args.cache_build_repeats):
        benchmark.cache.clear()
        benchmark.cache.get()
        cache_build_samples.append(benchmark.cache.counters.last_build_ns)
    cache_build_ns = int(statistics.median(cache_build_samples))

    runners = benchmark.runners()
    mode_names = list(runners)
    for mode in mode_names:
        if args.warmup_blocks:
            runners[mode](args.warmup_blocks)

    raw_rows: list[dict] = []
    reference_aggregate: int | None = None
    for repeat in range(args.repeats):
        # Rotate execution order to reduce systematic thermal/order bias.
        rotation = repeat % len(mode_names)
        ordered_modes = mode_names[rotation:] + mode_names[:rotation]
        for mode in ordered_modes:
            print(
                f"RUN repeat={repeat + 1}/{args.repeats} "
                f"mode={mode} blocks={args.blocks}",
                flush=True,
            )
            result = runners[mode](args.blocks)
            if reference_aggregate is None:
                reference_aggregate = result.aggregate_checksum
            elif result.aggregate_checksum != reference_aggregate:
                raise AssertionError("aggregate checksum differs across modes")
            elapsed_s = result.elapsed_ns / 1e9
            per_block_ms = result.elapsed_ns / args.blocks / 1e6
            teps_per_second = result.candidate_count / elapsed_s
            memory = memory_by_mode[mode]
            raw_rows.append(
                {
                    "repeat": repeat + 1,
                    "mode": mode,
                    "k": args.k,
                    "order": args.order,
                    "page_size": args.page_size if "paged" in mode else "",
                    "blocks": args.blocks,
                    "candidates_per_block": benchmark.expected_count,
                    "total_candidates": result.candidate_count,
                    "elapsed_ns": result.elapsed_ns,
                    "ms_per_block": f"{per_block_ms:.6f}",
                    "teps_per_second": f"{teps_per_second:.3f}",
                    "aggregate_checksum": f"{result.aggregate_checksum:016x}",
                    "peak_tracemalloc_bytes": memory.peak_tracemalloc_bytes,
                    "largest_live_payload_bytes": memory.largest_payload_bytes,
                    "cache_build_ns": cache_build_ns
                    if mode == "persistent_full_cache"
                    else 0,
                    "cache_bytes": benchmark.cache.nbytes
                    if mode == "persistent_full_cache"
                    else 0,
                }
            )

    summary_rows: list[dict] = []
    for mode in mode_names:
        rows = [row for row in raw_rows if row["mode"] == mode]
        latencies = [float(row["ms_per_block"]) for row in rows]
        rates = [float(row["teps_per_second"]) for row in rows]
        memory = memory_by_mode[mode]
        summary_rows.append(
            {
                "mode": mode,
                "k": args.k,
                "order": args.order,
                "page_size": args.page_size if "paged" in mode else "",
                "blocks_per_repeat": args.blocks,
                "repeats": args.repeats,
                "median_ms_per_block": f"{statistics.median(latencies):.6f}",
                "p95_ms_per_block": f"{percentile(latencies, 0.95):.6f}",
                "median_teps_per_second": f"{statistics.median(rates):.3f}",
                "peak_tracemalloc_bytes": memory.peak_tracemalloc_bytes,
                "largest_live_payload_bytes": memory.largest_payload_bytes,
                "cache_build_ms": f"{cache_build_ns / 1e6:.6f}"
                if mode == "persistent_full_cache"
                else "0.000000",
                "cache_bytes": benchmark.cache.nbytes
                if mode == "persistent_full_cache"
                else 0,
                "result": "pass",
            }
        )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    raw_path = args.output_dir / "tep_delivery_benchmark.csv"
    summary_path = args.output_dir / "tep_delivery_summary.csv"
    validation_path = args.output_dir / "tep_delivery_validation.csv"
    text_path = args.output_dir / "tep_delivery_summary.txt"

    write_csv(raw_path, list(raw_rows[0]), raw_rows)
    write_csv(summary_path, list(summary_rows[0]), summary_rows)
    write_csv(validation_path, list(validation_rows[0]), validation_rows)

    with text_path.open("w", encoding="utf-8") as handle:
        handle.write("TEP delivery benchmark\n")
        handle.write(f"python={sys.version.split()[0]}\n")
        handle.write(f"platform={platform.platform()}\n")
        handle.write(f"cpu={cpu_model()}\n")
        handle.write(f"k={args.k} order={args.order}\n")
        handle.write(f"candidates_per_block={benchmark.expected_count}\n")
        handle.write(f"page_size={args.page_size}\n")
        handle.write(f"blocks={args.blocks} repeats={args.repeats}\n")
        handle.write(f"osd_inline_loops_detected={loops}\n")
        handle.write("direct_production_sequence=validated_separately_by_tep_trace_test\n")
        handle.write("timing_scope=TEP_delivery_plus_lightweight_checksum_only\n")
        for row in summary_rows:
            handle.write(
                f"{row['mode']}: median_ms_per_block="
                f"{row['median_ms_per_block']} p95_ms_per_block="
                f"{row['p95_ms_per_block']} median_teps_per_second="
                f"{row['median_teps_per_second']} peak_tracemalloc_bytes="
                f"{row['peak_tracemalloc_bytes']} largest_payload_bytes="
                f"{row['largest_live_payload_bytes']}\n"
            )

    for row in summary_rows:
        print(
            f"RESULT mode={row['mode']} median_ms_per_block="
            f"{row['median_ms_per_block']} median_teps_per_second="
            f"{row['median_teps_per_second']} peak_bytes="
            f"{row['peak_tracemalloc_bytes']}",
            flush=True,
        )
    print(f"WROTE {raw_path}")
    print(f"WROTE {summary_path}")
    print(f"WROTE {validation_path}")
    print(f"WROTE {text_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
