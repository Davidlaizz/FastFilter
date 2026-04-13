#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

cd "${ROOT_DIR}"
cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --target measure_newfilter256_perf measure_newfilter256_built measure_newfilter256_fpp -j"$(nproc)"

mkdir -p "${ROOT_DIR}/scripts/Inputs"
rm -f "${ROOT_DIR}/scripts/Inputs/NewFilter256" \
      "${ROOT_DIR}/scripts/Inputs/NewFilter256-hitrate" \
      "${ROOT_DIR}/scripts/Inputs/NewFilter256-fill" \
      "${ROOT_DIR}/scripts/build-newfilter256.csv" \
      "${ROOT_DIR}/scripts/fpp_table_newfilter256.csv"

cd "${BUILD_DIR}"
taskset -c 2 ./measure_newfilter256_perf
taskset -c 2 ./measure_newfilter256_built
taskset -c 2 ./measure_newfilter256_fpp

echo "Done. Outputs:"
echo "  ${ROOT_DIR}/scripts/Inputs/NewFilter256"
echo "  ${ROOT_DIR}/scripts/Inputs/NewFilter256-hitrate"
echo "  ${ROOT_DIR}/scripts/Inputs/NewFilter256-fill"
echo "  ${ROOT_DIR}/scripts/build-newfilter256.csv"
echo "  ${ROOT_DIR}/scripts/fpp_table_newfilter256.csv"
