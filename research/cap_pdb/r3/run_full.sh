#!/usr/bin/env bash
set -euo pipefail
R3_ROOT="$(cd "$(dirname "$0")" && pwd)"
FRAME_COUNT="${1:-1000}"
OUTPUT_DIR="${2:-${R3_ROOT}/reproduced}"
CXX_COMPILER="${CXX:-g++}"
AUDIT_BINARY="${R3_ROOT}/r3_decisive_audit"

"${CXX_COMPILER}" -std=c++17 -O2 -Wall -Wextra -pedantic \
  -I"${R3_ROOT}/source" \
  "${R3_ROOT}/source/r3_decisive_audit.cc" \
  -o "${AUDIT_BINARY}"

mkdir -p "${OUTPUT_DIR}"
for mode in specialized fused_specialized fused_w1 fused_w1w2; do
  "${AUDIT_BINARY}" "${mode}" "${FRAME_COUNT}" 0.0 0 \
    > "${OUTPUT_DIR}/${mode}_${FRAME_COUNT}.csv" \
    2> "${OUTPUT_DIR}/${mode}_${FRAME_COUNT}.log"
done

echo "Four complete raw runs written to ${OUTPUT_DIR}"

if [[ "${FRAME_COUNT}" == "1000" ]]; then
  python3 "${R3_ROOT}/scripts/verify_decisive.py" \
    --results-dir "${OUTPUT_DIR}" \
    --frames 1000 \
    --expected-structural-checks 4096
fi
