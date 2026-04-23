#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

echo "Root: ${ROOT_DIR}"
mkdir -p "${BUILD_DIR}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" \
  --target measure_newfilter256_compact_perf measure_newfilter256_compact_built measure_newfilter256_compact_fpp \
  -j"$(nproc)"

mkdir -p "${ROOT_DIR}/scripts/Inputs"
rm -f "${ROOT_DIR}/scripts/Inputs/NewFilter256Compact" \
      "${ROOT_DIR}/scripts/Inputs/NewFilter256Compact-hitrate" \
      "${ROOT_DIR}/scripts/Inputs/NewFilter256Compact-fill" \
      "${ROOT_DIR}/scripts/Inputs/NewFilter256Compact-confusion" \
      "${ROOT_DIR}/scripts/build-newfilter256compact.csv" \
      "${ROOT_DIR}/scripts/fpp_table_newfilter256compact.csv"

cd "${BUILD_DIR}"
taskset -c 2 ./measure_newfilter256_compact_perf
taskset -c 2 ./measure_newfilter256_compact_built
taskset -c 2 ./measure_newfilter256_compact_fpp

python3 "${ROOT_DIR}/newfilter256_compact/plot_results.py" --outdir "${ROOT_DIR}/scripts"

echo "Done."
echo "Data:"
echo "  ${ROOT_DIR}/scripts/Inputs/NewFilter256Compact"
echo "  ${ROOT_DIR}/scripts/Inputs/NewFilter256Compact-hitrate"
echo "  ${ROOT_DIR}/scripts/Inputs/NewFilter256Compact-fill"
echo "  ${ROOT_DIR}/scripts/Inputs/NewFilter256Compact-confusion"
echo "  ${ROOT_DIR}/scripts/build-newfilter256compact.csv"
echo "  ${ROOT_DIR}/scripts/fpp_table_newfilter256compact.csv"
echo "Plots:"
echo "  ${ROOT_DIR}/scripts/newfilter256compact-throughput.pdf"
echo "  ${ROOT_DIR}/scripts/newfilter256compact-hitrate.pdf"
echo "  ${ROOT_DIR}/scripts/newfilter256compact-items.pdf"
echo "  ${ROOT_DIR}/scripts/newfilter256compact-filter-branches.pdf"
echo "  ${ROOT_DIR}/scripts/newfilter256compact-verify-branches.pdf"
echo "  ${ROOT_DIR}/scripts/newfilter256compact-confusion.pdf"
echo "  ${ROOT_DIR}/scripts/newfilter256compact-insert-outcomes.pdf"
