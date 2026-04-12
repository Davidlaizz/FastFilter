#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
OUT_CSV="${ROOT_DIR}/scripts/hamming256_linear.csv"

mkdir -p "${BUILD_DIR}" "${ROOT_DIR}/scripts"

cd "${ROOT_DIR}"
cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --target measure_hamming256_linear -j"$(nproc)"

cd "${BUILD_DIR}"
taskset -c 2 ./measure_hamming256_linear --out "${OUT_CSV}"
python3 "${ROOT_DIR}/newHamming256/plot_results.py" --csv "${OUT_CSV}" --outdir "${ROOT_DIR}/scripts"

echo "Done. Outputs in ${ROOT_DIR}/scripts"
