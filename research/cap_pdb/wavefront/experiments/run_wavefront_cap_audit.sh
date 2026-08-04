#!/usr/bin/env bash
set -euo pipefail

frames="${1:-1000}"
result_dir="${2:-results/wavefront_cap_2026-07-30}"
parallel_jobs="${3:-4}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$(mktemp -d)"
trap 'rm -rf -- "$build_dir"' EXIT

mkdir -p "$result_dir"

batches=(1 8 32 128 512 1000000000)

for batch in "${batches[@]}"; do
	g++ -I"$script_dir/.." -std=c++17 -O3 -march=native \
		-DRECONSTRUCTED_AUDIT_FRAMES="$frames" \
		-DRECONSTRUCTED_AUDIT_SKIP_COMPILED_DUAL=1 \
		-DOSD_RECONSTRUCTED_INCUMBENT_BATCH="$batch" \
		"$script_dir/reconstructed_cap_pdb_audit.cc" \
		-o "$build_dir/wavefront_b${batch}"
done

run_one()
{
	local batch="$1"
	"$build_dir/wavefront_b${batch}" \
		> "$result_dir/raw_batch_${batch}.csv"
}

active=0
for batch in "${batches[@]}"; do
	run_one "$batch" &
	active=$((active + 1))
	if (( active >= parallel_jobs )); then
		wait -n
		active=$((active - 1))
	fi
done
wait

sha256sum "$result_dir"/raw_batch_*.csv \
	> "$result_dir/RAW_SHA256SUMS.txt"
