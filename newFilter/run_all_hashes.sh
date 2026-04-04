#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
SCRIPTS_DIR="${SCRIPTS_DIR:-$ROOT/scripts}"
INPUTS_DIR="${INPUTS_DIR:-$SCRIPTS_DIR/Inputs}"
HASH_SCRIPT="${HASH_SCRIPT:-$ROOT/newFilter/hash_images.py}"
PYTHON="${PYTHON:-python3}"

COLUMBIA_DIR="${COLUMBIA_DIR:-$ROOT/ColumbiaArchive/coil-100}"
LG_DIR="${LG_DIR:-$ROOT/LGarchive/dataset}"

METHODS=(phash dhash ahash)
DATASETS=("columbia:$COLUMBIA_DIR" "lg:$LG_DIR")

if [[ ! -f "$HASH_SCRIPT" ]]; then
  echo "hash_images.py not found: $HASH_SCRIPT"
  exit 1
fi

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "Build directory not found: $BUILD_DIR"
  echo "Please run CMake configuration first."
  exit 1
fi

mkdir -p "$INPUTS_DIR"

if command -v taskset >/dev/null 2>&1; then
  RUN_PREFIX=(taskset -c 2)
else
  RUN_PREFIX=()
fi

echo "Building newFilter targets..."
cmake --build "$BUILD_DIR" --target measure_newfilter_perf measure_newfilter_built measure_newfilter_fpp -j"$(nproc)"

for dataset_entry in "${DATASETS[@]}"; do
  IFS=":" read -r dataset_name dataset_path <<< "$dataset_entry"
  if [[ ! -d "$dataset_path" ]]; then
    echo "Skip $dataset_name: dataset not found at $dataset_path"
    continue
  fi

  for method in "${METHODS[@]}"; do
    echo "=== Dataset: $dataset_name | Method: $method ==="
    hash_file="$INPUTS_DIR/${dataset_name}_${method}.txt"

    "$PYTHON" "$HASH_SCRIPT" --input "$dataset_path" --method "$method" --output "$hash_file"

    export SIMDUP_HASH_FILE="$hash_file"
    export SIMDUP_SHUFFLE="${SIMDUP_SHUFFLE:-1}"

    rm -f "$INPUTS_DIR/SimHash-4x16-OR" \
          "$INPUTS_DIR/SimHash-4x16-OR-hitrate" \
          "$INPUTS_DIR/SimHash-4x16-OR-fill" \
          "$SCRIPTS_DIR/build-newfilter.csv" \
          "$SCRIPTS_DIR/fpp_table_newfilter.csv"

    (cd "$BUILD_DIR" && "${RUN_PREFIX[@]}" ./measure_newfilter_perf)
    (cd "$BUILD_DIR" && "${RUN_PREFIX[@]}" ./measure_newfilter_built)
    (cd "$BUILD_DIR" && "${RUN_PREFIX[@]}" ./measure_newfilter_fpp)

    "$PYTHON" "$ROOT/newFilter/plot_results.py" --outdir "$SCRIPTS_DIR"

    outdir="$SCRIPTS_DIR/results_${dataset_name}_${method}"
    mkdir -p "$outdir"
    shopt -s nullglob
    mv "$SCRIPTS_DIR"/newfilter-*.pdf "$outdir"/
    shopt -u nullglob
    cp "$INPUTS_DIR"/SimHash-4x16-OR* "$outdir"/
    cp "$SCRIPTS_DIR/build-newfilter.csv" "$outdir/build-newfilter-${dataset_name}-${method}.csv"
    cp "$SCRIPTS_DIR/fpp_table_newfilter.csv" "$outdir/fpp_table_newfilter-${dataset_name}-${method}.csv"
  done
done

echo "Done. Results saved under $SCRIPTS_DIR/results_*"
