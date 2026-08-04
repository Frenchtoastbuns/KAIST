#!/usr/bin/env bash
set -euo pipefail

COMPACT_ROOT="$(cd "$(dirname "$0")" && pwd)"
FRAME_COUNT="${1:-1000}"
OUTPUT_FILE="${2:-${COMPACT_ROOT}/results/compact_parallel_trace_${FRAME_COUNT}.csv}"
CXX_COMPILER="${CXX:-g++}"
AUDIT_BINARY="${COMPACT_ROOT}/compact_parallel_trace_audit"

mkdir -p "$(dirname "${OUTPUT_FILE}")"
"${CXX_COMPILER}" -std=c++17 -O3 -Wall -Wextra -pedantic \
  -I"${COMPACT_ROOT}" \
  -DCOMPACT_PARALLEL_TRACE_FRAMES="${FRAME_COUNT}" \
  "${COMPACT_ROOT}/compact_parallel_trace_audit.cc" \
  -o "${AUDIT_BINARY}"
"${AUDIT_BINARY}" > "${OUTPUT_FILE}"

echo "Compact trace audit written to ${OUTPUT_FILE}"
