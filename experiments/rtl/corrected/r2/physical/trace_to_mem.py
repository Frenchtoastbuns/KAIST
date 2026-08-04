#!/usr/bin/env python3
"""Verify the frozen canonical trace and emit one hexadecimal byte per line."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

EXPECTED_SHA256 = "c3cd084c4aa8c457f87d0d72efb8c81c8fc8385b635be7b91117c1a881ae2f5a"
EXPECTED_SEED = 5928218492399464753
EXPECTED_STREAM = 11508365490867720138
EXPECTED_GENERATOR = 15049493467287215415
EXPECTED_TRACE = 332897943003122562
EXPECTED_FRAMES = 1000
EXPECTED_HEADER = 96
EXPECTED_RECORD = 1778


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()

    data = args.trace.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(f"trace SHA-256 mismatch: {digest}")
    if data[:8] != b"CAPCAN1\0":
        raise SystemExit("bad trace magic")

    values = struct.unpack_from("<11Ii5Q", data, 8)
    version, header_bytes, record_bytes, frames = values[:4]
    n, k, order, groups, states, group_bits = values[4:10]
    soft_scale, ebn0_milli = values[10:12]
    seed, stream_checksum, generator_checksum, trace_checksum, tie_frames = values[12:]

    expected_size = EXPECTED_HEADER + EXPECTED_FRAMES * EXPECTED_RECORD
    if len(data) != expected_size:
        raise SystemExit(f"trace size mismatch: {len(data)} != {expected_size}")
    checks = {
        "version": (version, 1),
        "header_bytes": (header_bytes, EXPECTED_HEADER),
        "record_bytes": (record_bytes, EXPECTED_RECORD),
        "frames": (frames, EXPECTED_FRAMES),
        "n": (n, 128),
        "k": (k, 64),
        "order": (order, 4),
        "groups": (groups, 13),
        "states": (states, 32),
        "group_bits": (group_bits, 5),
        "soft_scale": (soft_scale, 32),
        "ebn0_milli": (ebn0_milli, 0),
        "seed": (seed, EXPECTED_SEED),
        "stream_checksum": (stream_checksum, EXPECTED_STREAM),
        "generator_checksum": (generator_checksum, EXPECTED_GENERATOR),
        "trace_checksum": (trace_checksum, EXPECTED_TRACE),
    }
    bad = {name: pair for name, pair in checks.items() if pair[0] != pair[1]}
    if bad:
        raise SystemExit(f"frozen trace metadata mismatch: {bad}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("".join(f"{byte:02x}\n" for byte in data))
    manifest = {
        "sha256": digest,
        "bytes": len(data),
        "frames": frames,
        "seed": seed,
        "stream_checksum": stream_checksum,
        "generator_checksum": generator_checksum,
        "trace_checksum": trace_checksum,
        "tie_frames": tie_frames,
        "header_bytes": header_bytes,
        "record_bytes": record_bytes,
    }
    if args.manifest:
        args.manifest.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print("FROZEN_TRACE_MEM_PASS " + json.dumps(manifest, sort_keys=True))


if __name__ == "__main__":
    main()
